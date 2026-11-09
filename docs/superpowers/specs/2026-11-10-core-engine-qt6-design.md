# Viplyr: lõi phát video tự viết (FFmpeg libav) + giao diện Qt6

Ngày: 2026-11-10 · Nhánh: `feat/core-engine-qt6`

## 1. Mục đích

Viplyr hiện dùng Qt5 Multimedia (đã hết vòng đời, backend GStreamer cũ, là hộp đen).
Dự án cần một phần lõi **tự kiểm soát hoàn toàn**, chạy nhanh, và **dùng lại được** trong
các dự án Robotics / mô phỏng / thị giác máy tính về sau: lấy được từng khung hình kèm mốc
thời gian, tua chính xác từng khung, giải mã bằng CPU hoặc GPU, và cắm thêm nơi nhận khung
hình (OpenCV, ROS, ghi tệp) mà không sửa giao diện.

## 2. Mục tiêu

- Thư viện `viplyr_core` (C++17), **chỉ phụ thuộc FFmpeg**, không phụ thuộc Qt.
- Giải mã phần cứng (VAAPI, NVDEC/CUDA), tự rơi về CPU khi không dùng được.
- Đồng bộ âm thanh–hình ảnh, tua chính xác từng khung, bước từng khung tới/lui,
  đổi tốc độ phát mà không méo cao độ.
- Nhiều nơi nhận khung hình cùng lúc (`IVideoSink`): màn hình, CV, ROS...
- `FrameReader`: đọc khung hình đồng bộ (không luồng, không đồng hồ) cho xử lý hàng loạt,
  ảnh xem trước và kiểm thử.
- Ứng dụng Qt6 Widgets giữ nguyên tính năng hiện có và sửa các lỗi đã tìm thấy (mục 7).

## 3. Ngoài phạm vi (đợt này)

- Chỉnh sửa video (cắt, crop, đổi tỉ lệ, thay âm thanh) — đợt sau, qua `ffmpeg`.
- Thiết kế lại giao diện (thanh điều khiển phủ lên video, icon mới) — đợt sau.
- Node ROS 2 thật. Máy phát triển chưa cài ROS, nên đợt này chỉ có điểm cắm (`IVideoSink`)
  và ví dụ `frame_dump`. Tài liệu `docs/integration.md` mô tả cách viết publisher ROS 2.
- Hiển thị zero-copy (khung hình ở lại GPU từ giải mã tới màn hình). Đợt này GPU giải mã
  rồi tải khung hình về CPU để vẽ. Tuỳ chọn `keepHwFrames` đã giữ sẵn khung hình trên GPU
  cho nơi nhận nào tự xử lý được.
- Phụ đề nhúng trong tệp, chọn track âm thanh.

## 4. Điều cần tránh

- Lõi bị dính vào Qt hoặc widget (mất khả năng tái sử dụng).
- Viết sẵn backend/tích hợp không có cách kiểm thử trên máy này.

## 5. Kiến trúc

```
core/                      thư viện viplyr_core (C++17 + FFmpeg)
  include/viplyr/
    Frame.h                VideoFrame (bọc AVFrame, pts tính bằng giây), PixelFormat
    Sinks.h                IVideoSink, IAudioSink, AudioFormat
    Player.h               Player, MediaInfo, OpenOptions, HwAccel, State
    FrameReader.h          đọc khung hình đồng bộ
    Playlist.h             danh sách phát thuần C++
  src/                     Player.cpp, FrameReader.cpp, Frame.cpp, CodecUtil.*, Queues.h, Clock.h
  tests/                   kiểm thử không cần màn hình (ctest)
  examples/frame_dump.cpp  ví dụ nơi nhận khung hình cho CV
app/                       ứng dụng Qt6 Widgets (lớp mỏng)
  PlayerController         QObject bọc Player + Playlist + phụ đề, phát signal cho UI
  VideoWidget              QOpenGLWidget, là một IVideoSink (shader YUV→RGB, zoom/pan/xoay)
  AudioOutput              IAudioSink dựa trên QAudioSink
  ThumbnailProvider        luồng riêng + FrameReader, yêu cầu mới nhất thắng
  MainWindow, Components   chuyển từ bản Qt5
```

### Luồng dữ liệu trong `Player`

```
luồng demux ──► hàng gói video ──► luồng giải mã video ──► hàng khung hình ──► luồng trình chiếu ──► IVideoSink(s)
            └─► hàng gói audio ──► luồng giải mã audio (libavfilter: atempo + đổi định dạng) ──► IAudioSink
```

- **Đồng hồ chủ**: đồng hồ ngoại suy theo thời gian thực, được luồng audio hiệu chỉnh liên
  tục theo lượng âm thanh thực sự đã phát (`pts cuối đã ghi − lượng còn đệm × tốc độ`).
  Tệp không có audio (hoặc không có `IAudioSink`) thì đồng hồ tự chạy theo thời gian hệ thống.
- **Tua**: yêu cầu tua được gộp (cái mới nhất thắng). Luồng demux tua về keyframe phía
  trước, xoá hàng đợi, tăng "số sê-ri"; các luồng giải mã xả bộ giải mã rồi bỏ mọi khung
  hình/âm thanh trước mốc đích → kết quả chính xác từng khung. Khung hình mang sê-ri cũ bị loại.
- **Bước từng khung**: khi tạm dừng, bước tới = trình chiếu khung kế tiếp trong hàng;
  bước lui = tua chính xác tới `pts hiện tại − 1 khung`.
- **Trễ hình**: khung hình trễ quá ngưỡng và còn khung mới hơn thì bị bỏ.
- **Hết tệp**: demux gửi gói "xả cạn"; khi cả hai luồng giải mã xong và hàng khung hình
  rỗng thì gọi `onEnded`.

### Giao diện công khai (tóm tắt)

```cpp
struct OpenOptions { HwAccel hw = HwAccel::Auto; bool keepHwFrames = false;
                     std::map<std::string,std::string> formatOptions; };
class Player {
  void addVideoSink(IVideoSink*);  void setAudioSink(IAudioSink*);
  bool open(const std::string& url, const OpenOptions&, std::string* error);
  void close();  void play();  void pause();
  void seek(double seconds);  void stepFrame(int direction);  void setRate(double);
  double position() const;  double duration() const;  State state() const;  MediaInfo info() const;
  std::function<void()> onEnded;  std::function<void(const std::string&)> onError;
  std::function<void(State)> onStateChanged;      // gọi từ luồng nội bộ
};
class IVideoSink { virtual void present(const VideoFrame&) = 0; virtual void flush() {} };
class IAudioSink { format(), write(float*, frames), bufferedSeconds(), flush(), setPaused(bool) };
```

`open()` nhận mọi URL mà libavformat hiểu (tệp, `rtsp://`, `http://`...), nên nguồn mạng
dùng được ngay; tinh chỉnh độ trễ thấp truyền qua `formatOptions`.

`VideoFrame` cho truy cập mặt phẳng điểm ảnh, `pts`, kích thước, định dạng, và
`convert(PixelFormat, w, h)` (ra BGR24/RGBA/YUV420P qua swscale) — BGR24 khớp trực tiếp
với `cv::Mat`. Khi `keepHwFrames = true`, khung hình ở lại GPU; `download()` tải về CPU.

## 6. Các lựa chọn của người viết (chưa được chủ dự án xác nhận riêng)

- **Qt 6.4 từ apt là đủ**, không cần cài Qt 6.8: lõi không còn dùng Qt Multimedia để giải
  mã, chỉ dùng `QAudioSink` để phát âm thanh.
- Thứ tự thử giải mã phần cứng ở chế độ `Auto`: VAAPI → CUDA → CPU.
- Vẽ bằng `QOpenGLWidget` + shader; định dạng vẽ trực tiếp: YUV420P, NV12. Định dạng khác
  (10-bit...) được đổi về YUV420P 8-bit trước khi vẽ.
- `open()` chạy đồng bộ (tệp cục bộ mở trong vài chục mili-giây).
- Bộ phân tích `.srt` ở lại phía ứng dụng (dùng Qt).
- Ảnh xem trước lấy keyframe gần nhất (nhanh), không lấy khung chính xác.
- Xoay theo metadata của tệp (video quay bằng điện thoại) được áp dụng tự động.

## 7. Lỗi cũ được xử lý trong bản mới

1. Phụ đề dính lại sau khi tua. 2. Ảnh xem trước lưu sai vị trí (giờ mỗi kết quả mang đúng
vị trí đã yêu cầu, có giãn nhịp). 3. Prev/Next ở đầu/cuối danh sách làm dừng phát.
4. Kéo thả thư mục. 5. Cuộn ngang làm thu nhỏ, zoom không giới hạn. 6. Con trỏ không ẩn khi
toàn màn hình. 7. Đường dẫn tương đối từ terminal. 8. Thanh điều khiển ẩn khi đang dùng.
9. Mất tệp khi thả hai lần liên tiếp. Thêm: build mặc định ở chế độ Release, nhớ âm lượng,
phụ đề dài tự xuống dòng, không mở chồng hộp thoại lỗi.

## 8. Kiểm chứng

- `ctest`: tạo video thử bằng `ffmpeg`, kiểm tra `FrameReader` (số khung, pts tăng dần,
  tua chính xác), `Player` chạy không màn hình với nơi nhận giả (đồng bộ, tua, bước khung,
  hết tệp), `Playlist`, và giải mã phần cứng nếu máy có.
- Chạy ứng dụng thật trên X11 và chụp màn hình để xác nhận hiển thị.

## 9. Phụ thuộc

```
sudo apt install pkg-config qt6-base-dev qt6-multimedia-dev \
  libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libavfilter-dev
```

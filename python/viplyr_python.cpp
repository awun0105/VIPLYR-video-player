// Python module "viplyr": the core engine for Python code (OpenCV, PyTorch,
// ROS 2 Python nodes...).
//
//   reader = viplyr.FrameReader("video.mp4")
//   for image, pts in reader:            # image: numpy uint8 (H, W, 3), BGR
//       ...
//
// Pixels are not copied on the way to NumPy: an array keeps its frame alive.
// GPU frames (keep_gpu=True with CUDA) expose __cuda_array_interface__, so
// torch.as_tensor(frame.luma, device="cuda") reads them in place.
#include <pybind11/functional.h>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <mutex>

#include "viplyr/FrameReader.h"
#include "viplyr/Logging.h"
#include "viplyr/Player.h"
#include "viplyr/VideoRecorder.h"

namespace py = pybind11;
using namespace viplyr;

namespace {

HwAccel parseHwAccel(const std::string &name) {
    if (name == "auto") return HwAccel::Auto;
    if (name == "none" || name == "cpu") return HwAccel::None;
    if (name == "vaapi") return HwAccel::Vaapi;
    if (name == "cuda") return HwAccel::Cuda;
    throw py::value_error("hw must be 'auto', 'none', 'vaapi' or 'cuda'");
}

// Keyword arguments shared by FrameReader and Player.open.
struct SourceArguments {
    std::string hw = "auto";
    bool keepGpu = false;
    std::map<std::string, std::string> formatOptions;
    std::string inputFormat;
    bool lowLatency = false;
};

OpenOptions makeOptions(const SourceArguments &arguments) {
    OpenOptions options;
    options.hwAccel = parseHwAccel(arguments.hw);
    options.keepHwFrames = arguments.keepGpu;
    options.formatOptions = arguments.formatOptions;
    options.inputFormat = arguments.inputFormat;
    options.lowLatency = arguments.lowLatency;
    return options;
}

// "bgr" and "rgb" give (H, W, 3); "gray" gives (H, W).
PixelFormat parseFormat(const std::string &name) {
    if (name == "bgr") return PixelFormat::Bgr24;
    if (name == "rgb" || name == "rgba") return PixelFormat::Rgba;
    if (name == "gray") return PixelFormat::Gray8;
    throw py::value_error("format must be 'bgr', 'rgb', 'rgba' or 'gray'");
}

// A NumPy view of a frame's pixels; the capsule keeps the frame alive.
py::array toArray(const VideoFrame &source, const std::string &format, int width, int height) {
    const PixelFormat target = parseFormat(format);
    auto *frame = new VideoFrame(source.convert(target, width, height));
    if (!frame->isValid()) {
        delete frame;
        throw std::runtime_error("could not convert the frame");
    }
    py::capsule owner(frame, [](void *pointer) { delete static_cast<VideoFrame *>(pointer); });

    const auto rows = static_cast<py::ssize_t>(frame->height());
    const auto columns = static_cast<py::ssize_t>(frame->width());
    const auto stride = static_cast<py::ssize_t>(frame->stride(0));
    const uint8_t *pixels = frame->plane(0);
    if (format == "gray") return py::array_t<uint8_t>({rows, columns}, {stride, py::ssize_t(1)}, pixels, owner);
    if (format == "rgb") {  // RGBA without the alpha channel, as a strided view
        return py::array_t<uint8_t>({rows, columns, py::ssize_t(3)}, {stride, py::ssize_t(4), py::ssize_t(1)}, pixels,
                                    owner);
    }
    const py::ssize_t channels = target == PixelFormat::Rgba ? 4 : 3;
    return py::array_t<uint8_t>({rows, columns, channels}, {stride, channels, py::ssize_t(1)}, pixels, owner);
}

// One plane of a CUDA frame, readable in place by PyTorch, CuPy or Numba.
struct CudaPlane {
    VideoFrame frame;  // keeps the GPU surface alive
    uint64_t pointer = 0;
    int width = 0;     // samples per row
    int height = 0;
    int pitch = 0;
    int channels = 1;  // 2 for the interleaved chroma plane
    bool sixteenBit = false;

    py::dict cudaArrayInterface() const {
        py::dict interface;
        const int itemSize = sixteenBit ? 2 : 1;
        if (channels == 1) {
            interface["shape"] = py::make_tuple(height, width);
            interface["strides"] = py::make_tuple(pitch, itemSize);
        } else {
            interface["shape"] = py::make_tuple(height, width, channels);
            interface["strides"] = py::make_tuple(pitch, itemSize * channels, itemSize);
        }
        interface["typestr"] = sixteenBit ? "<u2" : "|u1";
        interface["data"] = py::make_tuple(pointer, true);  // read-only
        interface["version"] = 3;
        interface["stream"] = py::none();  // the decoder has finished writing
        return interface;
    }
};

// A decoded frame as handed to Python.
struct PyFrame {
    VideoFrame frame;

    double pts() const { return frame.pts(); }
    int width() const { return frame.width(); }
    int height() const { return frame.height(); }
    bool isGpu() const { return frame.isHardware(); }

    py::array toNumpy(const std::string &format, int width, int height) const {
        return toArray(frame, format, width, height);
    }

    CudaPlane plane(int index) const {
        const CudaImage image = frame.cudaPlanes();
        if (!image.isValid()) throw std::runtime_error("not a CUDA frame: open with hw='cuda', keep_gpu=True");
        CudaPlane plane;
        plane.frame = frame;
        plane.pointer = image.planes[index];
        plane.width = image.width[index];
        plane.height = image.height[index];
        plane.pitch = image.pitch[index];
        plane.channels = index == 0 ? 1 : 2;
        plane.sixteenBit = image.format == PixelFormat::P010;
        return plane;
    }
};

py::dict infoToDict(const MediaInfo &info) {
    const auto tracks = [](const std::vector<TrackInfo> &list) {
        py::list result;
        for (const TrackInfo &track : list) {
            py::dict item;
            item["stream_index"] = track.streamIndex;
            item["language"] = track.language;
            item["title"] = track.title;
            item["codec"] = track.codec;
            item["is_text"] = track.isText;
            result.append(item);
        }
        return result;
    };
    py::dict result;
    result["url"] = info.url;
    result["duration"] = info.duration;
    result["width"] = info.width;
    result["height"] = info.height;
    result["frame_rate"] = info.frameRate;
    result["rotation"] = info.rotationDegrees;
    result["pixel_aspect_ratio"] = info.pixelAspectRatio;
    result["interlaced"] = info.isInterlaced;
    result["video_codec"] = info.videoCodec;
    result["audio_codec"] = info.audioCodec;
    result["hardware_decoder"] = info.hardwareDecoder;
    result["has_audio"] = info.hasAudio;
    result["audio_tracks"] = tracks(info.audioTracks);
    result["subtitle_tracks"] = tracks(info.subtitleTracks);
    return result;
}

// FrameReader with Python-friendly errors and iteration.
class PyFrameReader {
public:
    PyFrameReader(const std::string &url, const std::string &format, const SourceArguments &arguments)
        : format_(format) {
        std::string error;
        bool opened;
        {
            py::gil_scoped_release release;
            opened = reader_.open(url, makeOptions(arguments), &error);
        }
        if (!opened) throw std::runtime_error(error.empty() ? "could not open " + url : error);
    }

    py::dict info() const { return infoToDict(reader_.info()); }

    bool seek(double seconds, bool accurate) {
        py::gil_scoped_release release;
        return reader_.seek(seconds, accurate ? SeekMode::Accurate : SeekMode::Keyframe);
    }

    // The next frame as a PyFrame, or None at the end.
    py::object readFrame() {
        PyFrame result;
        bool ok;
        {
            py::gil_scoped_release release;
            ok = reader_.read(result.frame);
        }
        if (!ok) return py::none();
        return py::cast(result);
    }

    // The next frame as (numpy image, pts), or None at the end.
    py::object read() {
        py::object frame = readFrame();
        if (frame.is_none()) return frame;
        const PyFrame &decoded = frame.cast<const PyFrame &>();
        return py::make_tuple(decoded.toNumpy(format_, 0, 0), decoded.pts());
    }

    py::tuple next() {
        py::object item = read();
        if (item.is_none()) throw py::stop_iteration();
        return item.cast<py::tuple>();
    }

    void close() { reader_.close(); }

private:
    FrameReader reader_;
    std::string format_;
};

// Calls a Python function for every frame the player presents.
class CallbackSink : public IVideoSink {
public:
    void setCallback(std::function<void(PyFrame)> callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        callback_ = std::move(callback);
    }

    void present(const VideoFrame &frame) override {
        std::function<void(PyFrame)> callback;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callback = callback_;
        }
        if (!callback) return;
        py::gil_scoped_acquire gil;
        try {
            callback(PyFrame{frame});
        } catch (py::error_already_set &error) {
            error.discard_as_unraisable("viplyr frame callback");
        }
    }

private:
    std::mutex mutex_;
    std::function<void(PyFrame)> callback_;
};

// Real-time playback (video clock only: there is no audio output in Python).
class PyPlayer {
public:
    PyPlayer() {
        player_.addVideoSink(&sink_);
        player_.addVideoSink(&recorder_);  // idle until start_recording()
    }
    ~PyPlayer() {
        py::gil_scoped_release release;  // engine threads may be waiting for the GIL
        player_.close();
        recorder_.stop();
    }

    void open(const std::string &url, const SourceArguments &arguments) {
        std::string error;
        bool opened;
        {
            py::gil_scoped_release release;
            opened = player_.open(url, makeOptions(arguments), &error);
        }
        if (!opened) throw std::runtime_error(error.empty() ? "could not open " + url : error);
    }

    void close() {
        py::gil_scoped_release release;
        player_.close();
    }

    Player &engine() { return player_; }
    CallbackSink &sink() { return sink_; }
    VideoRecorder &recorder() { return recorder_; }

private:
    Player player_;
    CallbackSink sink_;
    VideoRecorder recorder_;
};

RecorderOptions makeRecorderOptions(const std::string &encoder, int quality, int width, int height,
                                    double frameRate = 30.0) {
    RecorderOptions options;
    options.frameRate = frameRate;
    options.encoder = encoder;
    options.quality = quality;
    options.width = width;
    options.height = height;
    return options;
}

void startRecording(VideoRecorder &recorder, const std::string &path, const RecorderOptions &options) {
    std::string error;
    if (!recorder.start(path, options, &error)) throw std::runtime_error(error);
}

// Returns the number of frames written; raises when recording failed.
int64_t stopRecording(VideoRecorder &recorder) {
    std::string error;
    bool ok;
    {
        py::gil_scoped_release release;
        ok = recorder.stop(&error);
    }
    if (!ok) throw std::runtime_error(error);
    return recorder.framesWritten();
}

std::string stateName(State state) {
    switch (state) {
        case State::Stopped: return "stopped";
        case State::Paused:  return "paused";
        case State::Playing: return "playing";
        case State::Ended:   return "ended";
    }
    return "stopped";
}

}  // namespace

PYBIND11_MODULE(viplyr, module) {
    module.doc() = "Video decoding and playback engine (FFmpeg), CPU and GPU.";

    setLogLevel(LogLevel::Warning);  // FFmpeg's informational output is noise in scripts
    module.def(
        "set_log_level",
        [](const std::string &level) {
            static const std::map<std::string, LogLevel> levels = {{"quiet", LogLevel::Quiet},
                                                                   {"error", LogLevel::Error},
                                                                   {"warning", LogLevel::Warning},
                                                                   {"info", LogLevel::Info},
                                                                   {"debug", LogLevel::Debug}};
            const auto found = levels.find(level);
            if (found == levels.end()) throw py::value_error("level: quiet, error, warning, info or debug");
            setLogLevel(found->second);
        },
        py::arg("level"), "How much FFmpeg prints to stderr (default 'warning').");

    py::class_<CudaPlane>(module, "CudaPlane", "One plane of a CUDA frame, for torch.as_tensor / cupy.asarray.")
        .def_property_readonly("__cuda_array_interface__", &CudaPlane::cudaArrayInterface)
        .def_readonly("pointer", &CudaPlane::pointer)
        .def_readonly("width", &CudaPlane::width)
        .def_readonly("height", &CudaPlane::height)
        .def_readonly("pitch", &CudaPlane::pitch);

    py::class_<PyFrame>(module, "Frame", "A decoded video frame.")
        .def_property_readonly("pts", &PyFrame::pts, "Seconds from the start of the media.")
        .def_property_readonly("width", &PyFrame::width)
        .def_property_readonly("height", &PyFrame::height)
        .def_property_readonly("is_gpu", &PyFrame::isGpu)
        .def("to_numpy", &PyFrame::toNumpy, py::arg("format") = "bgr", py::arg("width") = 0, py::arg("height") = 0,
             "Image as a numpy array: 'bgr'/'rgb' (H, W, 3), 'rgba' (H, W, 4), 'gray' (H, W). "
             "Optional width/height scale it. GPU frames are copied to memory first.")
        .def_property_readonly("luma", [](const PyFrame &frame) { return frame.plane(0); },
                               "Y plane of a CUDA frame (H, W).")
        .def_property_readonly("chroma", [](const PyFrame &frame) { return frame.plane(1); },
                               "Interleaved UV plane of a CUDA frame (H/2, W/2, 2).");

    py::class_<PyFrameReader>(module, "FrameReader",
                              "Reads frames one by one on the calling thread. Iterating gives (image, pts).")
        .def(py::init([](const std::string &url, const std::string &hw, bool keepGpu, const std::string &format,
                         const std::map<std::string, std::string> &formatOptions, const std::string &inputFormat,
                         bool lowLatency) {
                 return new PyFrameReader(url, format, {hw, keepGpu, formatOptions, inputFormat, lowLatency});
             }),
             py::arg("url"), py::arg("hw") = "auto", py::arg("keep_gpu") = false, py::arg("format") = "bgr",
             py::arg("format_options") = std::map<std::string, std::string>(), py::arg("input_format") = "",
             py::arg("low_latency") = false,
             "input_format: 'v4l2' (cameras, automatic for /dev/video*), 'x11grab', 'lavfi'... "
             "format_options: e.g. {'video_size': '1280x720', 'framerate': '30'} for a camera.")
        .def_property_readonly("info", &PyFrameReader::info)
        .def("seek", &PyFrameReader::seek, py::arg("seconds"), py::arg("accurate") = true,
             "Accurate: the next read is the frame shown at that time. Otherwise the keyframe before it.")
        .def("read", &PyFrameReader::read, "(image, pts) or None at the end.")
        .def("read_frame", &PyFrameReader::readFrame, "A Frame or None at the end.")
        .def("close", &PyFrameReader::close)
        .def("__iter__", [](py::object self) { return self; })
        .def("__next__", &PyFrameReader::next)
        .def("__enter__", [](py::object self) { return self; })
        .def("__exit__", [](PyFrameReader &reader, py::args) { reader.close(); });

    py::class_<VideoRecorder>(module, "Recorder",
                              "Encodes frames into a video file on its own thread: write(frame) never blocks.")
        .def(py::init<>())
        .def("start",
             [](VideoRecorder &r, const std::string &path, const std::string &encoder, int quality, int width,
                int height) { startRecording(r, path, makeRecorderOptions(encoder, quality, width, height)); },
             py::arg("path"), py::arg("encoder") = "auto", py::arg("quality") = 20, py::arg("width") = 0,
             py::arg("height") = 0, "encoder: 'auto' (NVENC, else libx264) or any FFmpeg encoder name.")
        .def("write", [](VideoRecorder &r, const PyFrame &frame) { r.present(frame.frame); }, py::arg("frame"))
        .def("stop", &stopRecording, "Finishes the file; returns the number of frames written.")
        .def_property_readonly("is_recording", &VideoRecorder::isRecording)
        .def_property_readonly("frames_written", &VideoRecorder::framesWritten)
        .def_property_readonly("dropped_frames", &VideoRecorder::droppedFrames)
        .def_property_readonly("encoder", &VideoRecorder::encoderName);

    py::class_<PyPlayer>(module, "Player",
                         "Real-time playback; on_frame(frame) is called from an engine thread for every frame.")
        .def(py::init<>())
        .def("open",
             [](PyPlayer &p, const std::string &url, const std::string &hw, bool keepGpu,
                const std::map<std::string, std::string> &formatOptions, const std::string &inputFormat,
                bool lowLatency) { p.open(url, {hw, keepGpu, formatOptions, inputFormat, lowLatency}); },
             py::arg("url"), py::arg("hw") = "auto", py::arg("keep_gpu") = false,
             py::arg("format_options") = std::map<std::string, std::string>(), py::arg("input_format") = "",
             py::arg("low_latency") = false,
             "low_latency: for cameras and streams; frames are shown as soon as they are decoded.")
        .def("start_recording",
             [](PyPlayer &p, const std::string &path, const std::string &encoder, int quality, int width, int height) {
                 const double frameRate = p.engine().info().frameRate;
                 startRecording(p.recorder(), path,
                                makeRecorderOptions(encoder, quality, width, height, frameRate > 0 ? frameRate : 30.0));
             },
             py::arg("path"), py::arg("encoder") = "auto", py::arg("quality") = 20, py::arg("width") = 0,
             py::arg("height") = 0, "Records the frames being played into a video file.")
        .def("stop_recording", [](PyPlayer &p) { return stopRecording(p.recorder()); },
             "Finishes the file; returns the number of frames written.")
        .def("close", &PyPlayer::close)
        .def("play", [](PyPlayer &p) { p.engine().play(); })
        .def("pause", [](PyPlayer &p) { p.engine().pause(); })
        .def("seek", [](PyPlayer &p, double seconds) { p.engine().seek(seconds); }, py::arg("seconds"))
        .def("step_frame", [](PyPlayer &p, int direction) { p.engine().stepFrame(direction); },
             py::arg("direction") = 1)
        .def_property("rate", [](PyPlayer &p) { return p.engine().rate(); },
                      [](PyPlayer &p, double rate) { p.engine().setRate(rate); })
        .def_property_readonly("position", [](PyPlayer &p) { return p.engine().position(); })
        .def_property_readonly("duration", [](PyPlayer &p) { return p.engine().duration(); })
        .def_property_readonly("state", [](PyPlayer &p) { return stateName(p.engine().state()); })
        .def_property_readonly("info", [](PyPlayer &p) { return infoToDict(p.engine().info()); })
        .def_property("on_frame", nullptr,
                      [](PyPlayer &p, std::function<void(PyFrame)> callback) { p.sink().setCallback(callback); });
}

// ROS 2 node that plays a video (file, RTSP/HTTP stream, anything FFmpeg opens)
// in real time and publishes its frames as sensor_msgs/Image + CameraInfo.
//
// Parameters
//   url        (string)  what to play: a file, rtsp://..., /dev/video0 (camera); required
//   input_format (string) device type when it cannot be guessed: v4l2, x11grab, lavfi [""]
//   format_options (string[]) "key=value" for FFmpeg, e.g. ["video_size=1280x720",
//                        "framerate=30", "input_format=mjpeg"] for a camera,
//                        ["rtsp_transport=tcp"] for RTSP                          [[]]
//   low_latency (bool)   live sources: publish each frame as soon as it is decoded [false]
//   record_path (string) file that ~/set_recording writes (.mp4, .mkv)            [""]
//   hw         (string)  decoder: auto | none | vaapi | cuda          [auto]
//   encoding   (string)  bgr8 | rgb8 | mono8                          [bgr8]
//   width, height (int)  scale the published image; 0 keeps the size [0]
//   rate       (double)  playback speed, 0.5 .. 4                     [1.0]
//   loop       (bool)    start again at the end                       [true]
//   frame_id   (string)  header.frame_id                              [camera]
//   stamp      (string)  now: ROS time when published; media: start time + media time [now]
//   reliable   (bool)    reliable QoS instead of best effort. Large images over the
//                        network can be dropped with best effort                  [false]
//
// Also a component (viplyr_ros::VideoPublisher): loaded into the same container as its
// subscribers with intra-process communication, images are handed over in memory.
//
// Topics: image_raw (sensor_msgs/Image), camera_info (sensor_msgs/CameraInfo, size only)
// Control: ~/set_playing (std_srvs/SetBool), ~/seek (std_msgs/Float64, seconds),
//          ~/set_recording (std_srvs/SetBool): record the published video to record_path
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_components/register_node_macro.hpp>
#include <sensor_msgs/msg/camera_info.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <std_msgs/msg/float64.hpp>
#include <std_srvs/srv/set_bool.hpp>

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

#include "viplyr/Logging.h"
#include "viplyr/Player.h"
#include "viplyr/VideoRecorder.h"

namespace viplyr_ros {

namespace {

viplyr::HwAccel parseHwAccel(const std::string &name) {
    if (name == "none" || name == "cpu") return viplyr::HwAccel::None;
    if (name == "vaapi") return viplyr::HwAccel::Vaapi;
    if (name == "cuda") return viplyr::HwAccel::Cuda;
    return viplyr::HwAccel::Auto;
}

}  // namespace

class VideoPublisher : public rclcpp::Node, public viplyr::IVideoSink {
public:
    explicit VideoPublisher(const rclcpp::NodeOptions &options = rclcpp::NodeOptions())
        : rclcpp::Node("viplyr_video_publisher", options) {
        const std::string url = declare_parameter<std::string>("url", "");
        const std::string hw = declare_parameter<std::string>("hw", "auto");
        encoding_ = declare_parameter<std::string>("encoding", "bgr8");
        width_ = static_cast<int>(declare_parameter<int64_t>("width", 0));
        height_ = static_cast<int>(declare_parameter<int64_t>("height", 0));
        const double rate = declare_parameter<double>("rate", 1.0);
        loop_ = declare_parameter<bool>("loop", true);
        frameId_ = declare_parameter<std::string>("frame_id", "camera");
        stampWithMediaTime_ = declare_parameter<std::string>("stamp", "now") == "media";
        const bool reliable = declare_parameter<bool>("reliable", false);
        const std::string inputFormat = declare_parameter<std::string>("input_format", "");
        const auto formatOptions = declare_parameter<std::vector<std::string>>("format_options", std::vector<std::string>());
        const bool lowLatency = declare_parameter<bool>("low_latency", false);
        recordPath_ = declare_parameter<std::string>("record_path", "");
        viplyr::setLogLevel(viplyr::LogLevel::Warning);

        if (encoding_ == "bgr8") pixelFormat_ = viplyr::PixelFormat::Bgr24;
        else if (encoding_ == "rgb8") pixelFormat_ = viplyr::PixelFormat::Rgba;  // alpha dropped below
        else if (encoding_ == "mono8") pixelFormat_ = viplyr::PixelFormat::Gray8;
        else throw std::invalid_argument("encoding must be bgr8, rgb8 or mono8");
        if (url.empty()) throw std::invalid_argument("parameter 'url' is required");

        // Sensor data: keep only the newest frames, drop what a slow subscriber cannot take.
        rclcpp::QoS qos = rclcpp::SensorDataQoS();
        if (reliable) qos.reliable();
        imagePublisher_ = create_publisher<sensor_msgs::msg::Image>("image_raw", qos);
        infoPublisher_ = create_publisher<sensor_msgs::msg::CameraInfo>("camera_info", qos);
        playingService_ = create_service<std_srvs::srv::SetBool>(
            "~/set_playing", [this](const std::shared_ptr<std_srvs::srv::SetBool::Request> request,
                                    std::shared_ptr<std_srvs::srv::SetBool::Response> response) {
                if (request->data) player_.play();
                else player_.pause();
                response->success = true;
            });
        seekSubscription_ = create_subscription<std_msgs::msg::Float64>(
            "~/seek", 10, [this](const std_msgs::msg::Float64 &seconds) { player_.seek(seconds.data); });
        recordingService_ = create_service<std_srvs::srv::SetBool>(
            "~/set_recording", [this](const std::shared_ptr<std_srvs::srv::SetBool::Request> request,
                                      std::shared_ptr<std_srvs::srv::SetBool::Response> response) {
                setRecording(request->data, *response);
            });

        player_.addVideoSink(this);
        player_.addVideoSink(&recorder_);  // idle until ~/set_recording
        player_.onEnded = [this] { ended_ = true; };  // handled on the node's thread
        player_.onError = [this](const std::string &message) { RCLCPP_ERROR(get_logger(), "%s", message.c_str()); };

        viplyr::OpenOptions openOptions;
        openOptions.hwAccel = parseHwAccel(hw);
        openOptions.inputFormat = inputFormat;
        openOptions.lowLatency = lowLatency;
        for (const std::string &option : formatOptions) {
            const size_t equals = option.find('=');
            if (equals == std::string::npos) throw std::invalid_argument("format_options entries are key=value: " + option);
            openOptions.formatOptions[option.substr(0, equals)] = option.substr(equals + 1);
        }
        std::string error;
        if (!player_.open(url, openOptions, &error)) throw std::runtime_error("cannot open " + url + ": " + error);
        player_.setRate(rate);

        const viplyr::MediaInfo &info = player_.info();
        RCLCPP_INFO(get_logger(), "%s: %dx%d %.2f fps %s, decoder %s", url.c_str(), info.width, info.height,
                    info.frameRate, info.videoCodec.c_str(),
                    info.hardwareDecoder.empty() ? "cpu" : info.hardwareDecoder.c_str());

        startTime_ = now();
        endTimer_ = create_wall_timer(std::chrono::milliseconds(50), [this] { handleEnd(); });
        statsTimer_ = create_wall_timer(std::chrono::seconds(10), [this] { reportSkippedFrames(); });
        publishThread_ = std::thread([this] { publishLoop(); });
        player_.play();
    }

    ~VideoPublisher() override {
        player_.close();
        recorder_.stop();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        frameReady_.notify_one();
        publishThread_.join();
    }

    // viplyr::IVideoSink, on the engine's present thread. Only hands the frame over:
    // converting and publishing (which can wait on the network) happen on publishThread_,
    // so a slow subscriber never holds up playback.
    void present(const viplyr::VideoFrame &frame) override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pendingFrame_.isValid()) ++skippedFrames_;  // the previous one was not sent in time
            pendingFrame_ = frame;
        }
        frameReady_.notify_one();
    }

private:
    void publishLoop() {
        for (;;) {
            viplyr::VideoFrame frame;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                frameReady_.wait(lock, [this] { return stopping_ || pendingFrame_.isValid(); });
                if (stopping_) return;
                frame = std::move(pendingFrame_);
                pendingFrame_ = viplyr::VideoFrame();
            }
            publish(frame);
        }
    }

    void publish(const viplyr::VideoFrame &frame) {
        const viplyr::VideoFrame image = frame.convert(pixelFormat_, width_, height_);
        if (!image.isValid()) return;

        auto message = std::make_unique<sensor_msgs::msg::Image>();
        message->header.frame_id = frameId_;
        message->header.stamp = stampWithMediaTime_ ? startTime_ + rclcpp::Duration::from_seconds(frame.pts()) : now();
        message->width = static_cast<uint32_t>(image.width());
        message->height = static_cast<uint32_t>(image.height());
        message->encoding = encoding_;
        message->is_bigendian = false;
        const int channels = encoding_ == "mono8" ? 1 : 3;
        message->step = message->width * channels;
        message->data.resize(static_cast<size_t>(message->step) * message->height);

        const uint8_t *source = image.plane(0);
        uint8_t *target = message->data.data();
        for (uint32_t row = 0; row < message->height; ++row) {
            const uint8_t *in = source + static_cast<size_t>(row) * image.stride(0);
            uint8_t *out = target + static_cast<size_t>(row) * message->step;
            if (pixelFormat_ == viplyr::PixelFormat::Rgba) {
                for (uint32_t x = 0; x < message->width; ++x) std::memcpy(out + 3 * x, in + 4 * x, 3);
            } else {
                std::memcpy(out, in, message->step);
            }
        }

        sensor_msgs::msg::CameraInfo info;
        info.header = message->header;
        info.width = message->width;
        info.height = message->height;
        imagePublisher_->publish(std::move(message));
        infoPublisher_->publish(info);
    }

    void setRecording(bool start, std_srvs::srv::SetBool::Response &response) {
        std::string error;
        if (!start) {
            response.success = recorder_.stop(&error);
            response.message = response.success ? std::to_string(recorder_.framesWritten()) + " frames written to " + recordPath_
                                                : error;
            return;
        }
        if (recordPath_.empty()) {
            response.success = false;
            response.message = "set the record_path parameter first";
            return;
        }
        viplyr::RecorderOptions options;
        options.width = width_;
        options.height = height_;
        if (player_.info().frameRate > 0.0) options.frameRate = player_.info().frameRate;
        response.success = recorder_.start(recordPath_, options, &error);
        response.message = response.success ? "recording to " + recordPath_ : error;
    }

    void reportSkippedFrames() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (skippedFrames_ > 0) {
            RCLCPP_WARN(get_logger(), "%d frames not published in the last 10 s: publishing is slower than playback",
                        skippedFrames_);
        }
        skippedFrames_ = 0;
    }

    void handleEnd() {
        if (!ended_.exchange(false)) return;
        if (!loop_) {
            RCLCPP_INFO(get_logger(), "end of video");
            return;
        }
        startTime_ = now();
        player_.seek(0.0);
        player_.play();
    }

    viplyr::Player player_;
    viplyr::VideoRecorder recorder_;
    std::string recordPath_;
    viplyr::PixelFormat pixelFormat_ = viplyr::PixelFormat::Bgr24;
    std::string encoding_;
    std::string frameId_;
    int width_ = 0;
    int height_ = 0;
    bool loop_ = true;
    bool stampWithMediaTime_ = false;
    rclcpp::Time startTime_;
    std::atomic<bool> ended_{false};

    // Latest frame waiting for publishThread_; a newer one replaces it.
    std::mutex mutex_;
    std::condition_variable frameReady_;
    viplyr::VideoFrame pendingFrame_;
    int skippedFrames_ = 0;
    bool stopping_ = false;
    std::thread publishThread_;

    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr imagePublisher_;
    rclcpp::Publisher<sensor_msgs::msg::CameraInfo>::SharedPtr infoPublisher_;
    rclcpp::Service<std_srvs::srv::SetBool>::SharedPtr playingService_;
    rclcpp::Service<std_srvs::srv::SetBool>::SharedPtr recordingService_;
    rclcpp::Subscription<std_msgs::msg::Float64>::SharedPtr seekSubscription_;
    rclcpp::TimerBase::SharedPtr endTimer_;
    rclcpp::TimerBase::SharedPtr statsTimer_;
};

}  // namespace viplyr_ros

RCLCPP_COMPONENTS_REGISTER_NODE(viplyr_ros::VideoPublisher)

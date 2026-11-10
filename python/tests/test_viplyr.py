"""Tests for the viplyr Python module. Usage: test_viplyr.py <test_clip.mp4> <test_tracks.mkv>"""
import sys
import tempfile
import threading
import time

import numpy as np
import viplyr

FRAME = 1.0 / 30.0
failures = 0


def check(condition, text):
    global failures
    if not condition:
        failures += 1
        print(f"  FAILED: {text}")


def test_frame_reader(clip):
    print("frame reader")
    reader = viplyr.FrameReader(clip, hw="none")
    info = reader.info
    check(info["width"] == 320 and info["height"] == 240, "size")
    check(abs(info["frame_rate"] - 30.0) < 0.01, "frame rate")
    check(info["hardware_decoder"] == "", "cpu decoder")

    times = []
    first = None
    for image, pts in reader:
        if first is None:
            first = image
        times.append(pts)
    check(len(times) == 90, f"90 frames, got {len(times)}")
    check(all(b > a for a, b in zip(times, times[1:])), "increasing timestamps")
    check(first.shape == (240, 320, 3) and first.dtype == np.uint8, f"bgr shape {first.shape}")
    check(first.flags["C_CONTIGUOUS"] or first.strides[1] == 3, "row layout")
    check(reader.read() is None, "None at the end")

    check(reader.seek(45.5 * FRAME), "accurate seek")
    frame = reader.read_frame()
    check(frame is not None and abs(frame.pts - 45 * FRAME) < 1e-3, "frame 45 after seek")
    gray = frame.to_numpy("gray")
    rgb = frame.to_numpy("rgb")
    small = frame.to_numpy("bgr", 160, 120)
    check(gray.shape == (240, 320), f"gray shape {gray.shape}")
    check(rgb.shape == (240, 320, 3), f"rgb shape {rgb.shape}")
    check(small.shape == (120, 160, 3), f"scaled shape {small.shape}")
    bgr = frame.to_numpy("bgr")
    check(np.abs(rgb[..., ::-1].astype(int) - bgr.astype(int)).max() <= 2, "rgb is bgr reversed")

    # Arrays outlive the reader and the frame: they own their pixels.
    kept = bgr.copy()
    del frame, reader
    check(np.array_equal(kept, bgr), "array still valid after the reader is gone")

    try:
        viplyr.FrameReader("/no/such/file.mp4")
        check(False, "missing file raises")
    except RuntimeError:
        pass


def test_tracks(tracks_clip):
    print("tracks")
    with viplyr.FrameReader(tracks_clip, hw="none") as reader:
        info = reader.info
        check(len(info["audio_tracks"]) == 2, "two audio tracks")
        check(len(info["subtitle_tracks"]) == 1 and info["subtitle_tracks"][0]["is_text"], "text subtitles")
        check(info["interlaced"], "interlaced")


def test_gpu(clip):
    for hw in ("vaapi", "cuda"):
        try:
            reader = viplyr.FrameReader(clip, hw=hw, keep_gpu=True)
        except RuntimeError:
            print(f"gpu ({hw}): cannot open, skipped")
            continue
        frame = reader.read_frame()
        if frame is None or not frame.is_gpu:
            print(f"gpu ({hw}): no such decoder, skipped")
            continue
        print(f"gpu ({hw})")
        check(frame.to_numpy().shape == (240, 320, 3), "gpu frame downloads to numpy")
        if hw == "cuda":
            interface = frame.luma.__cuda_array_interface__
            check(interface["shape"] == (240, 320) and interface["typestr"] == "|u1", "luma interface")
            check(interface["data"][0] != 0 and interface["strides"][0] >= 320, "luma pointer and pitch")
            chroma = frame.chroma.__cuda_array_interface__
            check(chroma["shape"] == (120, 160, 2), "chroma interface")
        else:
            try:
                frame.luma
                check(False, "luma of a VAAPI frame raises")
            except RuntimeError:
                pass


def test_player(clip):
    print("player")
    player = viplyr.Player()
    received = []
    lock = threading.Lock()

    def on_frame(frame):
        with lock:
            received.append((time.monotonic(), frame.pts, frame.to_numpy("gray").shape))

    player.on_frame = on_frame
    player.open(clip, hw="none")
    check(player.state == "paused", f"paused after open, got {player.state}")
    check(abs(player.duration - 3.0) < 0.1, "duration")
    player.play()
    time.sleep(1.0)
    with lock:
        count = len(received)
    check(25 <= count <= 36, f"about 30 frames in one second, got {count}")
    check(all(shape == (240, 320) for _, _, shape in received), "frames convert in the callback")

    player.pause()
    player.seek(1.5)
    time.sleep(0.3)
    with lock:
        last_pts = received[-1][1]
    check(abs(last_pts - 1.5) < FRAME, f"seek shows 1.5 s, got {last_pts:.3f}")
    player.step_frame(1)
    time.sleep(0.2)
    with lock:
        check(abs(received[-1][1] - (1.5 + FRAME)) < FRAME / 2, "step forward")

    player.rate = 2.0
    check(player.rate == 2.0, "rate")
    player.play()
    deadline = time.monotonic() + 3.0
    while player.state != "ended" and time.monotonic() < deadline:
        time.sleep(0.05)
    check(player.state == "ended", f"ends, got {player.state}")
    player.close()


def test_recorder(clip, tmp):
    print("recorder")
    path = f"{tmp}/recorded.mp4"
    recorder = viplyr.Recorder()
    recorder.start(path, encoder="libx264", width=160, height=120)
    reader = viplyr.FrameReader(clip, hw="none")
    while (frame := reader.read_frame()) is not None:
        recorder.write(frame)
        while recorder.frames_written + 16 < frame.pts * 30:
            time.sleep(0.001)
    written = recorder.stop()
    check(written + recorder.dropped_frames == 90, f"90 frames recorded, got {written}")
    check(recorder.encoder == "libx264", f"encoder {recorder.encoder}")
    info = viplyr.FrameReader(path).info
    check((info["width"], info["height"]) == (160, 120), "recorded size")
    check(abs(info["duration"] - 3.0) < 0.1, f"recorded duration {info['duration']}")

    # A player records what it plays.
    player = viplyr.Player()
    player.open(clip, hw="none")
    played = f"{tmp}/played.mkv"
    player.start_recording(played, encoder="libx264")
    player.rate = 4.0
    player.play()
    deadline = time.monotonic() + 3.0
    while player.state != "ended" and time.monotonic() < deadline:
        time.sleep(0.05)
    count = player.stop_recording()
    check(count >= 80, f"player recording has the frames, got {count}")
    check(viplyr.FrameReader(played).info["width"] == 320, "player recording size")
    player.close()


def test_live_source():
    print("live source")
    with viplyr.FrameReader("testsrc2=size=160x120:rate=30", input_format="lavfi", low_latency=True) as reader:
        image, pts = reader.read()
        check(image.shape == (120, 160, 3), "frame from a device")
    try:
        viplyr.FrameReader("x", input_format="no_such_device")
        check(False, "unknown input format raises")
    except RuntimeError as error:
        check("Unknown input format" in str(error), "error text")


def main():
    clip, tracks_clip = sys.argv[1], sys.argv[2]
    test_frame_reader(clip)
    test_tracks(tracks_clip)
    test_gpu(clip)
    test_player(clip)
    with tempfile.TemporaryDirectory() as tmp:
        test_recorder(clip, tmp)
    test_live_source()
    print("ALL PASSED" if failures == 0 else f"{failures} FAILURE(S)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

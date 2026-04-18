# How To Test `ARProject.out` (MarkerlessAR_V2)

This project currently has no automated unit/integration tests. The steps below are the recommended manual checks for correctness and for performance/data logging.

## Prereqs

- Linux host with a camera available via V4L2 (typically `/dev/video0`), or a recorded video file.
- OpenGL + GLEW available (CMake will fail if not installed).
- A pattern image to detect (example: `AR_Application_Software/MarkerlessAR_V2/Artifacts/pattern.png`).

## Build

From the repo root:

```bash
cmake -S AR_Application_Software/MarkerlessAR_V2 -B AR_Application_Software/MarkerlessAR_V2/build
cmake --build AR_Application_Software/MarkerlessAR_V2/build -j 4 --target ARProject
```

Notes:
- Use `--target ARProject` to avoid running the repo’s `install_after_build` target.
- The binary will be at `AR_Application_Software/MarkerlessAR_V2/build/src/ARProject.out`.
- Ultra96 convenience script: `AR_Application_Software/MarkerlessAR_V2/Artifacts/build.sh` (run with `bash` or `chmod +x` first).

## Run (Camera)

```bash
cd AR_Application_Software/MarkerlessAR_V2
./build/src/ARProject.out Artifacts/pattern.png
```

Expected:
- If a display is available, a window opens and renders the camera feed.
- If `DISPLAY` is not set (headless SSH / no X server), the app runs headless (no GUI window).
- If the pattern is visible, the overlay locks to the detected pattern (see `OVERLAY_PIPELINE.md`).

Useful runtime env vars:

```bash
export AR_TARGET_FPS=30              # frame pacing (clamped 1..120)
export AR_OVERLAY_IMAGE=/abs/path.png

# Headless/GUI control
export AR_HEADLESS=1                 # force headless mode (no cv::namedWindow / no cv::waitKey)
export AR_GUI=1                      # force GUI (requires a working DISPLAY / X server)

# Optional: stream the rendered 2D result over Wi-Fi as MJPEG-over-HTTP
# (view from Windows by opening: http://<device-ip>:8080/)
export AR_MJPEG_PORT=8080            # 0/unset disables
export AR_MJPEG_QUALITY=80           # JPEG quality (1..100)
export AR_MJPEG_FPS=15               # encode FPS cap (0 = encode every frame)
```

## MJPEG Streaming (Windows Viewer)

This is optional and does not change behavior unless `AR_MJPEG_PORT` is set. The stream works in both GUI and headless modes (headless is automatic when `DISPLAY` is not set).

### Device/FPGA

Run the app with streaming enabled:

```bash
cd AR_Application_Software/MarkerlessAR_V2
export AR_MJPEG_PORT=8080
export AR_MJPEG_QUALITY=80
export AR_MJPEG_FPS=15
./build/src/ARProject.out Artifacts/pattern.png
```

Find the device IP address (use the Wi-Fi interface IP):

```bash
ip a
```

### Windows Laptop

Open a browser on the same network:

- `http://<device-ip>:8080/` (simple page that embeds the stream)
- `http://<device-ip>:8080/stream.mjpg` (raw MJPEG stream)

Troubleshooting:
- Verify connectivity: `ping <device-ip>`
- If it hangs, the port may be blocked (firewall) or already in use; try a different `AR_MJPEG_PORT`.

## Run (Recorded Video)

```bash
cd AR_Application_Software/MarkerlessAR_V2
./build/src/ARProject.out Artifacts/pattern.png /abs/path/to/video.mp4
```

Expected:
- If the video loads, the app processes it frame-by-frame like the live camera path.

## Build + Run With Performance/Data Collection (`DCOLLECTDA` / `COLLECTDA`)

This enables a 1Hz CSV log containing:
- FPS
- process CPU utilization (approx, computed from process CPU time / wall time)
- memory usage (VmRSS/VmSize)
- thread count
- frame-to-frame arrival delta
- total `processFrame(...)` time (detector + render)
- “preprocess” time (DMA/PL latency in this pipeline: TX start → RX frame complete)
- “arrival → preprocess finished” latency
- patterns found per second

Build with logging enabled:

```bash
cmake -S AR_Application_Software/MarkerlessAR_V2 -B AR_Application_Software/MarkerlessAR_V2/build_collectda -DDCOLLECTDA=ON
cmake --build AR_Application_Software/MarkerlessAR_V2/build_collectda -j 4 --target ARProject
```

Run and write the CSV to a known location:

```bash
cd AR_Application_Software/MarkerlessAR_V2
export AR_COLLECTDA_PATH=/tmp/ar_collectda.csv
./build_collectda/src/ARProject.out Artifacts/pattern.png
```

Logging/overhead tuning env vars (all optional):

```bash
export AR_COLLECTDA_INTERVAL_MS=1000   # default 1000; larger = fewer writes/less overhead
export AR_COLLECTDA_FLUSH=1            # default 1; set 0 to avoid fflush each row
export AR_COLLECTDA_PROC=1             # default 1; set 0 to skip /proc + CPU% sampling
export AR_COLLECTDA_BUFFER_KB=256      # default 256; larger = fewer syscalls
```

Watch the log:

```bash
tail -f /tmp/ar_collectda.csv
```

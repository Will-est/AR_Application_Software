# How To Test `ARProject.out` (MarkerlessAR_V2)

This project currently has no automated unit/integration tests. The steps below are the recommended manual checks for correctness and for performance logging.

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

## Run (Camera)

```bash
cd AR_Application_Software/MarkerlessAR_V2
./build/src/ARProject.out Artifacts/pattern.png
```

Expected:
- A window opens and renders the camera feed.
- If the pattern is visible, the overlay locks to the detected pattern (see `OVERLAY_PIPELINE.md`).

Useful runtime env vars:

```bash
export AR_TARGET_FPS=30              # frame pacing (clamped 1..120)
export AR_OVERLAY_IMAGE=/abs/path.png
```

## Run (Recorded Video)

```bash
cd AR_Application_Software/MarkerlessAR_V2
./build/src/ARProject.out Artifacts/pattern.png /abs/path/to/video.mp4
```

Expected:
- If the video loads, the app processes it frame-by-frame like the live camera path.

## Build + Run With Performance/Data Collection (`COLLECTDA`)

This enables a 1Hz CSV log containing:
- FPS
- process CPU utilization (approx, computed from process CPU time / wall time)
- memory usage (VmRSS/VmSize)
- thread count
- frame-to-frame arrival delta
- total `processFrame(...)` time
- grayscale+Gaussian time (preprocess)
- “arrival → preprocess finished” latency
- patterns found per second

Build with logging enabled:

```bash
cmake -S AR_Application_Software/MarkerlessAR_V2 -B AR_Application_Software/MarkerlessAR_V2/build_collectda -DCOLLECTDA=ON
cmake --build AR_Application_Software/MarkerlessAR_V2/build_collectda -j 4 --target ARProject
```

Run and write the CSV to a known location:

```bash
cd AR_Application_Software/MarkerlessAR_V2
export AR_COLLECTDA_PATH=/tmp/ar_collectda.csv
./build_collectda/src/ARProject.out Artifacts/pattern.png
```

Watch the log:

```bash
tail -f /tmp/ar_collectda.csv
```

Sanity checks:
- `fps` should roughly track `AR_TARGET_FPS` (or your camera’s real output rate).
- `preproc_avg_ms` should be stable (it measures grayscale + Gaussian blur inside `PatternDetector::findPattern`).
- `patterns_found_per_s` should increase when the pattern is in view.

## Troubleshooting

- **Camera doesn’t open**: verify `ls -la /dev/video*` and that your user has permission (or run with `sudo`).
- **High CPU**: lower `AR_TARGET_FPS`; also verify OpenGL isn’t using software rendering (llvmpipe).
- **No CSV output with `COLLECTDA`**:
  - Ensure you built with `-DCOLLECTDA=ON`.
  - Ensure `AR_COLLECTDA_PATH` is writable (or omit it to write `ARProject_collectda.csv` in the current directory).


# Headless AR Streaming

This document describes how to run `ARProject.out` without a local display or VNC session and stream the composited AR output over Wi-Fi to a Windows laptop.

## Goal

- Camera/video input stays on the FPGA board.
- Pattern detection and overlay compositing stay on the FPGA board.
- The board does not require VNC or a local X11 session to stay alive.
- The final composited frame is streamed over Wi-Fi.
- Windows displays that stream on the HDMI-connected LCD.

## What Was Added

- Headless-safe rendering:
  - If no `DISPLAY` or `WAYLAND_DISPLAY` is present, the app skips OpenCV window creation and keeps rendering frames in memory.
  - `cv::waitKey()` is only used when a real display window exists.
- Built-in MJPEG-over-HTTP streamer:
  - Optional and controlled by environment variables.
  - Streams the composited frame from `ARDrawingContext::getLastRenderedFrame()`.
- Restored normal runtime entry point:
  - `ARProject.out <pattern image> [video/image]`
- Preserved hexdump helper mode:
  - `ARProject.out --write-pattern-hexdump`

## Environment Variables

- `AR_HEADLESS=1`
  - Forces headless mode even if a stale display variable exists.
- `AR_STREAM_ENABLE=1`
  - Enables the MJPEG HTTP server.
- `AR_STREAM_PORT=5969`
  - TCP port for the stream.
- `AR_STREAM_BIND_IPV4=0.0.0.0`
  - Address to bind on the FPGA board.
- `AR_STREAM_JPEG_QUALITY=80`
  - JPEG quality from `30` to `100`.
- `AR_TARGET_FPS=30`
  - Frame pacing target.
- `AR_OVERLAY_IMAGE=/absolute/path/to/overlay.png`
  - Optional override for the overlay image.

## FPGA-Side Run Command

Run this on the FPGA board:

```bash
export AR_HEADLESS=1
export AR_STREAM_ENABLE=1
export AR_STREAM_PORT=5969
export AR_STREAM_BIND_IPV4=0.0.0.0
export AR_STREAM_JPEG_QUALITY=80
export AR_TARGET_FPS=20

./ARProject.out /absolute/path/to/pattern.png
```

If you are using a prerecorded video instead of the live camera:

```bash
./ARProject.out /absolute/path/to/pattern.png /absolute/path/to/input.mp4
```

When the server starts, the app prints a URL like:

```text
MJPEG streamer listening on http://0.0.0.0:5969/stream.mjpg
```

From another machine, replace `0.0.0.0` with the FPGA board IP address:

```text
http://<fpga-ip>:5969/stream.mjpg
```

## Windows-Side Viewing

Use any viewer that can display an MJPEG HTTP stream.

### Option 1: Browser

Open this in a browser on the Windows laptop:

```text
http://<fpga-ip>:5969/stream.mjpg
```

Then move that browser window to the HDMI-connected LCD and make it fullscreen.

### Option 2: VLC

1. Open VLC.
2. Open Network Stream.
3. Enter:

```text
http://<fpga-ip>:5969/stream.mjpg
```

4. Move VLC to the HDMI-connected LCD.
5. Press fullscreen.

VLC is usually more stable than a browser for long sessions.

## Recommended Setup For Your Use Case

For the exact workflow you described:

1. Camera feeds frames into the FPGA board.
2. `ARProject.out` processes each frame and composites the overlay.
3. The board publishes the composited result as MJPEG over HTTP.
4. The Windows laptop connects to `http://<fpga-ip>:5969/stream.mjpg`.
5. Windows shows that stream on the external LCD connected over HDMI.

## Windows Launcher Script

This repo now includes a Windows-side launcher:

- [OpenFpgaStream.ps1](/home/shreeya607/seniordesign/AR_Application_Software/MarkerlessAR_V2/tools/windows/OpenFpgaStream.ps1)
- [OpenFpgaStream.bat](/home/shreeya607/seniordesign/AR_Application_Software/MarkerlessAR_V2/tools/windows/OpenFpgaStream.bat)

From Windows, you can run:

```powershell
powershell -ExecutionPolicy Bypass -File .\OpenFpgaStream.ps1 -FpgaIp <fpga-ip>
```

Or:

```bat
OpenFpgaStream.bat <fpga-ip>
```

The script defaults to port `5969` and tries players in this order:

- Edge app window, fullscreen
- Chrome app window, fullscreen
- VLC fullscreen
- Default browser

You can force a player:

```powershell
powershell -ExecutionPolicy Bypass -File .\OpenFpgaStream.ps1 -FpgaIp <fpga-ip> -Player edge
```

Valid player values:

- `auto`
- `edge`
- `chrome`
- `vlc`
- `browser`

This avoids any dependency on VNC for the runtime path.

## Notes On Performance

- MJPEG is the easiest option to bring up and debug.
- MJPEG uses more bandwidth than H.264.
- Lower `AR_TARGET_FPS` and lower `AR_STREAM_JPEG_QUALITY` reduce Wi-Fi load.
- A good first pass is:
  - `AR_TARGET_FPS=15`
  - `AR_STREAM_JPEG_QUALITY=70`
- If latency or bandwidth becomes a problem later, the next upgrade path is H.264 or RTSP.

## Limitations

- The current built-in streamer is MJPEG only.
- The board still does all compositing locally; Windows is only a viewer.
- The root path `/` and `/stream.mjpg` both serve the MJPEG stream.
- If the Windows client disconnects, the FPGA app keeps running.

## Troubleshooting

### App crashes when VNC disconnects

Set:

```bash
export AR_HEADLESS=1
```

That prevents the runtime from depending on a GUI session.

### Stream does not open from Windows

Check:

- The FPGA board IP address is reachable.
- Port `5969` is open.
- The board is listening on `0.0.0.0` or the correct interface IP.
- Windows firewall is not blocking the client application.

### Stream is too slow

Try:

```bash
export AR_TARGET_FPS=15
export AR_STREAM_JPEG_QUALITY=65
```

### Need pattern hexdumps

That mode is still available:

```bash
./ARProject.out --write-pattern-hexdump
```

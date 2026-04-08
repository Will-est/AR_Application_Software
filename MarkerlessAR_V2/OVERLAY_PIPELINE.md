# OpenGL ES Overlay Pipeline Notes

This document describes the rendering changes made for OpenGL ES compatibility and image overlay support.

## Goals

- Keep existing pattern detection logic untouched.
- Render camera frames through a programmable OpenGL ES path.
- Add image overlay support.
- Lock overlay to detected pattern corners when a pattern is visible.

## Runtime Pipeline

1. `main.cpp` creates `ARPipeline` and `ARDrawingContext`.
2. `configureImageOverlay(...)` loads overlay image from:
   - `AR_OVERLAY_IMAGE` environment variable, or
   - fallback `/home/unc-design/augmented-reality-glasses/AR_Application_Software/MarkerlessAR_V2/Artifacts/overlay.png`.
3. `processFrame(...)` runs existing detector:
   - `drawingCtx.isPatternPresent = pipeline.processFrame(cameraFrame);`
   - `drawingCtx.setPatternOverlayState(...)` with `pipeline.getPatternInfo().points2d`.
4. `processFrame(...)` sends camera image to renderer with `drawingCtx.updateBackground(img)`.
5. OpenCV invokes `ARDrawingContext::draw()` as the OpenGL callback.
6. `draw()` composites overlay (pattern-locked or fallback corner mode), uploads final frame to a GL texture, then draws textured quad.

## CPU vs GPU Responsibilities

- GPU (OpenGL ES):
  - Shader program compile/link.
  - Camera texture upload (`glTexImage2D`/`glTexSubImage2D`).
  - Screen draw pass (`glDrawArrays` on full-screen quad).
- CPU (OpenCV):
  - Optional overlay compositing:
    - Corner-pinned mode (`CompositeOverlayOnFrame`).
    - Pattern-locked warp mode (`CompositeOverlayOnPattern` via homography + `warpPerspective`).

## Key Source Changes

- `src/ARDrawingContext.hpp`
  - Added:
    - `setOverlayImage(...)`
    - `setOverlayEnabled(...)`
    - `setPatternOverlayState(...)`
  - Added overlay and pattern state members.

- `src/ARDrawingContext.cpp`
  - Added overlay compositing helpers:
    - `CompositeOverlayOnFrame(...)`
    - `CompositeOverlayOnPattern(...)`
  - Updated `draw()`:
    - Pattern-locked overlay when pattern corners are available.
    - Fallback corner overlay when pattern not visible.
    - Upload composited frame to GLES texture and render.

- `src/main.cpp`
  - Added `configureImageOverlay(...)`.
  - Calls `configureImageOverlay(...)` in both video and image modes.
  - Sends detector 2D corners to renderer via `setPatternOverlayState(...)`.
  - Added frame pacing with `AR_TARGET_FPS` (default `30`, clamped `1..120`) to reduce busy-loop CPU usage.

## How To Use

1. Place an overlay image (PNG recommended for alpha).
2. Optional: set custom path:

```bash
export AR_OVERLAY_IMAGE=/absolute/path/to/overlay.png
```

3. Run app as usual with required pattern image argument.

4. Optional: tune render pacing for CPU usage:

```bash
export AR_TARGET_FPS=30
```

Lower values reduce CPU usage further (for example `24`), while higher values increase smoothness and CPU load.

## Behavior

- Pattern visible:
  - Overlay is homography-warped and locked to detected pattern corners.
- Pattern not visible:
  - Overlay falls back to a top-left corner composited badge.

## Important Notes

- Pattern detection/training/matching code was not modified.
- Old fixed-function OpenGL rendering remains disabled in this path.
- OBJ is currently loaded but not rendered in the GLES overlay path.
- If logs show `GL_RENDERER: llvmpipe`, rendering is software-based and CPU usage will be high; frame pacing helps, but hardware GL acceleration has the largest impact.
- For headless operation and Wi-Fi display streaming, see `HEADLESS_STREAMING.md`.

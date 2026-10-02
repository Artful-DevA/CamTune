# Camera Adjust (LinuxCameraAdjust)

A lightweight, native Linux webcam control panel and virtual camera for video
calls — a focused replacement for GUVCView/Webcamoid on Ubuntu and Fedora.

```
Launch → webcam appears → adjust framing and color → enable virtual camera
       → pick “Camera Adjust” in Zoom/Teams/Meet/Discord → leave it running for hours
```

Camera Adjust opens the physical webcam, applies hardware controls, color
adjustments and framing (smooth zoom/pan, crop, rotation, mirror), and writes
the result to a **v4l2loopback** virtual camera that every Linux application can
select. It is not a recorder or streaming tool and does not try to replace OBS.

## Features

- **Camera (hardware) controls** – every V4L2/UVC control the camera exposes,
  generated automatically: exposure, focus, gain, white balance, brightness,
  contrast, saturation, sharpness, anti-flicker, backlight compensation, …
  Auto/manual modes are respected (manual controls are disabled while their
  automatic mode is on). Malformed controls reported by buggy firmware are
  skipped instead of crashing.
- **Color (software)** – brightness, contrast, saturation, gamma, sharpness,
  warmth and tint, applied through lookup tables fused into the resampling pass.
- **Framing** – smooth digital zoom (up to 8×), pan X/Y, crop per edge,
  quarter-turn rotation plus fine “level” rotation, horizontal mirror, vertical
  flip, and fill / fit / stretch aspect modes. Changes are animated so zoom and
  pan glide instead of jumping. Scroll on the preview to zoom, drag to pan,
  double-click to reset.
- **Virtual camera** – 640×360 up to 2560×1440, 15–60 fps (720p30, 1080p30 and
  1080p60 included), I420 (zero-conversion) or YUYV output.
- **Presets** – save (File → Presets → Save, Ctrl+S) / load / rename / delete / reorder; each preset can include framing,
  color, hardware controls, output resolution/frame rate and effects. Ctrl+1…9
  in the window, smooth animated transitions between framings. Defaults:
  Ctrl+1 Normal, Ctrl+2 Zoom 1.25×, Ctrl+3 Close-up, Ctrl+4 Desk.
- **Background blur that follows you** – a small person-detection model
  (Google MediaPipe selfie segmentation, ~230 KB, Apache-2.0) is built into
  the app and runs on the CPU with a tiny built-in inference engine: no
  internet, no extra libraries. The mask is refined against the full-resolution
  picture so hair and shoulders stay crisp, and the background is blurred
  without your outline bleeding into it. Replace the background with blur, a
  solid color or an image.
- **Effects without AI** – blur the entire image, blur drawn regions, keep a
  drawn ellipse/rectangle sharp, a fixed mask image, or chroma key (with color
  picker). Depth-camera masking is not implemented.
- **Desktop integration** – system tray, start minimized, start on login,
  remembers the last camera, settings and preset, global shortcuts through the
  XDG desktop portal, a command line and a D-Bus interface for automation
  (hotkey daemons, Stream Deck scripts).
- **Reliability** – survives unplug/replug (identifies the camera by its
  `/dev/v4l/by-id` link, so a new `/dev/videoN` is fine), “camera busy”,
  stalled streams, suspend/resume, switching cameras and virtual-camera
  consumers coming and going. While the camera is unavailable the virtual
  camera shows a clear placeholder instead of freezing or disappearing.

## Architecture

```
            ┌────────────── Qt GUI thread ───────────────┐
            │ MainWindow · presets · tray · D-Bus · CLI  │
            └───────▲───────────────────────┬────────────┘
     signals (queued)│                      │ non-blocking setters
┌────────────────────┴──────────────────────▼──────────────────────────────┐
│ Engine (Qt-free C++)                                                       │
│                                                                            │
│  cam-capture ──latest──▶ cam-process ──latest──▶ cam-output ──▶ v4l2loopback
│  V4L2 mmap, MJPEG     │  resample+color+       │  write() as soon as a    │
│  decode, reconnect,   │  framing in one pass,  │  frame arrives; fps      │
│  watchdog             │  effects, animation    │  thinning; placeholder   │
│                       └──latest──▶ preview (GPU, YUV→RGB in shader)       │
│  cam-controls: UVC controls on their own fd, coalesced                     │
└────────────────────────────────────────────────────────────────────────────┘
```

Design choices that keep latency low and stable over multi-hour calls:

- **Single-slot “latest wins” mailboxes between stages.** A slow stage drops
  stale frames; nothing ever queues, so latency cannot creep up over time.
  The capture thread drains all ready V4L2 buffers and keeps only the newest.
- **Zero-copy capture.** Raw frames (YUYV/NV12/I420) are processed straight out
  of the V4L2 mmap buffers; packed formats are sampled in place (no
  deinterleave pass). Buffers return to the driver when the last reference drops.
- **No RGB round trip.** MJPEG is decoded by libjpeg-turbo directly to planar
  YUV, then a single fused pass does crop/zoom/pan/rotation (bilinear, fixed
  point, multi-threaded), full→limited range conversion and all color
  adjustments via LUTs, writing contiguous I420 that is handed to `write()`
  as-is. The preview uploads the three planes as textures and converts on the GPU.
- **Pooled frames.** Steady-state operation allocates nothing; long sessions
  don't fragment or grow memory (see the soak test).
- **Graceful degradation.** When the visible region is much larger than the
  output (e.g. 1080p camera → 540p output), MJPEG is decoded at ½ or ¼ scale for
  free. If processing can't keep up, the scale threshold is relaxed and frames
  are dropped rather than delayed.
- **The UI never blocks on devices.** All V4L2 I/O runs on worker threads;
  slow UVC control transfers run on a dedicated thread with their own fd so
  they cannot stall capture either.
- **Auto mode selection** picks the smallest camera mode covering the output
  at the requested frame rate, preferring uncompressed formats when they can
  sustain it (MJPEG otherwise — e.g. most webcams only do 1080p30 as MJPEG).

Typical per-frame cost on a 4-core laptop CPU using 2 worker threads
(`build/camcore_bench`): MJPEG 1080p decode ≈ 2 ms, 1080p→720p zoom+color
≈ 2 ms, YUYV 1080p zoom+color ≈ 5 ms, whole-image blur adds ≈ 3 ms at 1080p.
Person-detection blur adds roughly 10–15 ms per 720p frame (the network runs
at most ~20×/s; edges are refined on every frame), so prefer 720p output
when using it on slower CPUs.

Source layout:

| Path | Contents |
| --- | --- |
| `src/core` | frames & pools, mailboxes, thread pool, resampler/color, framing math & animation, effects, MJPEG decoder |
| `src/v4l2` | device enumeration, mode selection, mmap capture, controls, v4l2loopback writer |
| `src/pipeline` | capture / processing / output / control worker threads and the `Engine` facade |
| `src/app` | Qt model (`CameraController`), presets, settings, D-Bus, portal shortcuts, autostart, suspend handling |
| `src/ui` | main window, GPU preview, panels |
| `src/core/nn` | minimal CPU inference engine for the embedded segmentation model |
| `tools` | model converter (`.tflite` → `.camnn`), only needed to regenerate the model |
| `tests` | unit tests, soak test, benchmark |

## Building

Requirements: CMake ≥ 3.16, a C++17 compiler, Qt ≥ 6.2 (Widgets, OpenGL,
OpenGLWidgets, DBus) and libjpeg-turbo.

**Ubuntu 22.04+ / Debian 12+**

```sh
sudo apt install build-essential cmake pkg-config qt6-base-dev libqt6opengl6-dev \
                 libturbojpeg0-dev libgl-dev v4l2loopback-dkms
```

**Fedora 38+**

```sh
sudo dnf install gcc-c++ cmake pkgconf qt6-qtbase-devel turbojpeg-devel mesa-libGL-devel
# v4l2loopback comes from RPM Fusion (free):
sudo dnf install v4l2loopback
```

**Build, test, install**

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
sudo cmake --install build
```

`-DCAMADJUST_NATIVE=ON` adds `-march=native` for a few percent more speed on
the build machine.

## Virtual camera setup

The virtual camera is provided by the `v4l2loopback` kernel module. Use
**Output → Set up virtual camera…** (asks for your password via polkit) or run
the helper yourself:

```sh
sudo scripts/camadjust-setup-v4l2loopback            # or --install to install the package first
```

It loads the module now and at every boot with:

```
devices=1 video_nr=42 card_label="Camera Adjust" exclusive_caps=1 max_buffers=2
```

- `exclusive_caps=1` is required for Chromium/WebRTC based apps (Google Meet,
  Teams on the web, Discord) to list the device.
- `max_buffers=2` keeps the loopback queue — and thus latency — short.

With Secure Boot, DKMS (Ubuntu) / akmods (Fedora) need a signing key enrolled
via `mokutil`; the package installers usually walk you through it.

## Using it in a call

1. Start Camera Adjust; your webcam appears in the preview.
2. Adjust framing and color, then switch **Virtual camera** on.
3. In Zoom/Teams/Meet/Discord/OBS/the browser choose **Camera Adjust** as the camera.

Notes:

- Leave Camera Adjust running (closing the window keeps it in the tray). With
  *Settings → Start automatically on login* the virtual camera is always there.
- With `exclusive_caps=1` the virtual camera is only advertised while Camera
  Adjust is sending to it. If a call app was started first, re-open its camera
  menu (or restart its video) after enabling the virtual camera.
- Call apps may still list the physical camera too; pick the virtual one. If a
  call app has grabbed the physical camera, Camera Adjust shows “in use by
  another application” and takes it over automatically once it is released.
- Most call apps mirror only your *self-view*; others see the picture as sent.
  Use **Mirror** only if you want everyone to see a flipped image.
- Changing the output resolution while an app is using the virtual camera
  keeps the current size (the app keeps working). To switch, stop the camera in
  that app, then turn the virtual camera off and on.
- Flatpak/Snap apps need device access (`--device=all` for Flatpak).

## Command line and automation

```sh
camadjust                      # start, or show the running instance
camadjust --minimized          # start in the tray
camadjust --preset 2           # by shortcut number, position or name
camadjust --preset "Close-up"
camadjust --zoom 1.5 | --zoom-in | --zoom-out | --reset-framing
camadjust --pan 0.2,-0.3       # each -1 … 1
camadjust --virtual-camera on|off|toggle
camadjust --list-presets | --status | --show | --quit
```

Commands are sent over D-Bus to the running instance (service
`io.github.LinuxCameraAdjust`, object `/io/github/LinuxCameraAdjust`,
interface `io.github.LinuxCameraAdjust1`: `ApplyPreset`, `ListPresets`,
`SetZoom`, `AdjustZoom`, `SetPan`, `ResetFraming`, `SetVirtualCamera`,
`ToggleVirtualCamera`, `ShowWindow`, `Status`, `Quit`). If no instance is
running, the app is started and the command applied.

**Global hotkeys while Zoom has focus:** enable *Settings → Global shortcuts*
to register Ctrl+Alt+1…9 (presets), Ctrl+Alt+Up/Down (zoom), Ctrl+Alt+0 (reset)
and Ctrl+Alt+V (virtual camera) through the XDG desktop portal (KDE Plasma,
GNOME 48+, Hyprland…; your desktop lets you change the keys). On any desktop,
Wayland or X11, you can instead bind keys to the commands above in the
keyboard settings. Stream Deck/MIDI tools can call the same commands.

Settings live in `~/.config/LinuxCameraAdjust/` (`camadjust.conf`,
`presets.json`; presets are written atomically).

## Troubleshooting

| Symptom | Fix |
| --- | --- |
| “No virtual camera device found” | Load v4l2loopback (Output → Set up virtual camera…). |
| Browser/Discord doesn't list “Camera Adjust” | v4l2loopback must be loaded with `exclusive_caps=1`; virtual camera must be ON. |
| “Camera is in use by another application” | Another app opened the physical camera; switch it to the virtual camera. Camera Adjust retries automatically. |
| “Cannot open … permission denied” | Add yourself to the `video` group (`sudo usermod -aG video $USER`, then log in again). |
| Low frame rate in dim light | Disable *Allow frame rate drop* (exposure auto priority) or use manual exposure. |
| High CPU | Lower output resolution; use *Automatic* format; turn off effects/sharpening; pause the preview (Ctrl+P). |
| Camera only works through libcamera (some MIPI laptop cameras on Intel IPU6) | Not supported directly yet — such cameras need the vendor's v4l2 bridge or a PipeWire/libcamera source. |

## Testing

```sh
ctest --test-dir build --output-on-failure   # unit tests + 6 s soak test
build/camcore_soak 3600 60                   # one-hour soak at 1080p60
build/camcore_bench                          # per-frame processing costs
```

The soak test runs the full threaded engine on the built-in test pattern
while changing parameters constantly, and fails if latency accumulates, the
frame rate collapses or memory grows. Choose **Test pattern** as the camera to
try the application without a webcam.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).

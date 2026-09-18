# WinlatorXR backend (experimental)

KisakCOD VR can run on a standalone Meta Quest or Pico headset inside a
[WinlatorXR](https://github.com/WinlatorXR/WinlatorXR) container. WinlatorXR
runs Windows games through Wine and Box64. It exposes no OpenXR or SteamVR
runtime. Instead it offers [XrAPI](https://winlatorxr.github.io/xrapi.html):

- head and controller tracking arrives as UDP text on `127.0.0.1:7872`
- the game replies on `127.0.0.1:7278` with VR mode, stereo mode, FOV and
  haptics
- the game draws both eyes side by side into its own window, and WinlatorXR
  shows the left half to the left eye and the right half to the right eye

This backend is untested on a headset. Treat it as a starting point.

## How it works

- `KISAK_VR_BACKEND=winlatorxr` selects it. `auto` selects it when
  `Z:\tmp\xr\system` exists.
- At startup the game writes `Z:\tmp\xr\version` (default `0.5`) and
  `Z:\tmp\xr\vr`. It then waits up to 15 s for the first tracking packet.
  If none arrives, startup stops with an error.
- The packed D3D9 backbuffer is presented directly, with no D3D11 copy and no
  CPU readback. Before each `Present`, the frame's `HMD_SYNC` value is stamped
  as a 10x10 red block in the top-left corner. It uses the sync value of the
  pose the frame was rendered with.
- Each game frame waits up to 20 ms for a new `HMD_SYNC`. This paces the game
  to the headset, like SteamVR's `WaitGetPoses`.
- Poses are treated as OpenXR convention (+Y up, -Z forward, meters). With
  XrAPI 0.5, `HMD_ALTITUDE` is added so heights are measured from the floor.
- Buttons map with Touch semantics: A/X = primary, B/Y = secondary, left
  menu = menu, grip = squeeze. XrAPI has no thumbrest, so untouched default
  bindings get the same off-hand trigger mission selector as OpenVR.
- Haptics are sent as a vibration length in frames. Weaker effects become
  shorter pulses.
- In menus, the eye that holds the UI is copied into a centered panel in
  both window halves before `Present`. This matches what the compositor does
  on PC.

## Setup

1. Set Winlator's virtual screen resolution so each half matches the eye
   aspect. The game logs the recommended `r_customMode` at startup. For a
   Quest 3 FOV of about 99 x 103 degrees, each eye is about 0.92:1 (for
   example `1998x1080`).
2. Set `r_customMode` to that resolution and run fullscreen. The window must
   be exactly two eyes wide. If there is extra width, the side-by-side split
   stops matching the eyes.
3. Enable the XR API for the container in WinlatorXR and start the game with
   `KISAK_VR_BACKEND=winlatorxr`.
4. Check `main\console.log` for lines starting with `[VR][WINLATORXR]`.

## Settings

| Variable | Default | Purpose |
| --- | --- | --- |
| `KISAK_VR_WINLATORXR_API_VERSION` | `0.5` | Value written to `Z:\tmp\xr\version`. |
| `KISAK_VR_WINLATORXR_STARTUP_TIMEOUT_MS` | `15000` | How long to wait for the first packet. |
| `KISAK_VR_WINLATORXR_SYNC_WAIT_MS` | `20` | Longest wait for a new pose each frame. |
| `KISAK_VR_WINLATORXR_FOV_X` / `_FOV_Y` | headset | Render FOV in degrees. It is also sent to WinlatorXR. Lower values reduce the image area. |
| `KISAK_VR_WINLATORXR_FLIP_CONTROLLERS` | auto | `1` or `0` forces the 180-degree controller roll fix. Auto enables it on Quest 2, Quest Pro and Pico, as the HWXR reference mod does. |

## Known limitations

- No dedicated physical-scope panel. The window has no room for it, so
  scoped weapons use the normal view.
- Menus are copied from the eye they were drawn into and shown as a centered
  4:3 panel in both eyes. The panel is locked to the head, unlike the
  OpenXR/OpenVR comfort screen. It uses a temporary render target each menu
  frame.
- No palm pose. The off-hand glove uses its grip-frame fallback.
- Controller velocity is estimated from successive packets.
- Performance under Box64 and a mobile GPU is unknown. Start with a low
  `r_customMode` and reduced effects.
- Coordinate conventions, the controller roll fix and the `system` file's
  resolution line come from the spec and the HWXR reference mod. They are not
  verified on a headset yet.

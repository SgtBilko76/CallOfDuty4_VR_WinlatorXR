# WinlatorXR backend

KisakCOD VR runs on a standalone Meta Quest inside a
[WinlatorXR](https://github.com/WinlatorXR/WinlatorXR) container. WinlatorXR
runs Windows games through Wine and Box64 and exposes no OpenXR or SteamVR
runtime. Instead it offers [XrAPI](https://winlatorxr.github.io/xrapi.html):

- head and controller tracking arrives as UDP text on `127.0.0.1:7872`
- the game replies on `127.0.0.1:7278` with VR mode, stereo mode, FOV and
  haptics
- the game draws both eyes side by side into its own window, and WinlatorXR
  shows the left half to the left eye and the right half to the right eye

Tested on a Quest 3 with WinlatorXR `cats-27`: stereo rendering, head and
controller tracking, the physical weapon handling, menus and the campaign
intro all work. Pico and Quest 2 are untested.

## How it works

- `KISAK_VR_BACKEND=winlatorxr` selects it. `auto` selects it when
  WinlatorXR's `Z:\tmp\xr\system` file exists.
- At startup the game writes `Z:\tmp\xr\version` (default `0.5`) and
  `Z:\tmp\xr\vr`. It then waits up to 15 s for the first tracking packet.
  If none arrives, startup stops with an error.
- The packed D3D9 backbuffer is presented directly, with no D3D11 copy and no
  CPU readback. Before each `Present`, the frame's `HMD_SYNC` value is stamped
  as a 10x10 red block in the top-left corner. It uses the sync value of the
  pose the frame was rendered with.
- Each game frame waits up to 20 ms for a new `HMD_SYNC`. This paces the game
  to the headset, like SteamVR's `WaitGetPoses`.
- Poses are OpenXR convention (+Y up, -Z forward, meters). With XrAPI 0.5,
  `HMD_ALTITUDE` is added so heights are measured from the floor.
- XrAPI's controller rotation is rolled 180 degrees compared to an OpenXR aim
  pose, so the backend corrects it; its "upward" grip rotation is not an
  OpenXR grip and is not used. The free glove gets a palm pose derived from
  the corrected aim, rolled by `KISAK_VR_WINLATORXR_OFFHAND_ROLL`.
- Menus and cinematics have no compositor layer here, so they are drawn on a
  virtual screen 2 m in front of the player: menus from the eye they were
  drawn into (4:3), cinematics from the whole window (16:9). The screen is
  anchored when it appears and stays fixed in the room.
- WinlatorXR also turns controller buttons into desktop keyboard and mouse
  events. During VR gameplay those are ignored, so pressing A does not also
  strafe left. Menus, the console and Escape keep their keyboard input.
- The hardware-autoconfigure prompt is answered automatically in the
  container, where its dialog would open behind the game window.

## Setup

1. Set Winlator's virtual screen resolution so each half matches the eye
   aspect. The game logs the recommended `r_customMode` at startup. On a
   Quest 3, WinlatorXR reports an eye FOV of 108.9 x 103.4 degrees, so each
   eye is about 1.105:1 (`2388x1080`).
2. Copy the game folder with `KisakCOD-sp.exe`, the launcher and the original
   COD4 data to the headset, for example to `Download\CallOfDuty4`
   (drive `D:` inside the container).
3. Enable the XR API for the container, and use DXVK with the Turnip driver.
4. Run `Install-CoD4-Shortcut.bat` once inside the container to put the
   `CallOfDuty4-VR` shortcut on its desktop, or start
   `Launch-KisakCOD-VR-WinlatorXR.bat` directly.
5. Check `main\console.log` for lines starting with `[VR][WINLATORXR]`.

## Controls

XrAPI reports Touch-style buttons. It has no thumbrest, so the untouched
portable defaults take the safe layout, except that Pause stays on the Menu
button and Next weapon on Y, because XrAPI reports the left Menu button
separately from Y. Nothing is bound to the right stick click, which
WinlatorXR uses for its own menu.

| Action | Button |
| --- | --- |
| Fire | Right trigger |
| Use / pick up | X |
| Reload / eject magazine | A |
| Crouch / stance | B |
| Next weapon | Y |
| Pause | Menu |
| Support grip, magazines, grenades | Left grip |
| Grenade launcher | Right grip |
| Sprint | Left stick click |
| Melee | Left trigger + left stick up |
| Night vision, airstrike, C4 | Left trigger + left stick down / left / right |

Reloading and grenades are physical: eject with A, then take a fresh magazine
from the off-hand hip with the left grip and release it at the magazine well.
Grenades sit on the belt, frag on the off-hand side and tactical on the other.

## Settings

The launcher sets these; they work in any KisakCOD VR settings file.

| Variable | Default | Purpose |
| --- | --- | --- |
| `KISAK_VR_WINLATORXR_API_VERSION` | `0.5` | Value written to `Z:\tmp\xr\version`. |
| `KISAK_VR_WINLATORXR_STARTUP_TIMEOUT_MS` | `15000` | How long to wait for the first packet. |
| `KISAK_VR_WINLATORXR_SYNC_WAIT_MS` | `20` | Longest wait for a new pose each frame. |
| `KISAK_VR_WINLATORXR_FOV_X` / `_FOV_Y` | headset | Render FOV in degrees, also sent to WinlatorXR. |
| `KISAK_VR_WINLATORXR_OFFHAND_ROLL` | `180` | Roll of the free glove about the fingers, in degrees. |
| `KISAK_VR_WINLATORXR_GRIP_PITCH` | `0` | Pitch of the grip pose against the aim, in degrees. |
| `KISAK_VR_WINLATORXR_FLIP_CONTROLLERS` | auto | `1`/`0` forces the extra 180-degree controller flip. Auto enables it on Quest 2, Quest Pro and Pico, as the HWXR reference mod does. |

The launcher also lowers the HUD (`KISAK_VR_HUD_SAFE_Y=0.60`,
`KISAK_VR_COMPASS_INSET_Y=0`) for the tall lens, widens the magazine
insertion radius, and preloads shaders during level load.

## Performance

Measured on a Quest 3 at `2388x1080` (1194x1080 per eye): about 45 to 72
frames per second, median around 54, limited by Box64 and DXVK rather than by
the game's own frame work (6 to 7 ms per stereo frame). Higher resolutions
cost noticeably: `2864x1296` dropped the median to about 35.

## Known limitations

- No dedicated physical-scope panel. The window has no room for it, so
  scoped weapons use the normal view.
- The virtual screen for menus and videos is anchored when it appears; there
  is no manual recenter for it yet.
- Controller velocity is estimated from successive packets.
- Pico and Quest 2 handling of the extra controller flip is untested.
- WinlatorXR's own right-stick-click menu can switch the headset back to a
  flat window; click again to return to VR.

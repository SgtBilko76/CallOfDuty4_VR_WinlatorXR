# WinlatorXR launcher files

Copy these next to `KisakCOD-sp.exe` in the game folder on the headset, for
example `Download\CallOfDuty4`, which is `D:\CallOfDuty4` inside a container.
See [../../docs/WINLATORXR.md](../../docs/WINLATORXR.md) for the setup.

| File | Purpose |
| --- | --- |
| `Launch-KisakCOD-VR-WinlatorXR.bat` | Starts the game with the XrAPI backend. It loads `VR-Settings.bat` first, then applies the WinlatorXR-specific settings. |
| `CallOfDuty4-VR.desktop` | WinlatorXR shortcut. Copy it to `D:\Winlator`. |
| `Install-CoD4-Shortcut.bat` | Run once inside the container: copies the shortcut to the container's desktop and writes `shortcut-install.txt`. |

Adjust before use:

- `container_id` in the `.desktop` file must match your container (the file
  ships with `4`).
- `screenSize` there and `WINLATORXR_MODE` in the launcher must match each
  other and the container's screen size. The game logs the value that fits
  your headset at startup.
- The paths assume `D:\CallOfDuty4`.

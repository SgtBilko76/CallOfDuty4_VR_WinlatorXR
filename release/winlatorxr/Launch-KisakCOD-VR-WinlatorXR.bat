@echo off
rem KisakCOD VR - WinlatorXR test launcher (experimental, local test build).
rem Uses the release settings, then forces the XrAPI backend. The PC launcher's
rem configurator preflight is skipped: it probes OpenXR/SteamVR registry keys
rem that do not exist inside a WinlatorXR container.
setlocal EnableExtensions DisableDelayedExpansion
cd /d "%~dp0"

call "%~dp0VR-Settings.bat"
if errorlevel 1 exit /b 1

set "KISAK_VR_BACKEND=winlatorxr"
set "KISAK_VR_VERBOSE_DIAGNOSTICS=1"

rem WinlatorXR shows a tall 103-degree eye; keep the HUD inside the
rem visible part of the lens.
set "KISAK_VR_HUD_SAFE_Y=0.60"
rem Compass lower than the PC default of 48 (range -80 to 440).
set "KISAK_VR_COMPASS_INSET_Y=0"

rem Accept a magazine up to 12 inches (about 30 cm) from the well; the
rem maximum. The PC default is 6.5.
set "KISAK_VR_RELOAD_INSERT_RADIUS=12"

rem Two eyes side by side at the Quest 3 eye aspect (1.105): 2388x1080, 2626x1188,
rem 2864x1296 or 3182x1440. The container screen size must match.
rem the log recommends a value for your headset after the first start.
if not defined WINLATORXR_MODE set "WINLATORXR_MODE=2388x1080"

set "KISAK_VR_CRASH_DIR=%~dp0CrashDumps"
if not exist "%KISAK_VR_CRASH_DIR%\" mkdir "%KISAK_VR_CRASH_DIR%" >nul 2>&1

"%~dp0KisakCOD-sp.exe" ^
  +set logfile 2 +set r_preloadShaders 1 ^
  +set r_fullscreen 0 ^
  +set r_customMode %WINLATORXR_MODE% ^
  +set r_aaSamples 1 ^
  +set r_scaleViewport 1 ^
  +set r_resampleScene 0 ^
  +set r_vsync 0 ^
  +set com_maxfps 0 ^
  +set r_smp_backend 1 ^
  +set r_smp_worker 1 ^
  +set cg_gun_z 36 ^
  +set vr_leftHandModelOffsetForward %KISAK_VR_LEFT_HAND_OFFSET_FORWARD% ^
  +set vr_leftHandModelOffsetLeft %KISAK_VR_LEFT_HAND_OFFSET_LEFT% ^
  +set vr_leftHandModelOffsetUp %KISAK_VR_LEFT_HAND_OFFSET_UP% ^
  +set vr_leftHandModelPitch %KISAK_VR_LEFT_HAND_PITCH% ^
  +set vr_leftHandModelYaw %KISAK_VR_LEFT_HAND_YAW% ^
  +set vr_leftHandModelRoll %KISAK_VR_LEFT_HAND_ROLL% ^
  +set vr_leftHandGripRadius %KISAK_VR_LEFT_HAND_GRIP_RADIUS% ^
  +set compass %KISAK_VR_COMPASS_ENABLED% ^
  +set compassSize %KISAK_VR_COMPASS_SIZE% ^
  +set compassRotation %KISAK_VR_COMPASS_ROTATION% ^
  +set cg_drawCrosshair %KISAK_VR_CROSSHAIR% ^
  +set cg_subtitles %KISAK_VR_SUBTITLES% ^
  +set g_earthquakeEnable %KISAK_VR_CAMERA_SHAKE% ^
  +set cg_bobWeaponAmplitude %KISAK_VR_WEAPON_BOB_AMPLITUDE% ^
  +set mis_cheat %KISAK_VR_UNLOCK_MISSIONS% ^
  +set sm_enable 0 ^
  +set cg_drawPerformanceWarnings 0 ^
  +set developer 1 ^
  +set developer_script 0 ^
  +set uiscript_debug 0 ^
  +set con_errormessagetime 8 ^
  +set con_minicon 0 ^
  +set cg_drawFPS 1 ^
  +set com_statmon 0

endlocal & exit /b %ERRORLEVEL%

@echo off
rem Copies the CallOfDuty4-VR shortcut into this container's desktop.
copy /Y "D:\Winlator\CallOfDuty4-VR.desktop" "C:\users\xuser\Desktop\CallOfDuty4-VR.desktop" > "D:\CallOfDuty4\shortcut-install.txt" 2>&1
dir "C:\users\xuser\Desktop" >> "D:\CallOfDuty4\shortcut-install.txt" 2>&1

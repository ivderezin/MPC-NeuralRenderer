@echo off
echo.
echo.
title Restore MPC Neural Renderer default settings...
start /min reg delete "HKEY_CURRENT_USER\Software\MPC Neural Renderer" /f
echo    settings were reset to default
echo.
pause >NUL

@echo off
chcp 65001 >nul
cd /d "%~dp0"
py -3 -I "%~dp0scripts\quest\manage.py" %*
if errorlevel 1 pause

@echo off

set scriptdir=%~dp0
set scriptdir=%scriptdir:~0,-1%

set venvname=standalone
set venvscripts=%scriptdir%\%venvname%\Scripts

set venvpython=%venvscripts%\python.exe

cd /D "%scriptdir%"

python -m venv "%venvname%"

"%venvpython%" -m pip install --upgrade nuitka -r requirements-win32.txt

"%venvpython%" -m nuitka --standalone --deployment --windows-console-mode=attach --windows-icon-from-ico=nxdt.ico --enable-plugin=tk-inter nxdt_host.py

del /F /Q nxdt_host.7z
7z a nxdt_host.7z .\nxdt_host.dist\*

rmdir /s /q nxdt_host.build
rmdir /s /q nxdt_host.dist
rmdir /s /q standalone

pause

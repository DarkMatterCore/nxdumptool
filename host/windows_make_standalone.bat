@echo off

set scriptdir=%~dp0
set scriptdir=%scriptdir:~0,-1%

set venvname=installer
set venvscripts=%scriptdir%\%venvname%\Scripts

set venvpython=%venvscripts%\python.exe
set venvpyinstaller=%venvscripts%\pyinstaller.exe

cd /D "%scriptdir%"

python -m venv "%venvname%"

"%venvpython%" -m pip install --upgrade pyinstaller -r requirements-win32.txt

"%venvpyinstaller%" -y --clean --log-level WARN -F --add-binary "C:\Windows\System32\libusb0.dll;." -w -i nxdt.ico nxdt_host.py

move dist\nxdt_host.exe nxdt_host.exe

rmdir /s /q __pycache__
rmdir /s /q build
rmdir /s /q dist
rmdir /s /q installer
del nxdt_host.spec

pause

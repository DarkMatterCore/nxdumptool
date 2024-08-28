@echo off

set scriptdir=%~dp0
set scriptdir=%scriptdir:~0,-1%

set venvname=standalone
set venvscripts=%scriptdir%\%venvname%\Scripts

set venvpython=%venvscripts%\python.exe

cd /D "%scriptdir%"

python -m venv "%venvname%"

"%venvpython%" -m pip install --upgrade nuitka -r requirements-win32.txt

REM Useful command line arguments for Nuitka:

REM --standalone
REM Enable standalone mode for output. This allows you to transfer the created
REM binary to other machines without it using an existing Python installation.
REM It implies these option: "--follow-imports" and "--python-flag=no_site".

REM --assume-yes-for-downloads
REM Allow Nuitka to download external code if necessary, e.g. dependency
REM walker, ccache, and even gcc on Windows.

REM --deployment
REM Disable code aimed at making finding compatibility issues easier.
REM This will e.g. prevent execution with "-c" argument, which is often
REM used by code that attempts run a module, and causes a program to start
REM itself over and over potentially.

REM --windows-console-mode
REM Select console mode to use. Default mode is 'force' and creates a console
REM window unless the program was started from one. With 'disable' it doesn't
REM create or use a console at all. With 'attach' an existing console will be
REM used for outputs. Default is 'force'.

REM --windows-icon-from-ico=nxdt.ico
REM Add executable icon. Can be given multiple times for different resolutions
REM or files with multiple icons inside.

REM --enable-plugin=tk-inter
REM Enabled plugins. Must be plug-in names. Use '--plugin-list' to query the
REM full list and exit.

"%venvpython%" -m nuitka --standalone --assume-yes-for-downloads --deployment --windows-console-mode=attach --windows-icon-from-ico=nxdt.ico --enable-plugin=tk-inter nxdt_host.py

del /F /Q nxdt_host.7z
7z a nxdt_host.7z .\nxdt_host.dist\*

rmdir /s /q nxdt_host.build
rmdir /s /q nxdt_host.dist
rmdir /s /q standalone

pause

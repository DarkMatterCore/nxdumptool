@echo off

rem Get the directory of the script
set scriptdir=%~dp0
set scriptdir=%scriptdir:~0,-1%

rem Set up virtual environment variables
set venvname=standalone
set venvdir=%scriptdir%\%venvname%

rem Create a virtual environment
python -m venv "%venvdir%"

rem Activate the virtual environment
call "%venvdir%\Scripts\activate"

rem Install PyInstaller
pip install --upgrade pyinstaller

rem Install additional requirements
pip install -r requirements-win32.txt

rem Use PyInstaller to create a standalone executable
pyinstaller --onefile --noconsole --icon=nxdt.ico nxdt_host.py

rem Deactivate the virtual environment
call deactivate

rem Move the executable to the script directory
move "%scriptdir%\dist\nxdt_host.exe" "%scriptdir%"
timeout /t 1 /nobreak > nul

rem Clean up temporary files
rmdir /s /q "%scriptdir%\dist"
rmdir /s /q "%scriptdir%\build"
rmdir /s /q "%venvdir%"

rem Pause to keep the console window open for viewing output
pause

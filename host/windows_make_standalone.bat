pyinstaller -y --clean --log-level WARN -F --add-binary "C:\Windows\System32\libusb0.dll;." -w -i nxdt.ico nxdt_host.py

move dist\nxdt_host.exe nxdt_host.exe

rmdir /s /q __pycache__
rmdir /s /q build
rmdir /s /q dist
del nxdt_host.spec

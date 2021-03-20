rmdir /s /q nxdt_host
mkdir nxdt_host

cxfreeze nxdt_host.py -s --base-name=Win32GUI --target-dir=nxdt_host --icon=nxdt.ico

copy ..\README.md nxdt_host
copy ..\LICENSE.md nxdt_host

python -m zipfile -c nxdt_host.zip nxdt_host


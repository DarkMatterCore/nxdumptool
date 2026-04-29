#!/usr/bin/env bash

# Exit immediately if a command exits with a non-zero status
set -e

# Get the directory where the script is located and cd into it
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
cd "$SCRIPT_DIR"

VENV_NAME="standalone_mac"

echo "Creating virtual environment..."
python3 -m venv "$VENV_NAME"

# Activate virtual environment
source "$VENV_NAME/bin/activate"

echo "Installing dependencies..."
python3 -m pip install --upgrade pip
python3 -m pip install nuitka
python3 -m pip install -r requirements.txt

# Build with Nuitka
# 
# --macos-create-app-bundle: Packages the output as a native macOS .app directory.
# Note: The --windows-icon-from-ico flag is removed because macOS requires a .icns 
# file for OS-level application icons, not a .ico file. 
echo "Building standalone macOS App Bundle with Nuitka..."
python3 -m nuitka \
    --standalone \
    --assume-yes-for-downloads \
    --deployment \
    --macos-create-app-bundle \
    --enable-plugin=tk-inter \
    --macos-app-icon=nxdt.icns \
    nxdt_host.py

echo "Zipping the app bundle..."
# Remove any previous zip
rm -f nxdt_host_mac.zip

# Using 'ditto' is the standard/safest way to zip macOS app bundles via CLI. 
# It ensures symlinks, executable permissions, and resource forks are perfectly preserved.
ditto -c -k --sequesterRsrc --keepParent nxdt_host.app nxdt_host_mac.zip

echo "Cleaning up build artifacts..."
rm -rf nxdt_host.build
rm -rf nxdt_host.dist
rm -rf nxdt_host.app
rm -rf "$VENV_NAME"

echo "Done! macOS artifact created: nxdt_host_mac.zip"
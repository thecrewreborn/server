#!/bin/bash
# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
set -e
cd "$(dirname "$0")"

PY_VERSION=3.12.7

python_ok() {
    command -v "$1" >/dev/null 2>&1 && "$1" -c 'import sys; sys.exit(sys.version_info < (3, 10))' >/dev/null 2>&1
}

PY=python3
if ! python_ok "$PY"; then
    if [ "$(uname)" = "Darwin" ]; then
        if command -v brew >/dev/null 2>&1; then
            echo "Installing Python with Homebrew..."
            brew install python
        else
            echo "Downloading Python $PY_VERSION from python.org..."
            PKG="$(mktemp -d)/python.pkg"
            curl -fL --progress-bar -o "$PKG" "https://www.python.org/ftp/python/$PY_VERSION/python-$PY_VERSION-macos11.pkg"
            echo "Installing Python (macOS asks for your password)..."
            sudo installer -pkg "$PKG" -target /
        fi
        PY=/usr/local/bin/python3
        python_ok "$PY" || PY=python3
    else
        echo "Please install Python 3.10 or newer, e.g.: sudo apt install python3 python3-venv"
        exit 1
    fi
fi

if [ ! -x .venv/bin/python ]; then
    echo "Setting up the server environment..."
    "$PY" -m venv .venv
fi
.venv/bin/python -m pip install -q --disable-pip-version-check -r requirements-server.txt

echo "Starting The Crew Reborn server..."
exec .venv/bin/python -m tcreborn "$@"

#!/bin/bash
set -e

echo "Installing ClipForge..."

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

mkdir -p "$HOME/.local/bin"
cp build/clipforge "$HOME/.local/bin/clipforge"
chmod +x "$HOME/.local/bin/clipforge"
ln -sf "$HOME/.local/bin/clipforge" "$HOME/.local/bin/clip"
echo "  Binary installed to ~/.local/bin/clipforge"
echo "  Symlink created: clip -> clipforge"

mkdir -p "$HOME/.config/systemd/user"
cp systemd/clipforge.service "$HOME/.config/systemd/user/clipforge.service"
echo "  Systemd service installed"

systemctl --user daemon-reload
systemctl --user enable clipforge
systemctl --user start clipforge
echo "  Service enabled and started"

if [[ ":$PATH:" != *":$HOME/.local/bin:"* ]]; then
    echo ""
    echo "  NOTE: Add this to your ~/.bashrc:"
    echo "    export PATH=\"\$HOME/.local/bin:\$PATH\""
fi

echo ""
echo "ClipForge installed successfully!"
echo "Run: clip list"

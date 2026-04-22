#!/bin/bash

echo "Uninstalling ClipForge..."

systemctl --user stop clipforge 2>/dev/null || true
systemctl --user disable clipforge 2>/dev/null || true
rm -f "$HOME/.config/systemd/user/clipforge.service"
systemctl --user daemon-reload
rm -f "$HOME/.local/bin/clipforge"
rm -f "$HOME/.local/bin/clip"

read -p "Remove clipboard history and config? (y/N): " answer
if [[ "$answer" == "y" || "$answer" == "Y" ]]; then
    rm -rf "$HOME/.local/share/clipforge"
    rm -rf "$HOME/.config/clipforge"
    echo "  Data removed"
fi

echo "ClipForge uninstalled"

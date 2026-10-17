#!/bin/bash
# ThemeGallery launcher

TG_DIR="/opt/system/Tools/ThemeGallery"
LOG="$TG_DIR/ThemeGallery.log"

cd "$TG_DIR" || exit 1

# bundled libs apply to the app only, not to anything run after it
LD_LIBRARY_PATH="$TG_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ./ThemeGallery > "$LOG" 2>&1
rc=$?

# exit code 3 = themes installed/removed, ES must reload
if [ "$rc" -eq 3 ]; then
    sudo systemctl restart emulationstation.service
fi
exit 0
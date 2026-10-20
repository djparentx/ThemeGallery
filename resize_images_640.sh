#!/bin/bash

find . -type f \( -iname "*.png" -o -iname "*.jpg" -o -iname "*.jpeg" \) -print0 |
while IFS= read -r -d '' file; do
    dimensions=$(identify -ping -format '%wx%h' "$file" 2>/dev/null | head -n 1)

    if [[ "$dimensions" == "640x480" ]]; then
        convert "$file" -resize 480x360! "$file"
        echo "Resized: $file"
    fi
done
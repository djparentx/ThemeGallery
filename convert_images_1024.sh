#!/bin/bash

find . -type f \( -iname "*.png" -o -iname "*.jpg" -o -iname "*.jpeg" \) -print0 |
while IFS= read -r -d '' file; do
    dimensions=$(identify -ping -format '%wx%h' "$file" 2>/dev/null | head -n 1)

    if [[ "$dimensions" == "1024x768" ]]; then
        convert "$file" -resize 640x480! "$file"
        echo "Resized: $file"
    fi

    webp="${file%.*}.webp"
    if cwebp -quiet -q 90 "$file" -o "$webp"; then
        rm "$file"
        echo "Converted: $webp"
    else
        echo "Failed:    $file"
    fi
done
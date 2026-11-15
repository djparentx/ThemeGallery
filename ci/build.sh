#!/bin/bash
# Runs inside debian:buster (glibc 2.28) so the binary and bundled libs load on
# dArkOSen, a4c, ArkOS, dArkOSRE and dArkOS
set -euo pipefail

TG_VERSION="${TG_VERSION:?TG_VERSION not set}"
CMAKE_VER=3.31.6
SDL_TAG=release-2.0.18

# buster is archived
cat > /etc/apt/sources.list << 'EOF'
deb http://archive.debian.org/debian buster main
deb http://archive.debian.org/debian buster-updates main
deb http://archive.debian.org/debian-security buster/updates main
EOF
apt-get -o Acquire::Check-Valid-Until=false update
apt-get install -y --no-install-recommends \
    build-essential git ca-certificates curl \
    libfreetype6-dev libcurl4-openssl-dev libwebp-dev zip patch

# CMake >= 3.20 (buster ships 3.13)
curl -fsSL "https://github.com/Kitware/CMake/releases/download/v$CMAKE_VER/cmake-$CMAKE_VER-linux-aarch64.tar.gz" | tar -xz -C /opt
export PATH="/opt/cmake-$CMAKE_VER-linux-aarch64/bin:$PATH"

# SDL2 2.0.18 to link against (buster has 2.0.9; app needs SDL_RenderGeometry). Device SDL2 is used at runtime, never bundled.
git clone -q --depth 1 -b "$SDL_TAG" https://github.com/libsdl-org/SDL.git /tmp/SDL
cmake -S /tmp/SDL -B /tmp/SDL/build -DCMAKE_BUILD_TYPE=Release -DSDL_STATIC=OFF -DSDL_TEST=OFF -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build /tmp/SDL/build -j"$(nproc)"
cmake --install /tmp/SDL/build
ldconfig

# Configure + build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTG_VERSION="$TG_VERSION"
cmake --build build -j"$(nproc)"

# Package
OUT=out/Tools
APP="$OUT/ThemeGallery"
mkdir -p "$APP/lib" "$APP/fonts" "$APP/Images"

cp build/ThemeGallery "$APP/"
strip "$APP/ThemeGallery"
cp dist/ThemeGallery.sh "$OUT/"
chmod +x "$OUT/ThemeGallery.sh" "$APP/ThemeGallery"
cp assets/fonts/Exo2-Regular.ttf assets/fonts/Exo2-SemiBold.ttf assets/fonts/OFL.txt "$APP/fonts/"
cp dist/gallery.ini "$APP/"
if [ -d assets/Images ]; then cp -r assets/Images/. "$APP/Images/"; fi
if [ -d patches ]; then mkdir -p "$APP/patches" && cp -r patches/. "$APP/patches/"; fi

# bundled GNU patch (no device has /usr/bin/patch); must need only libc
mkdir -p "$APP/bin"
cp /usr/bin/patch "$APP/bin/patch"
strip "$APP/bin/patch"
chmod +x "$APP/bin/patch"
extra=$(ldd "$APP/bin/patch" | awk '{print $1}' | grep -Ev '^(linux-vdso|ld-linux|libc\.so)' || true)
if [ -n "$extra" ]; then
    echo "bin/patch needs more than libc: $extra"
    exit 1
fi

# Libraries the device provides: never bundled, never descended into
EXCLUDE='^(linux-vdso|ld-linux|libc\.so|libm\.so|libdl\.so|librt\.so|libpthread\.so|libresolv\.so|libstdc\+\+\.so|libgcc_s\.so|libSDL2-2\.0\.so|libEGL|libGLES|libGL\.so|libmali|libdrm|libgbm|libasound|libudev)'

# soname -> resolved path, from the full ldd closure
declare -A PATHOF
while read -r soname path; do
    PATHOF["$soname"]="$path"
done < <(ldd build/ThemeGallery | awk '$2 == "=>" && $3 ~ /^\// {print $1, $3}')

# Walk NEEDED entries from the binary, skipping excluded subtrees
declare -A SEEN
queue=(build/ThemeGallery)
while [ ${#queue[@]} -gt 0 ]; do
    f="${queue[0]}"
    queue=("${queue[@]:1}")
    for need in $(readelf -d "$f" | awk '/\(NEEDED\)/ {gsub(/[][]/, "", $5); print $5}'); do
        [ -n "${SEEN[$need]:-}" ] && continue
        SEEN["$need"]=1
        if printf '%s\n' "$need" | grep -Eq "$EXCLUDE"; then continue; fi
        p="${PATHOF[$need]:-}"
        if [ -z "$p" ]; then
            echo "unresolved dependency: $need (needed by $f)"
            exit 1
        fi
        cp -L "$p" "$APP/lib/$need"
        strip --strip-unneeded "$APP/lib/$need"
        queue+=("$p")
    done
done
ls -l "$APP/lib"

# zip keeps exec bits; root holds ThemeGallery.sh + ThemeGallery/
(cd "$OUT" && zip -qr ../../ThemeGallery.zip ThemeGallery.sh ThemeGallery)

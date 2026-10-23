#!/usr/bin/env bash
# Builds the French SoulSilver ROM and turns it into an Android app with ssrecomp.
# Run on a Mac or Linux PC with the Android SDK and NDK; see docs/ANDROID.md.
set -euo pipefail

usage() {
    cat <<'EOF'
usage: android/build.sh [options]

  --install        install on the Android device adb sees (cable or wireless debugging)
  --app-id ID      application ID (default: io.github.<your user name>.soulsilverfr)
  --name NAME      name under the app icon (default: SoulSilver FR)
  --icon IMAGE     app icon from this picture (PNG or JPEG, cropped to a square)
                   (default: ios/icon.png)
  --sdk DIR        Android SDK (default: $ANDROID_HOME, else the usual place)
  --ndk DIR        Android NDK (default: $ANDROID_NDK_HOME, else the newest in the SDK)
  --jobs N         parallel compile jobs (default: one per 2 GB of RAM, at most one per core)
  --skip-rom       use the French ROM already built in build/soulsilver.us.fr
EOF
}

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

# ssrecomp release v0.4.2; ios/ssrecomp-ios.patch (the iOS and Android changes) is made
# against this commit.
SSRECOMP_URL=https://github.com/ClickingMaster/ssrecomp.git
SSRECOMP_COMMIT=9c185b63c6826880b2c49391eb1799191d7f40d9
# SDL2 for Android, as ssrecomp's own Android build uses (and the iOS app).
SDL2_NAME=SDL2-2.32.10
SDL2_URL=https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz
SDL2_SHA256=5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165
ABI=arm64-v8a          # 64-bit ARM: phones from about 2015 on
MIN_API=24             # Android 7.0

install=0
app_id=""
app_name="SoulSilver FR"
icon_src=""
sdk="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
ndk="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
jobs=""
skip_rom=0
while [[ $# -gt 0 ]]; do
    case "$1" in
    --install) install=1; shift ;;
    --app-id) app_id="$2"; shift 2 ;;
    --name) app_name="$2"; shift 2 ;;
    --icon) icon_src="$2"; shift 2 ;;
    --sdk) sdk="$2"; shift 2 ;;
    --ndk) ndk="$2"; shift 2 ;;
    --jobs) jobs="$2"; shift 2 ;;
    --skip-rom) skip_rom=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ -z "$sdk" ]]; then
    for d in "$HOME/Library/Android/sdk" "$HOME/Android/Sdk"; do
        [[ -d "$d" ]] && sdk="$d" && break
    done
fi
if [[ -z "$sdk" || ! -d "$sdk/build-tools" || ! -d "$sdk/platforms" ]]; then
    echo "No Android SDK found (with build-tools and a platform). Install Android Studio, or the" >&2
    echo "command-line tools, then pass --sdk or set ANDROID_HOME." >&2
    exit 1
fi
newest() { ls -1 "$1" 2>/dev/null | sort -V | tail -n 1; }
if [[ -z "$ndk" ]]; then
    v="$(newest "$sdk/ndk")"
    [[ -n "$v" ]] && ndk="$sdk/ndk/$v"
fi
if [[ -z "$ndk" || ! -f "$ndk/build/cmake/android.toolchain.cmake" ]]; then
    echo "No Android NDK found: install one with Android Studio (SDK Manager > SDK Tools > NDK)," >&2
    echo "or pass --ndk." >&2
    exit 1
fi
build_tools="$sdk/build-tools/$(newest "$sdk/build-tools")"
platform_jar="$sdk/platforms/$(newest "$sdk/platforms")/android.jar"
for f in "$build_tools/aapt2" "$build_tools/d8" "$platform_jar"; do
    if [[ ! -f "$f" ]]; then
        echo "Missing $f: install the SDK's build-tools and a platform (SDK Manager)." >&2
        exit 1
    fi
done
if ! command -v javac >/dev/null 2>&1 || ! javac -version >/dev/null 2>&1; then
    echo "javac (a JDK, version 11 or later) is needed to compile SDL's Java code." >&2
    exit 1
fi
adb="$sdk/platform-tools/adb"
if [[ $install -eq 1 && ! -x "$adb" ]]; then
    echo "--install needs adb ($adb): install the SDK's platform-tools." >&2
    exit 2
fi
if [[ -z "$icon_src" && -f "$repo_root/ios/icon.png" ]]; then
    icon_src="$repo_root/ios/icon.png"
fi
if [[ -n "$icon_src" && ! -f "$icon_src" ]]; then
    echo "No such picture: $icon_src" >&2
    exit 2
fi
if [[ -z "$app_id" ]]; then
    user_part="$(id -un | tr '[:upper:]' '[:lower:]' | tr -cd 'a-z0-9')"
    app_id="io.github.${user_part:-player}.soulsilverfr"
fi
if [[ -z "$jobs" ]]; then
    # The recompiled game code needs about 1.5 GB per compile job.
    if [[ "$(uname -s)" == Darwin ]]; then
        cores="$(sysctl -n hw.ncpu)"
        mem_gb=$(( $(sysctl -n hw.memsize) / 1073741824 ))
    else
        cores="$(nproc)"
        mem_gb=$(( $(awk '/MemTotal/ {print $2}' /proc/meminfo) / 1048576 ))
    fi
    jobs=$(( mem_gb / 2 ))
    (( jobs > cores )) && jobs=$cores
    (( jobs < 1 )) && jobs=1
fi

tools_dir="$repo_root/.local-tools"
rom_dir="$repo_root/build/soulsilver.us.fr"
rom="$rom_dir/pokesoulsilver.us.fr.nds"
ssr="$tools_dir/ssrecomp"
venv="$tools_dir/android-venv"
work="$tools_dir/android"
# Android installs an update over the app only when it is signed with the same key: keep it.
key="$work/signing-key.json"
out_dir="$repo_root/build/android"
apk="$out_dir/SoulSilver-FR.apk"
mkdir -p "$tools_dir" "$work" "$out_dir"

step() { printf '\n==> %s\n' "$*"; }

if [[ $skip_rom -eq 0 ]]; then
    step "Building the French ROM"
    ./scripts/build.sh GAME_VERSION=SOULSILVER TRANSLATION=FRENCH
fi
for f in "$rom" "$rom_dir/main.elf" "$rom_dir/main.elf.xMAP" "$rom_dir/main.sbin"; do
    if [[ ! -f "$f" ]]; then
        echo "Missing $f: build the ROM first (drop --skip-rom)." >&2
        exit 1
    fi
done

step "Preparing Python tools"
if [[ ! -x "$venv/bin/python3" ]]; then
    python3 -m venv "$venv"
fi
# CMake 4 drops support for SDL2's own minimum version, hence the upper bound.
"$venv/bin/python3" -m pip install --quiet --upgrade "pyelftools" "cmake>=3.24,<4" "ninja" "pillow"
py="$venv/bin/python3"
cmake="$venv/bin/cmake"
ninja="$venv/bin/ninja"

step "Fetching ssrecomp ${SSRECOMP_COMMIT:0:7}"
if [[ ! -d "$ssr/.git" ]]; then
    git clone "$SSRECOMP_URL" "$ssr"
fi
if ! git -C "$ssr" cat-file -e "$SSRECOMP_COMMIT^{commit}" 2>/dev/null; then
    git -C "$ssr" fetch origin
fi
# The checkout belongs to this script (and ios/build.sh): put it back to the release and apply
# the changes, keeping the generated code and the builds between runs.
git -C "$ssr" checkout --quiet --force "$SSRECOMP_COMMIT"
git -C "$ssr" clean --quiet -fd -e gen/ -e build-ios/ -e build-android/
git -C "$ssr" apply "$repo_root/ios/ssrecomp-ios.patch"

step "Recompiling the game code (a few minutes)"
(cd "$ssr" && "$py" tools/recomp.py "$rom_dir" melonDS/src gen && "$py" tools/apps.py "$rom_dir" gen)

toolchain_args=(-G Ninja -DCMAKE_MAKE_PROGRAM="$ninja" -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake"
    -DANDROID_ABI="$ABI" -DANDROID_PLATFORM="android-$MIN_API")

sdl_src="$work/$SDL2_NAME"
sdl_prefix="$work/sdl2-$ABI"
if [[ ! -f "$sdl_prefix/lib/libSDL2.so" ]]; then
    step "Compiling SDL2 for Android (once)"
    tarball="$work/$SDL2_NAME.tar.gz"
    if [[ ! -f "$tarball" ]]; then
        curl -fL --retry 3 -o "$tarball.part" "$SDL2_URL"
        mv "$tarball.part" "$tarball"
    fi
    sum="$("$py" -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$tarball")"
    if [[ "$sum" != "$SDL2_SHA256" ]]; then
        rm -f "$tarball"
        echo "The SDL2 download is not the expected file (sha256 $sum); run the script again." >&2
        exit 1
    fi
    rm -rf "$sdl_src"
    tar -xzf "$tarball" -C "$work"
    "$cmake" -S "$sdl_src" -B "$work/build-sdl2-$ABI" "${toolchain_args[@]}" \
        -DCMAKE_INSTALL_PREFIX="$sdl_prefix" -DSDL_STATIC=OFF -DSDL_TEST=OFF
    "$ninja" -C "$work/build-sdl2-$ABI" -j "$jobs" install
fi

step "Compiling the app with $jobs jobs (20-40 minutes the first time)"
build="$ssr/build-android"
"$cmake" -S "$ssr" -B "$build" "${toolchain_args[@]}" -DSDL2_DIR="$sdl_prefix/lib/cmake/SDL2"
"$ninja" -C "$build" -j "$jobs"

step "Packaging"
# The APK's frame: the manifest (with this app's ID and name), the icon and SDL's Java code,
# compiled as ssrecomp's tools/android/make_skeleton.sh does; apk.py then adds the game.
skel="$work/skeleton"
rm -rf "$skel"
mkdir -p "$skel/res" "$skel/classes" "$skel/dex"
"$py" - "$ssr/tools/android/AndroidManifest.xml" "$skel/AndroidManifest.xml" "$app_id" "$app_name" <<'PY'
import re, sys, time
src, dst, app_id, name = sys.argv[1:5]
m = open(src, encoding='utf-8').read()
m = m.replace('package="io.github.clickingmaster.ssrecomp"', f'package="{app_id}"')
m = re.sub(r'android:label="[^"]*"', 'android:label="%s"' % name.replace('&', '&amp;').replace('"', '&quot;'), m)
# Each build a higher version, so that it installs over the one before.
m = re.sub(r'android:versionCode="\d+"', f'android:versionCode="{int(time.time()) // 60}"', m)
m = re.sub(r'android:versionName="[^"]*"', 'android:versionName="fr-%s"' % time.strftime('%Y%m%d.%H%M'), m)
# Only ssrecomp's check for a newer release used the network, and this build has none.
m = re.sub(r'\s*<!-- Only to ask GitHub[^>]*-->\s*<uses-permission android:name="android.permission.INTERNET" />', '', m)
assert 'INTERNET' not in m and app_id in m
open(dst, 'w', encoding='utf-8').write(m)
PY
"$py" - "$icon_src" "$skel/res" "$ssr/tools/appimage/ssrecomp-setup.png" <<'PY'
import os, sys
from PIL import Image
src, res, fallback = sys.argv[1:4]
im = Image.open(src or fallback).convert('RGBA')
side = min(im.size)
im = im.crop(((im.width - side) // 2, (im.height - side) // 2, (im.width + side) // 2, (im.height + side) // 2))
for density, px in (('mdpi', 48), ('hdpi', 72), ('xhdpi', 96), ('xxhdpi', 144), ('xxxhdpi', 192)):
    os.makedirs(f'{res}/mipmap-{density}', exist_ok=True)
    im.resize((px, px), Image.LANCZOS).save(f'{res}/mipmap-{density}/ic_launcher.png', optimize=True)
PY
"$build_tools/aapt2" compile --dir "$skel/res" -o "$skel/res.zip"
"$build_tools/aapt2" link -o "$skel/skeleton.apk" -I "$platform_jar" --manifest "$skel/AndroidManifest.xml" \
    --min-sdk-version "$MIN_API" --target-sdk-version 34 "$skel/res.zip"
if [[ ! -d "$sdl_src" ]]; then
    tar -xzf "$work/$SDL2_NAME.tar.gz" -C "$work"
fi
javac -nowarn --release 11 -cp "$platform_jar" -d "$skel/classes" \
    $(find "$sdl_src/android-project/app/src/main/java" -name '*.java') 2>&1 | grep -v '^Note:' || true
"$build_tools/d8" --release --min-api "$MIN_API" --lib "$platform_jar" --output "$skel/dex" \
    $(find "$skel/classes" -name '*.class')
(cd "$skel/dex" && zip -q -X "$skel/skeleton.apk" classes.dex)
# Without the debugging information, which the phone would unpack for nothing (about 40 MB).
strip_tool="$(find "$ndk/toolchains/llvm/prebuilt" -maxdepth 3 -name llvm-strip | head -n 1)"
"$strip_tool" --strip-debug -o "$skel/libmain.so" "$build/libmain.so"
"$py" "$ssr/tools/android/apk.py" "$skel/skeleton.apk" "$ABI" "$skel/libmain.so" \
    "$sdl_prefix/lib/libSDL2.so" "$rom" "$key" "$apk"
echo "APK: $apk"
echo "It contains your game: keep it to yourself. Keep $key too:"
echo "later builds install over this one (keeping the save) only when signed with it."

if [[ $install -eq 1 ]]; then
    step "Installing on the Android device"
    if ! "$adb" get-state >/dev/null 2>&1; then
        echo "adb sees no device (or more than one). Connect it by cable with USB debugging on, or" >&2
        echo "pair it with wireless debugging, then run: $adb install -r $apk" >&2
        exit 1
    fi
    "$adb" install -r "$apk"
    echo "Installed: the app is called $app_name."
fi

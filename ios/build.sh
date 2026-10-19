#!/usr/bin/env bash
# Builds the French SoulSilver ROM and turns it into an iOS app with ssrecomp.
# Run on a Mac with Xcode; see docs/IOS.md.
set -euo pipefail

usage() {
    cat <<'EOF'
usage: ios/build.sh [options]

  --team ID        sign for this Apple development team (Xcode > Settings > Accounts);
                   without it the app is left unsigned for Sideloadly or AltStore
  --install        install on the iPhone connected by cable (needs --team)
  --bundle-id ID   bundle identifier (default: io.github.<your user name>.soulsilverfr)
  --name NAME      name under the app icon (default: SoulSilver FR)
  --jobs N         parallel compile jobs (default: one per 2 GB of RAM, at most one per core)
  --skip-rom       use the French ROM already built in build/soulsilver.us.fr
  --open           open the Xcode project afterwards instead of building it there
EOF
}

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

# ssrecomp release v0.4.2; ios/ssrecomp-ios.patch is made against this commit.
SSRECOMP_URL=https://github.com/ClickingMaster/ssrecomp.git
SSRECOMP_COMMIT=9c185b63c6826880b2c49391eb1799191d7f40d9

team=""
install=0
bundle_id=""
app_name="SoulSilver FR"
jobs=""
skip_rom=0
open_xcode=0
while [[ $# -gt 0 ]]; do
    case "$1" in
    --team) team="$2"; shift 2 ;;
    --install) install=1; shift ;;
    --bundle-id) bundle_id="$2"; shift 2 ;;
    --name) app_name="$2"; shift 2 ;;
    --jobs) jobs="$2"; shift 2 ;;
    --skip-rom) skip_rom=1; shift ;;
    --open) open_xcode=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ "$(uname -s)" != Darwin ]]; then
    echo "The iOS app can only be built on a Mac." >&2
    exit 1
fi
if ! xcrun --sdk iphoneos --show-sdk-path >/dev/null 2>&1; then
    echo "The iOS SDK is missing: install Xcode from the App Store, open it once, then run" >&2
    echo "  sudo xcode-select -s /Applications/Xcode.app/Contents/Developer" >&2
    exit 1
fi
if [[ $install -eq 1 && -z "$team" ]]; then
    echo "--install needs --team: an unsigned app cannot be installed directly." >&2
    exit 2
fi
if [[ -z "$bundle_id" ]]; then
    user_part="$(id -un | tr '[:upper:]' '[:lower:]' | tr -cd 'a-z0-9')"
    bundle_id="io.github.${user_part:-player}.soulsilverfr"
fi
if [[ -z "$jobs" ]]; then
    # The recompiled game code needs about 1.5 GB per compile job.
    cores="$(sysctl -n hw.ncpu)"
    mem_gb=$(( $(sysctl -n hw.memsize) / 1073741824 ))
    jobs=$(( mem_gb / 2 ))
    (( jobs > cores )) && jobs=$cores
    (( jobs < 1 )) && jobs=1
fi

tools_dir="$repo_root/.local-tools"
rom_dir="$repo_root/build/soulsilver.us.fr"
rom="$rom_dir/pokesoulsilver.us.fr.nds"
ssr="$tools_dir/ssrecomp"
venv="$tools_dir/ios-venv"
out_dir="$repo_root/build/ios"
mkdir -p "$tools_dir" "$out_dir"

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
# CMake 3.24 or newer is needed by SDL2 on Apple platforms; CMake 4 drops support for SDL2's
# own minimum version, hence the upper bound.
"$venv/bin/python3" -m pip install --quiet --upgrade "pyelftools" "cmake>=3.24,<4" "ninja"
py="$venv/bin/python3"
cmake="$venv/bin/cmake"

step "Fetching ssrecomp ${SSRECOMP_COMMIT:0:7}"
if [[ ! -d "$ssr/.git" ]]; then
    git clone "$SSRECOMP_URL" "$ssr"
fi
if ! git -C "$ssr" cat-file -e "$SSRECOMP_COMMIT^{commit}" 2>/dev/null; then
    git -C "$ssr" fetch origin
fi
# The checkout belongs to this script: put it back to the release and apply the iOS changes,
# keeping the generated code and the Xcode build between runs.
git -C "$ssr" checkout --quiet --force "$SSRECOMP_COMMIT"
git -C "$ssr" clean --quiet -fd -e gen/ -e build-ios/
git -C "$ssr" apply "$repo_root/ios/ssrecomp-ios.patch"

step "Recompiling the game code (a few minutes)"
(cd "$ssr" && "$py" tools/recomp.py "$rom_dir" melonDS/src gen && "$py" tools/apps.py "$rom_dir" gen)

step "Creating the Xcode project"
"$cmake" -S "$ssr" -B "$ssr/build-ios" -G Xcode \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
    -DSSR_IOS_ROM="$rom" \
    -DSSR_IOS_BUNDLE_ID="$bundle_id" \
    -DSSR_IOS_NAME="$app_name" \
    -DSSR_IOS_TEAM="$team"

if [[ $open_xcode -eq 1 ]]; then
    open "$ssr/build-ios/ssrecomp.xcodeproj"
    echo "In Xcode: pick the ssrecomp target, your team under Signing & Capabilities, your iPhone, then Run."
    exit 0
fi

step "Compiling the app with $jobs jobs (20-40 minutes the first time)"
xcode_args=(-project "$ssr/build-ios/ssrecomp.xcodeproj" -target ssrecomp -configuration Release
    -sdk iphoneos -jobs "$jobs")
if [[ -n "$team" ]]; then
    xcodebuild "${xcode_args[@]}" -allowProvisioningUpdates -allowProvisioningDeviceRegistration \
        DEVELOPMENT_TEAM="$team" CODE_SIGN_STYLE=Automatic build
else
    xcodebuild "${xcode_args[@]}" CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO CODE_SIGN_IDENTITY="" build
fi

app="$(find "$ssr/build-ios" -maxdepth 3 -type d -path '*Release-iphoneos/SoulSilver.app' | head -n 1)"
if [[ -z "$app" ]]; then
    echo "The build finished but SoulSilver.app was not found under $ssr/build-ios." >&2
    exit 1
fi

step "Packaging"
rm -rf "$out_dir/Payload" "$out_dir/SoulSilver-FR.app" "$out_dir/SoulSilver-FR.ipa"
mkdir -p "$out_dir/Payload"
cp -R "$app" "$out_dir/Payload/"
cp -R "$app" "$out_dir/SoulSilver-FR.app"
(cd "$out_dir" && zip -qry SoulSilver-FR.ipa Payload)
rm -rf "$out_dir/Payload"
echo "App: $out_dir/SoulSilver-FR.app"
echo "IPA: $out_dir/SoulSilver-FR.ipa"
echo "It contains your game: keep it to yourself."

if [[ $install -eq 1 ]]; then
    step "Installing on the iPhone"
    devices_json="$out_dir/devices.json"
    xcrun devicectl list devices --json-output "$devices_json" >/dev/null
    device="$("$py" - "$devices_json" <<'PY'
import json, sys
devices = json.load(open(sys.argv[1]))["result"]["devices"]
for d in devices:
    props = d.get("hardwareProperties", {})
    if props.get("platform") == "iOS" and d.get("connectionProperties", {}).get("pairingState") == "paired":
        print(d["identifier"])
        break
PY
)"
    rm -f "$devices_json"
    if [[ -z "$device" ]]; then
        echo "No paired iPhone found. Connect it by cable, unlock it and trust this Mac, then install" >&2
        echo "$out_dir/SoulSilver-FR.app from Xcode (Window > Devices and Simulators)." >&2
        exit 1
    fi
    xcrun devicectl device install app --device "$device" "$out_dir/SoulSilver-FR.app"
    echo "Installed. The first time, on the iPhone: turn on Settings > Privacy & Security >"
    echo "Developer Mode, then trust your Apple ID in Settings > General > VPN & Device Management."
fi

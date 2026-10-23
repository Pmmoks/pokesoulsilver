# SoulSilver in French on Android

The same app as the [iPhone one](IOS.md), for Android phones and handhelds: one
screen with the touch-screen buttons brought onto it, played with a controller,
**R2** held for the English original and R2 with the D-pad to turn the pages of a
conversation. Everything under *Playing* in [IOS.md](IOS.md) holds here too.

It is built with ssrecomp's own Android support (the game's code as a native library
in SDL's Android app) and the same `ios/ssrecomp-ios.patch`. The finished app
contains the game: never share the `.apk`.

## What you need

- A Mac or Linux PC that already builds this repository (see [INSTALL.md](../INSTALL.md)),
  with about 3 GB of free disk space and 16 GB of RAM.
- The Android SDK with *build-tools*, a *platform* and the *NDK* (Android Studio's
  SDK Manager installs them; the script finds them in the usual place, or takes
  `--sdk` and `--ndk`), and a JDK 11 or later (`javac`).
- A phone or handheld with a 64-bit ARM processor and Android 7.0 or later.

## Build

```sh
android/build.sh                 # build/android/SoulSilver-FR.apk
android/build.sh --install       # and install it on the device adb sees
```

The script builds the French ROM (`--skip-rom` reuses it), recompiles its code as
`ios/build.sh` does, compiles SDL2 for Android once and then the app (20–40 minutes the
first time), and packs and signs `build/android/SoulSilver-FR.apk`. `--icon picture.jpg`
changes the icon (default `ios/icon.png`), `--name` the name under it.

The signing key is made on the first build and kept in
`.local-tools/android/signing-key.json`. Android installs a new build over the old one,
keeping the save, only when both are signed with the same key: keep that file. Each
build gets a higher version number, so a new one always installs over an older one.

### Installing

- **With adb** (`--install`): on the phone, turn on *Developer options* (tap *Build
  number* in *Settings › About phone › Software information* seven times), then *USB
  debugging*, connect it and allow the PC. *Wireless debugging* (Android 11 and later)
  works without the cable once paired with `adb pair`.
- **Without**: copy the APK to the phone (cable, cloud drive…), open it there and allow
  the file manager to install apps when asked.

Uninstalling the app deletes its save.

## Differences from the iPhone

- The save and the settings are in the app's own storage, which only the app can
  read. `log.txt` is in `Android/data/<app id>/files` on the shared storage (a file
  manager, or a PC over USB, opens it; `adb shell cat` reads it).
- The widescreen map is drawn by the graphics chip (OpenGL ES 3), where the iPhone
  draws it with the processor: on a Galaxy Note 9 a picture took 10 ms there against
  25 ms on three processor cores, and the game no longer waits for it. If the graphics
  chip cannot be used, `log.txt` says why (`[wide] graphics chip: ...`) and the
  processor draws as before. The game itself runs on the fastest cores (`[cpu]` in
  `log.txt`).
- A phone slower than an iPhone may still not keep up with the iPhone's settings in the
  busiest scenes, and the sound then runs short (`[audio] the queue ran dry` in
  `log.txt`). `[perf]` lines there say how long each part of a frame took (a frame has
  16.7 ms). On the Note 9 the fast cores were held at 1.8 GHz of their 2.7 by
  the phone; Samsung's *Settings › Device care › Battery › Power mode* chooses how far
  the processor may go. *60 FPS* off in the pause menu is the next thing to try: on
  the Note 9 that left a quarter of every frame to spare. *WORLD DETAIL* lower and then
  *WIDESCREEN WORLD* off save more.
- For trying other settings without building again, a file `tuning.txt` in the same
  folder as `log.txt` sets environment variables for the app, one `NAME=VALUE` a line:
  `SSR_WIDE_GPU=0` draws the map with the processor, `SSR_MAIN_CPUS` and
  `SSR_WIDE_CPUS` (hexadecimal core masks) choose the cores for the game and for
  drawing, `SSR_WIDE_THREADS` the number of drawing threads. `android/build.sh
  --profileable` lets Android's `simpleperf` sample the app over adb.
- The app ID is `io.github.<your user name>.soulsilverfr` (`--app-id` changes it).
- Controllers come through Android's own gamepad support. The Backbone's buttons
  are the same as on the iPhone; on other pads, *Start* and *Select* (or *Menu* and
  *View*) are ≡ and ⧉.
- The phone's Back gesture opens the pause menu instead of closing the game.

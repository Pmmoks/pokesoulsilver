# SoulSilver in French on iPhone

This builds Pokémon SoulSilver with French text into an iPhone app. It shows one
screen, with the touch-screen buttons brought onto it, and you play it with a
Backbone or any other MFi controller. **R2** switches between the French text
and the English original.

The app is [ssrecomp](https://github.com/ClickingMaster/ssrecomp), which turns
the game's ARM9 code into native code and runs it on the melonDS core.
`ios/ssrecomp-ios.patch` adds an iOS port to its v0.4.2 release, plus the R2
switch and support for this repository's build output. Everything is built on
your Mac from your own copy of the game. The finished app contains the game:
never share the `.app` or `.ipa`.

## What you need

- A Mac that already builds this repository (see [INSTALL.md](../INSTALL.md)),
  with **Xcode** from the App Store opened once (the Command Line Tools alone
  have no iOS SDK).
- About 3 GB of free disk space and 16 GB of RAM. The recompiled game code needs
  about 1.5 GB per compile job; the script picks the number of jobs from your RAM.
- An iPhone or iPad on iOS 15 or later, and a free Apple ID (or a paid developer
  account).

## Build

```sh
ios/build.sh                       # unsigned build/ios/SoulSilver-FR.ipa, for Sideloadly or AltStore
ios/build.sh --team ABCDE12345     # signed with your Apple ID's team
ios/build.sh --team ABCDE12345 --install   # signed and installed on the iPhone on the cable
```

The script:

1. builds the French ROM: `./scripts/build.sh GAME_VERSION=SOULSILVER TRANSLATION=FRENCH`,
   giving `build/soulsilver.us.fr/pokesoulsilver.us.fr.nds`;
2. fetches ssrecomp into `.local-tools/ssrecomp` and applies the iOS patch;
3. recompiles the ROM's code into `.local-tools/ssrecomp/gen` (a few minutes);
4. generates an Xcode project and compiles the app: 20–40 minutes the first time;
5. writes `build/ios/SoulSilver-FR.app` and `build/ios/SoulSilver-FR.ipa`.

Use `--skip-rom` to reuse a ROM you built already. `--open` stops after step 4
and opens the project in Xcode instead, if you prefer to sign and run from there
(choose the `ssrecomp` target, your team under *Signing & Capabilities*, your
iPhone, then *Run*).

### Signing with a free Apple ID

1. In Xcode, *Settings › Accounts*: add your Apple ID. Its team appears as
   "(Personal Team)"; the ten-character team ID is shown in *Manage Certificates*,
   or in Keychain Access in the name of your "Apple Development" certificate.
2. Connect the iPhone by cable, unlock it and trust the Mac.
3. Run `ios/build.sh --team <TEAM ID> --install`.
4. On the iPhone, turn on *Settings › Privacy & Security › Developer Mode* (iOS 16
   and later; the phone restarts), then trust your Apple ID in *Settings › General
   › VPN & Device Management*.

Apps signed by a free account stop opening after seven days. Run the same
command again to renew it: the save is kept. The bundle identifier defaults to
`io.github.<your Mac user name>.soulsilverfr`; change it with `--bundle-id` if
Xcode says it is taken.

### Without Xcode signing

`ios/build.sh` without `--team` leaves the app unsigned. Sideloadly or AltStore
sign `build/ios/SoulSilver-FR.ipa` with your Apple ID when they install it.

## Playing

| Backbone | Game |
|---|---|
| A / B / X / Y | A / B / X / Y, by the letters on the buttons (pause menu › BUTTONS switches to by position) |
| D-pad, left stick | D-pad |
| L1 / R1 | L / R |
| Menu (≡) / Options (⧉) | Start / Select |
| **R2** | **French ⇄ English text** |
| L2 (hold) | fast forward |
| Menu + Options together | pause menu (save states, layout, 60 fps, widescreen…) |

The language switch works through the DS's debug button, which retail consoles
lack and this ROM reads for nothing else. While it is held, the game reads its
text from the English original, so everything shown after you press R2 is in
English, and in French again after the next press. Text already on screen stays
as it is until the game shows the next message or menu. The pause menu's first
entry, *TEXT (R2)*, does the same without a controller.

Without a controller, on-screen buttons are shown; using the controller hides
them and touching the screen brings them back. Wherever the game wants a touch,
touch the picture.

### Files

The save (`soulsilver.sav`), the settings and `log.txt` are in the app's
Documents folder. The Files app shows it under *On My iPhone › SoulSilver FR*,
and Finder shows it when the iPhone is connected (*Files* tab). To carry on with a
save from another emulator, put it there as `soulsilver.sav` before starting the
game. It must be a SoulSilver US save.

## Limits

- Text drawn into pictures (the title screen logo, some button art) is still in
  English: only the game's text archive is translated.
- Pokémon caught while English is shown keep their English species name as
  their name, as a traded Pokémon would.
- The iOS port is untested on a device so far. Its iOS-only code was checked on
  Linux against stand-in headers, and the rest of ssrecomp builds as usual; the
  *iOS app check* workflow (runs on pushes to ios/ on iOS_version) compiles it with Xcode
  around a synthetic stand-in for the game code. `log.txt` helps with reports.

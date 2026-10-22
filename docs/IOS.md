# SoulSilver in French on iPhone

This builds Pokémon SoulSilver with French text into an iPhone app. It shows one
screen, with the touch-screen buttons brought onto it, and you play it with a
Backbone or any other MFi controller. Hold **R2** to see the English original
of the French text.

The app is [ssrecomp](https://github.com/ClickingMaster/ssrecomp), which turns
the game's ARM9 code into native code and runs it on the melonDS core.
`ios/ssrecomp-ios.patch` adds an iOS port to its v0.4.2 release, plus the R2
button and support for this repository's build output. Everything is built on
your Mac from this repository; no ROM dump is needed, but building needs the
Metrowerks compiler and Nitro SDK that [INSTALL.md](../INSTALL.md) describes.
The finished app contains the game: never share the `.app` or `.ipa`.

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

Use `--skip-rom` to reuse a ROM you built already. The app icon is `ios/icon.png`;
`--icon picture.jpg` uses any other PNG or JPEG instead (cropped to a square in
the middle). `--open` stops after step 4
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

If Xcode stops with "PLA Update available", Apple has a new Program License
Agreement for your account: accept it on developer.apple.com (Account), then run
the command again.

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
| Menu (≡, right) | Start; on the map it opens and closes the game's menu, as X does |
| Options (⧉, left) or the Backbone button | pause menu (save states, layout, 60 fps, widescreen, sound…) |
| Left stick click | Select (unless pause menu › STICK TOUCH CURSOR is on, which uses the stick, and R2, for touching) |
| **R2 (hold)** | **English text** |
| **R2 + D-pad** | **turn the pages of the message: left/up back, right/down on** |
| L2 (hold) | fast forward |

While R2 is held, the game reads its text from the English original, and the text
already on screen is printed again in English: messages on the map and in battles,
Professor Oak's introduction, menus, lists and other screens. When you let go it
goes back to French. A message being printed carries on from the same page in the
other language. The names in a message (Pokémon, moves, items, places) change too;
only names someone typed (yours, your rival's, nicknames) are the same in both.

Pages are matched by box: the French and English messages clear the box as often,
but French sometimes needs an extra scrolled line, so the match can be one line off.
Keep R2 held and use the D-pad to go back or on a page in either language and line
them up; letting go of R2 shows the French of the page you are on. Going on past
the page the game had reached is like pressing A. The D-pad does nothing else while
R2 is held.

What can't be printed again stays as it was until the game draws it next: text
that is not from the game's text archive (numbers, names), and text drawn into
pictures. The pause menu's first entry, *TEXT (HOLD R2)*, keeps English on without
holding anything.

The pause menu's *SOUND SMOOTHING* chooses how the DS sound is resampled:
*SMOOTH* (the default) takes away the high whine the DS's own sound has, *SOFT* is
gentler still, and *OFF* is the DS sound as it was.

The app tells the game through a byte in its memory (`gMsgDataShowOriginal` in
`src/msgdata.c`), and also holds the DS's debug button, which retail consoles lack
and this ROM reads for nothing else; either one switches the text. Page turns go
through `gMsgDataPageTurn`. The build prints where it found both
(`original text switch at 0x…`, `page turns at 0x…`); if a line is missing, the
ROM is older than the app and R2 does less.

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
- The iOS port has been played on an iPhone 17 Pro. The *iOS app check* workflow
  (runs on pushes to ios/ on iOS_version) compiles it with Xcode around a synthetic
  stand-in for the game code. `log.txt` helps with reports: it says, for example,
  whether the sound ever ran short (`[audio] the queue ran dry`), and with `[view]`,
  `[scene]` and `[wide]` lines, the layout settings, the size of the picture and
  why the map is or is not shown wide.

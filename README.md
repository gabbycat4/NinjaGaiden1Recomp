# Ninja Gaiden Recompiled

A native PC port of **Ninja Gaiden** (NES, USA), built with
[NESRecomp](https://github.com/mstan/nesrecomp). The game's own 6502 code is
recompiled to C and runs natively, cycle-accurate, at the speed of the real
console, with additions the NES could never do:

- **120 / 180 / 240 pictures a second.** In-between pictures for
  high-refresh and FreeSync / G-Sync monitors, drawn at 4x resolution so
  things can move a quarter of a NES pixel at a time.
- **Smooth scrolling.** The NES camera moves 1.5 pixels a frame as a
  1, 2, 1, 2 limp; here the scenery glides at an even speed. **Camera
  easing** lets it speed up and slow down over a tenth of a second instead
  of jumping from standing still to full speed in one frame, which the
  fine fences of Act 1 showed as a flash.
- **Steady lanterns and enemies.** The game places its objects with a
  camera rounded the other way from the scenery's, so on a NES the item
  lanterns wobble a pixel against the wall as you run. They are drawn where
  they belong.
- **Character glide.** Ryu, the enemies and the bosses glide between the
  game's positions instead of hopping in uneven steps, also while Ryu's legs
  change pose.
- **No sprite flicker.** Every sprite is drawn every frame (the NES can only
  draw 8 per line; up to 25 share a line in this game).
- The **cinema scenes** are shown exactly as the NES drew them, an **edge
  crop** hides the NES's scrolling garbage, an in-game **settings menu**,
  and save states with pictures.

Everything above changes only the picture; the game itself runs exactly as
on a NES, slowdown included. Each feature can be switched off in the menu.

**You need your own ROM.** This repository contains no game data. You must
supply a dump of *Ninja Gaiden (USA)*
(SHA-1 `ca513f841d75efeb33bb8099fb02beeb39f6bb9c`); the program checks it is
the right one before it runs.

## Download

Get the latest version from the
[Releases page](https://github.com/gabbycat4/NinjaGaiden1Recomp/releases/latest):

- **Windows:** `NinjaGaiden1Recomp-Windows-x64.zip`. Extract it, double-click
  `ng1recomp.exe`.
- **Linux:** `NinjaGaiden1Recomp-x86_64.AppImage`. Mark it executable
  (right-click, Properties, Permissions, or `chmod +x`), then double-click it.

The first time, the game asks for your ROM and checks it. No building needed.

## Recommended settings

**High-refresh monitor? Set Pictures per second to 240 (or 180) and turn VSync Off**
(with FreeSync/G-Sync). VSync starts On, which suits 60 Hz monitors but makes the
extra pictures stutter.

The game starts with these already on, except two you set yourself:
**Pictures per second** starts at 60, because only high-refresh monitors
benefit from more, and the game opens in a window (press **F11** for
fullscreen). In game, press **Escape** (or the right shoulder button) for the
menu:

| Menu | Setting | Recommended |
|---|---|---|
| Smooth motion | Pictures per second | **240** or **180** on a 240 Hz FreeSync/G-Sync monitor; your monitor's rate or lower otherwise; **60 (off)** on a 60 Hz monitor |
| Smooth motion | Camera easing | **Gentle** (Softest for even softer starts and stops, Quick for the game's instant starts) |
| Smooth motion | Character glide | **On** |
| Smooth motion | Second core | **On** |
| Picture | No sprite flicker | **On** |
| Picture | Edge crop | **Standard** |
| Graphics | VSync | **Off** with FreeSync/G-Sync; **On** without |
| Display | Fullscreen | On (**F11** toggles) |

Settings are saved in `config.ini` beside the program.

## Troubleshooting

- **Stutter at 120/180/240:** check that VSync is Off and your monitor's
  FreeSync/G-Sync (on KDE: Adaptive sync *Always*) is on, and that
  **Second core** is On. Close busy programs. On Linux the game keeps its
  two busy threads on separate CPU cores by itself (`CYC_PIN=0` turns that
  off); on Windows, try 180 if 240 isn't steady.
- **Starting and stopping looks like a flash on fine patterns:** set
  Camera easing to Gentle or Softest.
- **The controller stops responding:** a controller that was connected for a
  long time can stop sending input: unplug it and plug it back in.
- **The sound comes out of a PS4 controller:** a wired PS4 controller is also
  a sound card. Pick your speakers in your system's sound settings (Linux:
  set the controller's audio profile to *Off*).
- **Windows SmartScreen** warns about an unknown publisher: *More info*,
  then *Run anyway*.
- **The AppImage won't start** on an older Linux: install FUSE 2
  (`sudo apt install libfuse2`) or run it with `--appimage-extract-and-run`.
- **Wrong ROM:** the game only accepts the USA release; other dumps are
  refused before the game starts.

## Building

Only needed if you want to change the code. You need Git, CMake 3.20+, a
C/C++ compiler, Python 3.11+ and SDL2.

**Linux** (Arch/CachyOS: `sudo pacman -S git cmake ninja sdl2 python`;
Debian/Ubuntu: `sudo apt install git cmake ninja-build build-essential libsdl2-dev python3`):

```sh
git clone --recursive https://github.com/gabbycat4/NinjaGaiden1Recomp.git
cd NinjaGaiden1Recomp
cp /path/to/your/rom.nes ninjagaiden.nes
cmake -S . -B build-play -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-play --parallel
./build-play/ng1recomp ninjagaiden.nes
```

`--recursive` also downloads the engine and the menu (they are submodules).
If you cloned without it, run `git submodule update --init --recursive`.
The first configure takes a while: it builds the recompiler and translates
the game.

## Controls

Keyboard and controllers are rebindable in the launcher's Controls page.

| | |
|---|---|
| Escape / right shoulder | Menu |
| F7 | Save states |
| F8 | Rewind (off by default; turn on in the menu) |
| F11 | Fullscreen |
| Tab | Fast forward |
| F12 | Screenshot |

## Command line

Every menu setting also has an option for one run (it does not change
`config.ini`): `--present-rate N`, `--camera-easing 0|1|2`,
`--char-glide 0|1`, `--emu-thread 0|1`, `--screen-shake 0|1`,
`--no-sprite-limit 0|1`, `--sprite-order 0|1`, `--overscan T,B,L,R`.
`--plain 1` shows the engine's own picture for comparing. Developer logs:
`--scroll-log FILE`, `--oam-log FILE`, `--ppu-log FILE`;
`CYC_PACING_LOG=FILE` for picture timing.

## How it works

The NESRecomp engine runs the recompiled game cycle by cycle. This project
adds a presentation layer (`src/ng1_extras.c`) through the engine's
`HOST_EXTRAS` interface:

- each frame the screen is split into **scroll bands** from the PPU's
  per-line scroll (the status bar and the playfield, split at line 56 by
  the game's sprite-0 hit); the playfield follows a moving average of the
  game's camera (Gentle / Softest) or a 4-step average (Quick), and is
  redrawn from the background alone at its smoothed position;
- sprites are matched frame to frame (by tile, flips and palette, then by
  position when a character changes its picture) and grouped into
  characters. Objects the game places with its other-rounded **sprite
  camera** (worked out from the lanterns: it matches 99% of frames) are
  moved to where the scenery's camera puts them, and never grouped with
  Ryu;
- the **cinema scenes** switch graphics banks halfway down the screen; a
  self-check compares the redrawn picture with the NES's own every frame
  and shows the NES's picture whenever they differ;
- with a picture rate above 60, the next NES frame runs on a second core
  while the in-between pictures are shown, so they stay evenly spaced.

`notes.txt` has the measurements behind each feature, and
`tools/glide_harness.c` replays the smoothing on recorded play without a
window, which is how every change was checked.

## Help wanted

Emulating one NES frame takes about 10 ms on a Ryzen 7 2700, which is why
the second core is needed above 60. Optimizations in the engine are welcome.

## Credits and licences

- [NESRecomp](https://github.com/mstan/nesrecomp) and
  [recomp-ui](https://github.com/mstan/recomp-ui) by Matthew Stanley; their
  own licences apply (see the `nesrecomp` and `recomp-ui` folders). The engine
  submodule points at a fork with window additions (in-between pictures, the
  second-core option and its thread placement, display-sized windows, a VSync
  menu row).
- This project's code is licensed under the PolyForm Noncommercial License
  1.0.0 (`LICENSE`): free to use, share and change; not to be sold.
- *Ninja Gaiden* is © Koei Tecmo. This project is not affiliated with Koei
  Tecmo and includes none of the game.

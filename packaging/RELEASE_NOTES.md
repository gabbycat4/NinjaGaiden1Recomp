## Download and play

You need your own dump of **Ninja Gaiden (USA)**. These downloads contain no game
data; the game asks for your ROM the first time it starts and checks that it
is the right one.

### Windows
1. Download **NinjaGaiden1Recomp-Windows-x64.zip** and extract it anywhere
   (right-click, *Extract All*). Don't run it from inside the zip.
2. Double-click **ng1recomp.exe**. If Windows SmartScreen warns about an
   unknown publisher, click *More info*, then *Run anyway*.
3. Pick your ROM when asked. Settings and saves are kept in the same folder.

### Linux
1. Download **NinjaGaiden1Recomp-x86_64.AppImage**.
2. Make it executable: right-click, *Properties*, *Permissions*, tick
   *Allow executing as program* (or `chmod +x NinjaGaiden1Recomp-x86_64.AppImage`).
3. Double-click it and pick your ROM when asked. Settings and saves are kept
   in `~/.local/share/NinjaGaiden1Recomp`.

If the AppImage won't start on an older distribution, install FUSE 2
(`sudo apt install libfuse2` on Ubuntu/Debian), or run it with
`--appimage-extract-and-run`.

### High-refresh monitors (120 Hz and up)
In the menu (Escape): **Smooth motion -> Pictures per second: 240** (or 180), and
**Graphics -> VSync: Off** if your monitor has FreeSync or G-Sync (VSync On makes
the extra pictures stutter). On a 60 Hz monitor, keep 60 and VSync On.

### Controls
Escape (or the right shoulder button) opens the menu. F11 fullscreen,
F7 save states, F8 rewind, Tab fast forward.
The README has the recommended settings for high-refresh monitors
(240 or 180 pictures a second with FreeSync/G-Sync).

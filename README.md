# SmackIT

A general purpose replacement for the Smacker video library (`smackw32.dll`) used by many late 90s Windows games.

Instead of decoding `.smk` files, SmackIT plays a modern video with the same name (for example `INTRO.SMK` -> `INTRO.mp4`) through Windows Media Foundation, in a borderless overlay on top of the game window. When the video ends (or can't be found), the game is told the movie is over and carries on as normal. This lets you replace low resolution cutscenes with upscaled or remastered versions.

It started as the wrapper written for [WoWRevived](https://github.com/Thor110/WoWRevived) and has been made game independent:

- The game window, its size and whether it is fullscreen are detected automatically, and the video follows the window when it moves or resizes.
- The video keeps its own aspect ratio (black bars), or can be stretched.
- A missing replacement video simply ends the movie, so the game never gets stuck.
- The full Smacker API is exported with the correct calling conventions, so games that use other Smacker functions still load.
- Everything can be adjusted in `SmackIT.ini`.

## Usage

1. Build `Smackw32.dll` (Visual Studio, Win32 / x86, Release).
2. Back up the game's original `smackw32.dll`, then copy the new one over it.
3. Convert the game's `.smk` videos to `.mp4` (H.264 + AAC) with the same names, next to the originals or in a folder set with `VideoFolder`.
4. Optionally copy `SmackIT.ini` next to the DLL and change the settings. Set `Debug=1` to get a `Smackw32_log.txt` when something doesn't work.

## Settings (`SmackIT.ini`)

| Setting | Default | Meaning |
| --- | --- | --- |
| `Debug` | `0` | `1` writes `Smackw32_log.txt` next to the DLL |
| `VideoExtension` | `mp4` | Extension of the replacement videos |
| `VideoFolder` | | Extra folder checked first, by file name |
| `WindowTitle` / `WindowClass` | | Pick the game window by hand if the automatic choice is wrong |
| `Fullscreen` | `-1` | `-1` auto, `0` windowed, `1` fullscreen |
| `Stretch` | `0` | `1` fills the area instead of keeping the aspect ratio |
| `SkipKeys` | `0` | `1` lets Escape / Space / Enter / left click end a movie |
| `HideCursor` | `1` | Hide the mouse cursor during movies |
| `Volume` | `100` | Video volume, 0 to 100 |
| `X`, `Y`, `Width`, `Height` | `0` | Video area inside the game window (`0` size = whole window) |

## Limitations

- Games that draw on top of their movies, or read the decoded frames themselves (for example in-game video screens or talking heads), only get blank frames.
- Movies stored inside archive files (rather than as separate `.smk` files) can't be matched to a replacement video.
- Games running in exclusive fullscreen DirectDraw may draw over the overlay; a wrapper such as cnc-ddraw (windowed or borderless) avoids this.

# Supported Games

Note that games not on this list might not be supported.

- Jeff Wayne's The War of the Worlds (1998): WoWRevived : [This project has it's own version of the wrapper](https://github.com/Thor110/WoWRevived)

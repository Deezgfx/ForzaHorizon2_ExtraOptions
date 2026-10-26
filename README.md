# Forza Horizon 2 - Extra Options
A quality-of-life mod for the Xbox One version of Forza Horizon 2 that adds several tweaks to the game.

# Features
 - Intro Skip.
 - Change Car Anywhere.
 - Autodrive (as seen in Forza Horizon 6).
 - Drift Camera (as seen in Forza Horizon 4/5/6).
 - FOV Sliders.
 - Roof toggle for supported cars during free-roam.
 - Lights toggle for all cars.
 - Custom spawn locations.
 - All Stock Rims for All Cars.
 - All DLC Packs Unlocked.
 - VIP Status & Treasure Map Unlocks.
 - 4K Liveries w/ Enhanced Sampling.
 - Unrestricted Photo Mode.
 - Custom Gamerpic Support.
 - Rim Style category with all rims.
 - Minimap Follows Orbit Camera.
 - Azimuth's Save Data Tamper Checks Bypass.
 - "mediapc" Modding Folder Support.

# Information

Many of the features can now be toggled and tweaked inside the game itself through the native menus.

The treasure map is disabled by default. Open the INI and set "UnlockTreasureMap" to 1 if you wish to have it.

Autodrive is engaged with D-Pad Down on a controller or F2 on a keyboard.

Night lights can be toggled by just pressing the right-stick.

Roofs can be toggled by pressing and holding the right-stick for 3 seconds while stationary.

Important: The 3 cars with proper roof animations (BMW Z4 iDrive, Mazda MX-5 (2013), and Ferrari California) will look broken if you are using vanilla car model quality. Set "Car Detail" to High or Ultra in Azimuth.

Important #2: This is a rather experimental feature, so it doesn't look perfect in some cars due to the way the ForzaVista animations were authored.
The Lotus Exige, for example, will have its roof floating above the car. It's beyond the view of the camera but you will spot its shadow. This will be looked into in future updates.

# Installation

The installation is different depending on the translation layer you're using.

### Azimuth:

 - Extract all the 3 files from the "Azimuth" folder in this ZIP to "Azimuth\data\bin" and make sure to replace the existing DLL when prompted to.

### Mezcal:

 - Extract all the 3 files from the "Mezcal" folder in this ZIP to "Forza Horizon 2\Mount" which is where "forza_durango_release_final.exe" is located.

# Custom gamerpic 
 - You can edit the "Gamerpic.png" image located where you extracted the mod files to with any image you like and it will be used as your avatar in-game.
 - The maximum dimensions supported are 2048x2048 and the image must weight less than 16 megabytes.

# AI Usage Disclosure
Codex was used in this project to create the DLL. The actual research was done manually, mostly based on things I already toyed with in the other titles from the series, but I don't yet have the C language knowledge to code something like this fully from scratch (I was initially editing the raw executable). Leaving this here as I believe people have the right to know.

## FH2 Air Ride (experimental; first in-game test passed)

Live front/rear ride-height control for your own car: smooth travel, air-out, a return to the
spawned tune, five stance presets, and keyboard and controller bindings (`[AirRide]` in the
INI). It drives the game's own native tuning re-apply (`CCarDynamics` slot `0x238`) with the
car's own ride-height range. Every native function it calls is checked by its code bytes at
startup.

Defaults: Home/End front, Page Up/Down rear, Insert/Delete both, F5 air-out, F6 stock, F8/F7
presets, F9 full lift. On a controller: double-tap LB (L1) to air out, double-tap LB again for a
full lift, double-tap and hold LB to go back to the car's own ride height. Single presses are
left to the game. Air-out and lift go 50 % beyond the game's own limits (`ExtraDropPercent`,
`ExtraLiftPercent`; experimental).

Sound effects: an air-out blast, a vent hiss while lowering and a compressor while raising
(`Sounds`, `SoundVolume`). The mod generates them, and you can replace them with your own
`AirRide_AirOut.wav`, `AirRide_Down.wav` and `AirRide_Up.wav` beside the INI. They play through
Windows, not the game's audio. Not tested in game yet.

Requires Azimuth 0.9.10. Install `winmm.dll` and the INI next to `azimuth.exe`, and keep
Azimuth's original `azimuth-tls.dll` in `data\bin`. The code paths are verified against the
executable and covered by automated tests. Keyboard height changes are tested in game; the
controller double taps, extra drop/lift and sounds are not yet.
See `AIR_RIDE.md` for the evidence, the list of what is tested and what is not, and
troubleshooting.

`[Global] KeepRunningInBackground=1` stops the game pausing when its window loses focus (tested in game; off by default).

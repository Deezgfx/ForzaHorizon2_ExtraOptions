# Forza Horizon 2 - Extra Options
A quality-of-life mod for the Xbox One version of Forza Horizon 2 that adds several tweaks to the game.

# WARNING
An individual or group of people is trying to use Forza Horizon 2 and the mod's name to perform a scan that adds a keylogger to your computer. There is **NO** "All-In-One" installation tutorial and anything that requires you to insert and run things on your PowerShell is a **scam.**

Note: Issues will be set to "Contributors Only" at times where I am not available until I can sort out the situation with scams being spammed there. If you need assistance, you can find official threads for the mod in the ForzaMods and Mezcal Discord servers.

# Features
 - Change your car anywhere.
 - Autodrive (as seen in Forza Horizon 6).
 - Drift Camera (as seen in Forza Horizon 4/5/6).
 - FOV sliders for every camera type.
 - Roof toggle for supported cars during free-roam.
 - Lights, hazards, and turn-signal toggles for all cars.
 - New rim style category with all stock rims.
 - Custom spawn locations.
 - "mediapc" modding folder support.
 
# Enhancements
 - Skip developer opening videos.
 - All DLC packs properly unlocked.
 - VIP status & "Treasure Map" unlocks.
 - Toggle for 4K liveries w/ enhanced sampling.
 - Unrestricted Photo Mode.
 - "Return to Garage" button in the pause menu.
 - Aero tuning without equipping Forza Aero parts.
 - New rim style category with all aftermarket rims.
 - Toggle for having the minimap follow the orbit camera.
 - Bypass for Azimuth's save data checks.
 - Custom Gamerpic support.

# Installation

The installation is different depending on the translation layer you're using.

Azimuth:

 - Extract all the 3 files from the "Azimuth" folder in this ZIP to Azimuth's main folder where "Azimuth.exe" is located.

Mezcal:

 - Extract all the 3 files from the "Mezcal" folder in this ZIP to "Forza Horizon 2\Mount" which is where "forza_durango_release_final.exe" is located.

Note: Add this command if you're running Linux and using Wine: `WINEDLLOVERRIDES="winmm.dll=n,b" %command%`
More information on Linux support here: https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions/pull/35#issuecomment-6233015259

# Information

Most of the features can now be toggled and tweaked inside the game itself through the native menus.

### Treasure Map
The treasure map is disabled by default. Open the INI and set "UnlockTreasureMap" to 1 if you wish to have it.

### Autodrive
Autodrive is engaged by pressing D-Pad Down on a controller or F2 on a keyboard.

### Drift Camera
The drift camera can be toggled and tweaked in the HUD settings inside the game.
Cockpit drift camera is enabled by default.

### Vehicle Lights
Lights can be toggled by clicking the right-stick.

Hazards can be toggled by tapping D-Pad Up.

Turn signals can be engaged by holding D-Pad Left/Right for 1 second; They will de-engage once a turn is made and the steering is centered.

Note: Some cars have glitched turn signals. More info at the [mod's Wiki page.](https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions/wiki/4.-Oddities)

### Roofs
Roofs can be toggled by pressing and holding the right-stick for 3 seconds while stationary.

# Custom gamerpic 
You can edit the "Gamerpic.png" image located where you extracted the mod files to with any image you like and it will be used as your avatar in-game.
The maximum dimensions supported are 2048x2048 and the image must weight less than 16 megabytes.

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

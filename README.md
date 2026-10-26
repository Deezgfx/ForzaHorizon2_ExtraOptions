# Forza Horizon 2 - Extra Options
A quality-of-life mod for the Xbox One version of Forza Horizon 2 that adds several tweaks to the game.

> **This is an unofficial fork that adds [FH2 Air Ride](#fh2-air-ride) (live air suspension).**
> Extra Options itself is by Arthur (RokkuDayo): https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions.
> This build is Extra Options 0.2.98 with Air Ride included, so it replaces the official DLL; install one or the other, not both.
> Please report Air Ride problems here, not on the official repository.

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

# FH2 Air Ride

Lower and raise your car's ride height live in free roam, like real air suspension: slam it to
the ground, lift it up, or go back to your tune, with air-release sounds. It drives the game's
own native tuning re-apply (`CCarDynamics` slot `0x238`) inside the car's own ride-height range,
and every native function it calls is checked by its code bytes at startup.

**Download** the zip from this fork's [Releases](../../releases) page. It contains the DLL, the
INI, the sounds and `INSTALL_AND_USAGE.txt` with full instructions.

### Controls

| Action | Controller (LB = L1) | Keyboard |
| --- | --- | --- |
| Air out (drop to the floor) | Double-tap LB | F5 (press again to air back up) |
| Full lift | Double-tap LB again | F9 |
| Back to your car's own ride height | Double-tap LB and hold the 2nd press | F6 |
| Front / rear / both up and down | | Home/End, Page Up/Down, Insert/Delete |
| Stance presets | | F8 / F7 |

A single press of LB is left to the game. Works in free roam at 30 km/h or less by default.
All bindings and limits are in the `[AirRide]` section of `FH2_ExtraOptions.ini`.

**Your car needs adjustable suspension:** install the upgrade that unlocks Ride Height in the
Tuning menu (normally Race springs). Stock springs have no ride-height range.

### Install (Azimuth 0.9.10 only)

Copy `winmm.dll`, `FH2_ExtraOptions.ini` and the three `AirRide_*.wav` files next to
`azimuth.exe`. Keep Azimuth's **original** `azimuth-tls.dll` in `data\bin`. No scripts and no
PowerShell are needed; anything that asks you to run commands is not this mod.

### Sounds

`AirRide_AirOut.wav`, `AirRide_Down.wav` and `AirRide_Up.wav` (in `sounds/`) are cut from an air
release recording by freesound_community on Pixabay, used under the Pixabay Content License.
Replace them with any 16-bit PCM WAV, or delete them to use the mod's built-in sounds. They play
through Windows, so the in-game volume sliders don't affect them (`SoundVolume`, 0-100).

### Status

Tested in game on Azimuth 0.9.10: keyboard and LB double-tap height changes, sounds, and
`[Global] KeepRunningInBackground=1` (no pause menu when the window loses focus; off by
default). Experimental: `ExtraDropPercent` / `ExtraLiftPercent` (going beyond the game's own
ride-height limits) and adjusting while moving. See `AIR_RIDE.md` for the evidence, the full list
of what is tested and troubleshooting, and `BUILD.md` to build it.

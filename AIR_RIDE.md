# FH2 Air Ride

Live ride-height control for your own car in Forza Horizon 2 (Xbox One build, PC via Azimuth),
built into Extra Options. Front and rear move independently and smoothly. You also get an
air-out toggle, a return to the car's spawned tune, five stance presets, and keyboard and
controller bindings.

Air ride does not invent a physics value. It uses the game's own tuning path: the
same three steps the native tuning code runs when you change ride height in the game.

## Status: what is tested and what is not

| Item | Status |
| --- | --- |
| Game executable is the supported build (SHA-256 `3ec04c61…f0a3`) | **Tested** (hash and the existing verification code) |
| Every native function, offset and vtable slot below exists, with the expected code | **Tested**: the real DLL checks them against your mapped executable (`tests/run_dll_tests.py`) |
| The full Extra Options patch transaction (all existing patch sites) still applies with air ride included | **Tested** on the real executable image at its real base |
| INI parsing, bounds, defaults, older INI files without `[AirRide]` | **Tested** (`tests/run_dll_tests.py`) |
| Controller logic: smooth travel, independent axles, hold-repeat, air-out and air-up, full lift, stock, presets, blocking, extra-drop floor and extra-lift ceiling | **Tested** (`tests/air_suspension_test.c`) |
| Double-tap detection: single presses and long holds ignored, slow taps rejected, double-tap-and-hold | **Tested** (`tests/pad_gesture_test.c`) |
| LB double tap air-out / lift in game | **Tested in game** (player confirmed). Double-tap-and-hold LB for stock: not tested in game yet. |
| Windows DLL builds for Azimuth and Mezcal with `-Wall -Wextra -Werror` | **Tested** (Zig 0.15.2) |
| The car visibly changes height in game | **Tested in game** (Azimuth 0.9.10, 1997 BMW M3 with race springs, parked in free roam). Keyboard air-out, air-up, front/rear steps and stock all changed the live ride-height fields; the player confirmed it visually. |
| Calling the native re-apply while driving (up to `MaxAdjustSpeedKmh`) behaves well | **Not tested** |
| Thread safety of calling the re-apply from the car update | **Not verified** |
| Controller bits other than D-pad Down/Left/Right and RS (`0x2/0x4/0x8/0x80`, already used by Extra Options) follow the XInput layout | **Assumed** |
| The velocity vector used for the speed limit is in metres per second | **Assumed** |
| `ExtraDropPercent` / `ExtraLiftPercent` (below the native minimum / above the native maximum) | **Experimental, not tested in game.** 50 by default. |
| Azimuth adapter | **Built for Azimuth 1.0.1 / 1.0.2** (Extra Options 0.3.2 adapter; see Compatibility). Air Ride on this base is **not yet tested in game**. |
| Sound synthesis, custom WAV checks, choosing which sound plays | **Tested** (`tests/air_sound_test.c`) |
| The DLL loads the system `PlaySoundW` and plays the built-in and custom sounds | **Tested** outside the game (`tests/run_dll_tests.py`, real playback at low volume) |
| Sounds play in game while adjusting | **Not tested in game yet** |

## Sounds (`[AirRide] Sounds`, `SoundVolume`)

- **Air-out:** a valve pop, then a loud hiss that fades as the pressure drops. The fade plays out
  even after the car has settled.
- **Lowering:** a vent hiss for as long as the car is moving down. It cuts off when the car stops,
  like a valve closing.
- **Raising:** a compressor buzz for as long as the car is moving up.

Full lift plays the raising sound, and going back to the car's own tune plays the raising or
lowering sound depending on the direction.

The release package ships `AirRide_AirOut.wav`, `AirRide_Down.wav` and `AirRide_Up.wav`, cut
from a recorded air release (freesound_community via Pixabay, Pixabay Content License). Without
those files the mod falls back to built-in sounds it generates itself. They play through
Windows (`PlaySoundW` from the system `winmm.dll`, loaded once your car is loaded, never during
startup). The game's audio engine is not used, so:

- they are not 3D and don't depend on the camera;
- the in-game volume sliders don't affect them (use `SoundVolume`, 0–100);
- only one plays at a time.

If the game stops updating your car (for example, a pause menu), a sound plays to the end of its
clip (at most 1.7 s) and doesn't repeat.

To use your own sounds, put any of `AirRide_AirOut.wav`, `AirRide_Down.wav` or `AirRide_Up.wav`
beside the INI. Each must be 16-bit PCM, mono or stereo, 8–96 kHz, and 4 MB or less. The lowering
and raising sounds repeat while the car keeps moving, so short steady clips work best. The log
says which custom files were used. A file that isn't accepted is replaced by the built-in sound.

## How it works (evidence)

All addresses are RVAs in `forza_durango_release_final.exe` (timestamp `0x565501ED`, size
58,204,160). They were found by disassembling that file with MSVC `dumpbin /disasm`, by RTTI,
and by the game's own debug strings. Nothing was taken from other games or guessed.

| RVA / offset | What it is | Evidence |
| --- | --- | --- |
| `0x10613F0` | Loads the front and rear `List_SpringDamperPhysics` rows (`SELECT * FROM List_SpringDamperPhysics WHERE SpringDamperPhysicsID=`). | String xrefs at `0x1061468` and `0x10615AB`. Uses the `FrontSpringDamperPhysicsID` / `RearSpringDamperPhysicsID` columns. |
| `0x1061810` | Parses one spring/damper row: `DefRideHeight` → `+0x0C`, `MinRideHeight` → `+0x10`, `MaxRideHeight` → `+0x14`, `MaxCompressHeight` → `+0x18`, … row size `0x90`. | Column-name string immediately before each `vmovss [rsi+N]`. |
| `0x1056210` | Builds the car physics description (`SELECT * FROM Data_Car WHERE Id=`). Puts the front spring row at `desc+0x324` and the rear at `desc+0x3B4`; `FrontStockRideHeight` → `desc+0x38E0`. | Call `0x10577A6`: `lea rcx,[rsi+324h]; call 0x10613F0`. |
| `ForzaCar+0x1200` | The car's embedded physics description. | `0xF605C0` is `lea rax,[rcx+1200h]; ret`. `0xF67824` passes `car+0x1200` to the builder. |
| `ForzaCar+0x4C08` | `CCarDynamics` interface pointer (RTTI `TRefCountedObjectThreadSafe<CCarDynamics>`, vtable `0x2EB5E18`; object vtable `0x2EB5D68` at `-0x3A0`). | `0xF605D0` is `mov rax,[rcx+4C08h]; ret`. Extra Options' existing velocity code reads the same pointer (`car+0x20+0x4BE8`). |
| `0x12E3680` | Resolves the tuning block `desc+0x3800`. For each axle, `0x12E3A90` maps the tuning value with `DefRideHeight/Min/Max` into a 0..1 position. | Its helper `0x12C3E60` names this value `"RideHeight"` (string `0x22224D8`, error `"%s: val=%1.2f min=%1.2f max=%1.2f"`). Front lands at `desc+0x3848`, rear at `desc+0x3874`. |
| `0x12E3D20` | Suspension geometry: ride height = `Min + (Max − Min) × desc+0x3848` (front; rear uses `+0x3874`). | `vsubss [r15+10h]; vmulss [rdx+3848h]; vaddss [r15+10h]`. |
| Interface slot `0x238` → `0x12DA500` → `0x12DA510` | `CCarDynamics` live re-apply. It re-resolves the tune, re-runs suspension and chassis setup **without** clearing wheel state, and keeps the body transform. | Disassembly; vtable `0x2EB5E18+0x238 = 0x12DA500`. |
| `0x173C0C5` | The game's own tuning apply. It writes the tuning fields into `desc` (ride height to `+0x3848/+0x3874`), calls `0x12E3680(desc, desc+0x3800)`, then `dyn->vtbl[0x238](dyn, desc, 3)`. | Disassembly. `0x174D8DB` makes the same call. |

Air ride repeats that sequence for the local car only:

1. It gets the local car from the existing Extra Options `ForzaCar` update hook (no new byte
   patch is installed).
2. It validates both vtables, the spring rows (finite, `Min < Max`), the tuning positions
   (0..1), and that the fields are writable.
3. It writes the new 0..1 positions to `desc+0x3848` and `desc+0x3874`.
4. It calls `0x12E3680(desc, desc+0x3800)`, then `vtbl[0x238](dyn, desc, 3)`.

At startup the DLL compares the code bytes of every function it calls, and the two vtable slots
it uses. If anything differs, air ride turns itself off and logs why. The rest of Extra Options
keeps working.

Positions are always inside the car's own tuning range, so 0 % is the lowest ride height the
tuning menu allows and 100 % the highest. `ExtraDropPercent` (experimental) lowers
`MinRideHeight` (`+0x10` of the spring row) and `ExtraLiftPercent` (experimental) raises
`MaxRideHeight` (`+0x14`) in the car's own description copy, so the same geometry formula reaches
lower or higher. Returning inside the native range restores the original values.

### Safety rules in the code

- Only your own car (`autodrive_game_get_car`) is touched. AI and other cars are never changed.
- By default it adjusts only in free roam (`FreeRoamOnly=1`), never in a car meet, never while
  the window is unfocused, and only at or below `MaxAdjustSpeedKmh`.
- The re-apply runs at most every `ApplyIntervalMs` while the car moves towards a target, and
  not at all once it has settled.
- If the game changes the tune itself (tuning menu, respawn), the game's values win and become
  the new stock. If the game keeps rewriting them, air ride stops for that car.
- An input held while adjustment is blocked does nothing until you release and press it again.

## Controls (defaults, all configurable)

| Action | Keyboard | Controller (LB = L1) |
| --- | --- | --- |
| Air out (drop to the lowest height) | F5 (toggle: again airs back up) | Double-tap LB |
| Full lift (raise to the highest height) | F9 | Double-tap LB again while aired out |
| Back to the car's own tuned ride height | F6 | Double-tap and hold LB (second press held ~0.2 s) |
| Front up / down | Home / End | — |
| Rear up / down | Page Up / Page Down | — |
| Both up / down | Insert / Delete | — |
| Next / previous preset | F8 / F7 | — |

On the keyboard, tap to move one step (`StepPercent`) and hold for continuous change after
0.3 s (`HoldSpeed`).

On the controller a single press or a normal hold does nothing, so LB keeps working for
driving. A double tap is two presses each shorter than `DoubleTapMs` (default 300 ms) with a gap
shorter than that. The game still sees the presses, so whatever LB does in your control layout
also happens. RB is not used by default because it changes the camera in game (tested).
`PadDoubleTapLower`, `PadDoubleTapRaise` and `PadDoubleTapStock` (optional, off) pick the
buttons. If Lower and Raise are on different buttons, double tap lowers or lifts fully and
double-tap-and-hold lowers or raises smoothly while held.

The older hold-a-modifier combos (`PadModifier` with `PadFrontUp` … `PadLift`) are still there
but unbound by default, because the game also reacted to those buttons. While a bound combo
modifier is held, Extra Options' D-pad blinkers, radio change and autodrive toggle stand aside.

### How low and how high (`ExtraDropPercent`, `ExtraLiftPercent`)

Both default to 50: air-out goes 50 % of the car's native ride-height range below the lowest
height the tuning menu allows, and full lift goes 50 % of the range above the highest. 0 keeps
the game's own limits; the maximum is 100. **Not tested in game:** how the physics behaves
outside the native range (the body may touch the road when very low). Lower the values if a
car misbehaves.

Default presets (front/rear % of range): 1 Slammed 0/0, 2 Rake 15/30, 3 Low 30/30,
4 Mid 50/50, 5 Lifted 100/100.

`RememberStance=1` keeps the last stance per car model for the current session and restores it
when that car spawns again. Nothing is written to your save. The saved tune in the game is not
modified.

## Compatibility

- Game: only `forza_durango_release_final.exe` with SHA-256
  `3ec04c6163fb5614e3d351ba0570d39b846041a257715d6340eceb03ccb7f0a3`.
- Azimuth: this source's adapter (`adapters/azimuth.c`, from Extra Options 0.3.2) accepts only
  the Azimuth 1.0.1 and 1.0.2 `azimuth.exe` builds it lists by size and SHA-256 (1.0.2 is
  9,715,712 bytes). With any other build it shows a warning and patches nothing.

## Installation (Azimuth 1.0.2)

1. Back up your Azimuth folder.
2. In `Azimuth\data\bin`, make sure `azimuth-tls.dll` is Azimuth's **original** file from the
   Azimuth 1.0.2 package. Older Extra Options builds (for example 0.2.68, 154,624 bytes)
   replaced it. The adapter needs Azimuth's own TLS helper.
3. Copy `winmm.dll` and `FH2_ExtraOptions.ini` next to `azimuth.exe` (the Azimuth root
   folder), and move your `Gamerpic.png` there too. `azimuth.exe` imports `WINMM.dll`
   directly, so the proxy must sit beside it. The INI, log and gamerpic are read from the DLL's
   own folder.
   If you have an older `FH2_ExtraOptions.ini`, keep it: air ride uses defaults when
   `[AirRide]` is missing. You can also copy the `[AirRide]` section from the supplied INI.
4. Start the game, drive in free roam, and check `FH2_ExtraOptions.log`. You should see
   `AIRRIDE: native live-tuning path verified` and then `AIRRIDE: bound local car model …`.

Uninstall: delete `winmm.dll` from the Azimuth root.

## Troubleshooting

- No `FH2_ExtraOptions.log` beside `winmm.dll`: the proxy did not load. Check that it is beside
  `azimuth.exe`.
- A message box about an unsupported Azimuth build: you are not on Azimuth 1.0.1 or 1.0.2.
- `AIRRIDE: native tuning code did not match…`: wrong game build. Air ride is off; the rest of
  the mod still works.
- `adjustment waits for free roam and the configured speed limit`: slow down, or raise
  `MaxAdjustSpeedKmh`.
- No sounds: check `Sounds=1` and `SoundVolume` above 0, and look for `AIRSOUND:` lines in the
  log. Windows' own volume mixer lists the sounds under the game's process.
- If the car jolts or misbehaves when adjusting while moving, set `MaxAdjustSpeedKmh=0` and
  raise `ApplyIntervalMs`. Then please report what happened, with the log.

## Build and test

See `BUILD.md`. Short version on Windows:

```text
powershell -ExecutionPolicy Bypass -File scripts\build_and_test.ps1 -Zig C:\path\zig.exe -Python C:\path\python.exe -GameExe "C:\path\to\forza_durango_release_final.exe"
```

## Requirement: an adjustable suspension

Air ride moves the car inside its own ride-height range. With stock springs the game sets MinRideHeight = MaxRideHeight (confirmed by reading a live session: 28 of 30 loaded cars had a zero range). On those cars nothing can move, and the log says `local car ride-height data failed validation`. Install the upgrade that unlocks Ride Height in the tuning menu (normally Race springs), then respawn the car.


## Keep running in the background (`[Global] KeepRunningInBackground`)

**Tested in game** (Azimuth 0.9.10): with the option on, the simulation stayed running for 5 s with the window in the background, and no pause menu appeared when focus came back.

How it works: `0xF6C3C0` registers `0xF6DBA0` as the game's `CoreWindow.Activated` handler (`ICoreWindow` slot `0x78`). The handler reads `WindowActivationState` (args slot `0x30`) and posts it to the game's message queue as message `0x1A/6`. When the window loses focus, Azimuth raises it with state 1 (*Deactivated*), and the game answers by opening the pause menu. With the option on, the handler pointer loaded at `0xF6C617` (the only reference to `0xF6DBA0`) is redirected to a thunk in unused padding at `0xF6DC64`. That drops only the Deactivated notification; the other states pass through unchanged. Off by default. Both patch sites are validated byte for byte like every other Extra Options patch.

Note: the game keeps simulating while you're tabbed out, so pause it yourself if you leave the car somewhere unsafe. If Azimuth keeps delivering controller input to an unfocused window, the game will react to it. Air ride itself only reads input while the game window is focused.

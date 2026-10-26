# Building and testing

Use Python 3.10 or later and Zig 0.15.2. Run these commands from this source
folder. Builds target x86_64 Windows PE DLLs and use only Zig's bundled Windows
headers and Kernel32 import library. No Microsoft C runtime, Xbox SDK, original
helper DLL, or translation-layer source is required.

## Release builds

```text
python build.py /absolute/path/to/zig.exe
python build.py /absolute/path/to/zig.exe --azimuth
```

The outputs are `../xg.dll` for Mezcal and `../winmm.dll` for Azimuth 0.9.10 (the Azimuth
adapter in this source is a `winmm.dll` proxy loaded beside `azimuth.exe`).
`--output PATH` chooses another destination. `--build-dir PATH` keeps the
intermediate files in that directory; otherwise they use a temporary directory.
The script lists common modules explicitly and selects one layer adapter.
Azimuth also includes its guarded host, livery and save compatibility modules.
All modules compile with warnings treated as errors and x64 unwind tables.

On Linux, use the portable Linux Zig distribution and the same commands with
`zig` as the compiler filename. The target remains a Windows DLL. Cross-build
support is retained; Linux/Wine execution has not been tested.

The public exports are `Fh2TweaksStatus` on both adapters and the additional
`azimuth_tls_index` / `azimuth_tls_attached` data exports on Azimuth. These names
are retained for compatibility. Azimuth has a 336-byte zero TLS template aligned
to 16 bytes and no TLS callbacks. The release binaries import Kernel32 only.
The gamerpic feature lazily loads the system Windows codec on a later UI call.
Autodrive loads User32 lazily on a native GPS update to read F2 and focus.
Controller buttons and telemetry visibility are read from native game state;
the cinematic transition calls the game's existing white-flash routine.

Both adapters initialize synchronously in `DllMain`. Startup performs simple
configuration I/O, hashing, mapped-image validation, and memory patching.
Callback resource work and PNG decoding happen on normal game call paths.
There is no worker thread or startup wait. Callback installation pins the DLL
before any callback pointer is written.

## Tests

Windows, everything at once (all builds, controller tests, DLL integration tests):

```text
powershell -ExecutionPolicy Bypass -File scripts\build_and_test.ps1 -Zig C:\path\zig.exe -Python C:\path\python.exe -GameExe C:\path\forza_durango_release_final.exe
```

Or individually:

```text
zig cc -std=c11 -Wall -Wextra -Werror -Wno-division-by-zero tests/air_suspension_test.c features/air_suspension.c -o air_suspension_test.exe
air_suspension_test.exe
zig cc -std=c11 -Wall -Wextra -Werror tests/pad_gesture_test.c features/pad_gesture.c -o pad_gesture_test.exe
pad_gesture_test.exe
zig cc -std=c11 -Wall -Wextra -Werror tests/air_sound_test.c features/air_sound_synth.c features/air_suspension.c -o air_sound_test.exe
air_sound_test.exe C:\some\folder
python build.py C:\path\zig.exe --azimuth --test
python tests/run_dll_tests.py ..\fh2-extra-options-test.dll C:\path\forza_durango_release_final.exe
```

`tests/run_dll_tests.py` maps the game executable into memory section by section, at its
preferred base when possible (otherwise it applies relocations). It never executes game code.
It then checks the air ride signatures, runs the whole patch transaction against that image,
and checks INI parsing. It also plays the air ride sounds once through the system `PlaySoundW`
at low volume (about 4 s). `air_sound_test.exe` with a folder argument writes the built-in
sounds there as WAV files you can listen to. Set `FH2_USER_INI` to an existing INI to also check that it still loads.

The air ride feature adds no byte patches or runtime hooks. It runs inside the existing
`local_car_vehicle_controls_update` hook and calls native functions only after `air_ride_bind`
has matched their code bytes. `KeepRunningInBackground` adds one fixed patch
(`window_activation_handler`) and one padding thunk (`window_activation_filter_thunk`) in the
`BACKGROUND` group, both applied only when the option is on. See `AIR_RIDE.md`.

## Patch data

Edit `patches.json`, then regenerate the checked-in C tables:

```text
python generate_data.py
```

The generator validates non-overlapping file/image ranges, supported groups,
and equal-length byte variants. It emits `patches_generated.c` and
`patches_generated.h`. You can format these with clang-format afterward.
It needs no game executable. Runtime patches use RVAs; file verification uses
raw offsets. The fifteen runtime callback descriptors live in the feature
modules and are described in `runtime_hooks` in the manifest.

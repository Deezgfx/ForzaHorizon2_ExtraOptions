"""Integration checks for the Windows test DLL (build with: python build.py zig.exe --azimuth --test).

Usage: python tests/run_dll_tests.py PATH/fh2-extra-options-test.dll PATH/forza_durango_release_final.exe

The game executable is mapped into memory section by section (no code is executed), then the
real DLL code verifies the native air ride signatures, runs the complete in-memory patch
transaction against that image, and parses INI files.
"""

import ctypes
import os
import shutil
import struct
import sys
import tempfile
from ctypes import wintypes
from pathlib import Path

SOURCE = Path(__file__).resolve().parent.parent
MEM_COMMIT_RESERVE = 0x3000
PAGE_READWRITE = 0x04
failures = 0


def check(condition, label):
    global failures
    print(("PASS " if condition else "FAIL ") + label)
    if not condition:
        failures += 1


def map_image(exe_path):
    data = Path(exe_path).read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    sections = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    image_size = struct.unpack_from("<I", data, optional + 56)[0]
    header_size = struct.unpack_from("<I", data, optional + 60)[0]
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.VirtualAlloc.restype = ctypes.c_void_p
    kernel32.VirtualAlloc.argtypes = [ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD, wintypes.DWORD]
    preferred = struct.unpack_from("<Q", data, optional + 24)[0]
    # Azimuth maps the game at its preferred base; try that first, otherwise relocate.
    base = kernel32.VirtualAlloc(preferred, image_size, MEM_COMMIT_RESERVE, PAGE_READWRITE)
    if not base:
        base = kernel32.VirtualAlloc(None, image_size, MEM_COMMIT_RESERVE, PAGE_READWRITE)
    if not base:
        raise OSError("VirtualAlloc failed")
    ctypes.memmove(base, data, header_size)
    table = optional + optional_size
    for index in range(sections):
        entry = table + index * 40
        virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from("<IIII", data, entry + 8)
        count = min(raw_size, virtual_size) if virtual_size else raw_size
        ctypes.memmove(base + virtual_address, data[raw_pointer:raw_pointer + count], count)
    delta = base - preferred
    relocation_rva, relocation_size = struct.unpack_from("<II", data, optional + 112 + 5 * 8)
    if delta and relocation_rva:
        cursor, end = relocation_rva, relocation_rva + relocation_size
        while cursor < end:
            page, block = struct.unpack_from("<II", ctypes.string_at(base + cursor, 8))
            if block < 8:
                break
            entries = ctypes.string_at(base + cursor + 8, block - 8)
            for (item,) in struct.iter_unpack("<H", entries):
                if item >> 12 == 10:  # IMAGE_REL_BASED_DIR64
                    slot = ctypes.c_uint64.from_address(base + page + (item & 0xFFF))
                    slot.value = (slot.value + delta) & 0xFFFFFFFFFFFFFFFF
            cursor += block
    print(f"mapped image at 0x{base:x} (preferred 0x{preferred:x})")
    return base, image_size


def main():
    dll_path, exe_path = sys.argv[1], sys.argv[2]
    dll = ctypes.CDLL(str(Path(dll_path).resolve()))
    dll.Fh2TestOptionOffset.restype = ctypes.c_int
    dll.Fh2TestOptionOffset.argtypes = [ctypes.c_char_p]
    dll.Fh2TestOptionsSize.restype = ctypes.c_uint
    dll.Fh2TestReadOptions.argtypes = [ctypes.c_wchar_p, ctypes.c_void_p]
    dll.Fh2TestVerifyFile.argtypes = [ctypes.c_wchar_p]
    dll.Fh2TestAirRideBind.argtypes = [ctypes.c_void_p]
    dll.Fh2TestApply.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    dll.Fh2TweaksStatus.restype = ctypes.c_char_p

    size = dll.Fh2TestOptionsSize()
    options = (ctypes.c_ubyte * size)()

    def value(name):
        offset = dll.Fh2TestOptionOffset(name.encode())
        if offset < 0:
            raise KeyError(name)
        return struct.unpack_from("<i", bytes(options), offset)[0]

    def read(directory):
        return dll.Fh2TestReadOptions(str(directory), options)

    # Game executable identity (existing Extra Options verification code).
    check(dll.Fh2TestVerifyFile(str(exe_path)) == 1, "game executable matches the supported build")

    # Native air ride signatures against the mapped executable.
    base, image_size = map_image(exe_path)
    check(dll.Fh2TestAirRideBind(base) == 1, "air ride native tuning signatures verified")
    original = ctypes.c_ubyte.from_address(base + 0x12DA500).value
    ctypes.c_ubyte.from_address(base + 0x12DA500).value = original ^ 0xFF
    check(dll.Fh2TestAirRideBind(base) == 0, "air ride refuses a modified re-apply thunk")
    ctypes.c_ubyte.from_address(base + 0x12DA500).value = original
    slot = ctypes.c_uint64.from_address(base + 0x2EB5E18 + 0x238)
    saved_slot = slot.value
    slot.value = base + 0x12DA510
    check(dll.Fh2TestAirRideBind(base) == 0, "air ride refuses a redirected vtable slot")
    slot.value = saved_slot
    check(dll.Fh2TestAirRideBind(base) == 1, "air ride verification restored")

    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)

        # Defaults with no INI.
        check(read(directory) == 1, "missing INI loads defaults")
        check(value("AirRideEnabled") == 1 and value("AirRidePadModifier") == 0x100,
              "air ride defaults")
        check(value("AirRideKeyFrontUp") == 0x24 and value("AirRidePreset2Rear") == 30,
              "air ride default bindings and presets")
        check(value("AirRidePadDoubleTapLower") == 0x100 and
              value("AirRidePadDoubleTapRaise") == 0x100 and
              value("AirRidePadDoubleTapStock") == 0 and value("AirRideDoubleTapMs") == 300 and
              value("AirRidePadRearUp") == 0 and value("AirRideKeyLift") == 0x78,
              "air ride default double-tap bindings, modifier combos unbound")
        check(value("AirRideExtraDropPercent") == 50 and value("AirRideExtraLiftPercent") == 50,
              "air ride default extra drop and lift")

        # Full patch transaction against the mapped image with default options.
        result = dll.Fh2TestApply(base, options)
        status = dll.Fh2TweaksStatus().decode(errors="replace")
        check(result == 1, "complete patch transaction succeeds on the real image: " + status)

        # Focus-loss pause filter: both sites stay original when off, both change when on.
        lea = ctypes.string_at(base + 0xF6C617, 7).hex()
        pad = ctypes.string_at(base + 0xF6DC64, 12).hex()
        check(lea == "488d0d82150000" and pad == "cc" * 12,
              "focus-loss pause filter left alone by default")
        background = dll.Fh2TestOptionOffset(b"KeepRunningInBackground")
        check(background >= 0, "KeepRunningInBackground setting exists")
        struct.pack_into("<i", options, background, 1)
        result = dll.Fh2TestApply(base, options)
        lea = ctypes.string_at(base + 0xF6C617, 7).hex()
        pad = ctypes.string_at(base + 0xF6DC64, 12)
        check(result == 1 and lea == "488d0d46160000" and pad[:2] == b"\x48\xb8"
              and pad[10:] == b"\xff\xe0", "focus-loss pause filter installed when enabled")
        struct.pack_into("<i", options, background, 0)
        result = dll.Fh2TestApply(base, options)
        check(result == 1 and ctypes.string_at(base + 0xF6C617, 7).hex() == "488d0d82150000"
              and ctypes.string_at(base + 0xF6DC64, 12).hex() == "cc" * 12,
              "focus-loss pause filter removed again when disabled")

        ini = directory / "FH2_ExtraOptions.ini"
        shutil.copy(SOURCE / "FH2_ExtraOptions.ini", ini)
        check(read(directory) == 1, "shipped INI parses: " + dll.Fh2TweaksStatus().decode())
        check(value("AirRideKeyBothUp") == 0x2D and value("AirRidePadRearUp") == 0 and
              value("AirRidePadDoubleTapLower") == 0x100 and
              value("AirRidePadDoubleTapRaise") == 0x100 and
              value("AirRidePadDoubleTapStock") == 0 and value("AirRideKeyLift") == 0x78,
              "shipped INI bindings")
        check(value("AirRideMaxAdjustSpeedKmh") == 30 and value("AirRideExtraDropPercent") == 50 and
              value("AirRideExtraLiftPercent") == 50, "shipped INI limits")
        check(value("Enable4K") == 1 if dll.Fh2TestOptionOffset(b"Enable4K") >= 0 else True,
              "existing options still parse")

        user_ini = Path(os.environ.get("FH2_USER_INI", ""))
        if user_ini.is_file():
            shutil.copy(user_ini, ini)
            check(read(directory) == 1, "existing user INI without [AirRide] still loads")
            check(value("AirRideEnabled") == 1, "existing user INI gets air ride defaults")

        def expect(text, accepted, label):
            ini.write_text(text, encoding="utf-8")
            check(read(directory) == accepted, label)

        expect("[AirRide]\nKeyFrontUp=255\nPadModifier=0xFFFF\n", 1, "upper bounds accepted")
        expect("[AirRide]\nKeyFrontUp=0x100\n", 0, "virtual key above 255 refused")
        expect("[AirRide]\nPreset1Front=101\n", 0, "preset above 100 refused")
        expect("[AirRide]\nStepPercent=0\n", 0, "zero step refused")
        expect("[AirRide]\nApplyIntervalMs=5\n", 0, "too-short apply interval refused")
        expect("[AirRide]\nMaxAdjustSpeedKmh=fast\n", 0, "non-numeric value refused")
        expect("[AirRide]\nMaxAdjustSpeedKmh=-5\n", 0, "negative value refused")
        expect("[AirRide]\nKeyFrontUp=0x\n", 0, "empty hex refused")
        expect("[AirRide]\nUnknownKey=1\n", 0, "unknown air ride key refused")
        expect("[AirRide]\nEnabled=2\n", 0, "boolean outside 0/1 refused")
        expect("[AirRide]\nKeyFrontUp=99999999999\n", 0, "overflowing value refused")
        expect("[AirRide]\nPresetCount=6\n", 0, "preset count above capacity refused")
        expect("[AirRide]\nExtraDropPercent=50\nKeyAirOut=0x0D\n", 1, "custom values accepted")
        check(value("AirRideExtraDropPercent") == 50 and value("AirRideKeyAirOut") == 0x0D,
              "custom values stored")
        expect("[AirRide]\nExtraLiftPercent=101\n", 0, "extra lift above 100 refused")
        expect("[AirRide]\nDoubleTapMs=100\n", 0, "too-short double-tap window refused")
        expect("[AirRide]\nPadDoubleTapLower=0x4000\nDoubleTapMs=450\n", 1,
               "double-tap settings accepted")
        check(value("AirRidePadDoubleTapLower") == 0x4000 and value("AirRideDoubleTapMs") == 450,
              "double-tap settings stored")
        expect("[AirRide]\nSoundVolume=101\n", 0, "sound volume above 100 refused")
        expect("[AirRide]\nSounds=0\nSoundVolume=35\n", 1, "sound settings accepted")
        check(value("AirRideSounds") == 0 and value("AirRideSoundVolume") == 35,
              "sound settings stored")

        # Air ride sounds through the real system PlaySoundW (quiet, about 4.3 s in total).
        lengths = (ctypes.c_uint * 4)()
        sounds = directory / "sounds"
        sounds.mkdir()
        dll.Fh2TestAirSound.argtypes = [ctypes.c_wchar_p, ctypes.c_int, ctypes.c_void_p]
        played = dll.Fh2TestAirSound(str(sounds), 5, lengths)
        check(played == 0b1110 and list(lengths[1:]) == [1700, 1200, 1400],
              f"built-in sounds play through PlaySoundW ({played:#x}, {list(lengths[1:])})")
        # A custom 0.25 s stereo WAV replaces the air-out sound; a broken one falls back.
        frames = 11025
        data = struct.pack("<hh", 300, -300) * frames
        header = (b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " +
                  struct.pack("<IHHIIHH", 16, 1, 2, 44100, 44100 * 4, 4, 16) +
                  b"data" + struct.pack("<I", len(data)))
        (sounds / "AirRide_AirOut.wav").write_bytes(header + data)
        (sounds / "AirRide_Up.wav").write_bytes(b"RIFF\x00\x00\x00\x00WAVEjunk")
        played = dll.Fh2TestAirSound(str(sounds), 5, lengths)
        check(played == 0b1110 and list(lengths[1:]) == [250, 1200, 1400],
              f"custom WAV used, broken WAV falls back ({played:#x}, {list(lengths[1:])})")

    print(f"{failures} failure(s)" if failures else "all DLL integration checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

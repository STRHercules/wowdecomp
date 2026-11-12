# BodySliderPatch.exe

Small native Windows patcher; no Python or VC runtime installation is needed.

Double-click it and select the supported original build 12340 `Wow.exe`. It
creates `Wow-BodySlider.exe` in the same folder. It never changes the selected
file or overwrites an existing output. Close WoW, preserve your original, then
rename the new copy to `Wow.exe` when installing the DLL/addon. Selecting an
unchanged BodySlider-patched copy instead creates `Wow-original.exe`.

The executable is accepted by exact SHA-256, not just its version string:
`aa63a5750d60ef16746c686b3d5e26876d98953eab08b1c026cd0faf78e88cb8`.
Unknown variants and files changed after patching are rejected.

For scripted use:

```powershell
BodySliderPatch.exe --patch input/Wow.exe output/Wow.exe
BodySliderPatch.exe --restore patched/Wow.exe restored/Wow.exe
```

The file format is byte-for-byte compatible with the Python reference patcher.
The native program embeds only 26 byte changes and 2486 appended bytes, not the
original executable. Both input and generated output SHA-256 are verified before
exclusive output creation. Windows Unicode file APIs preserve non-ASCII paths.

Build and test from the repository root:

```powershell
cmake -S Tools/BodySlider/patcher-native -B build/body-slider-patcher -A Win32
cmake --build build/body-slider-patcher --config Release
python Tools/BodySlider/patcher-native/test_native_patcher.py
```

`PatchData.h` is generated from the supported original with
`python Tools/BodySlider/patcher-native/generate_patch_data.py D:/Wow.exe`.
Regenerate it and rerun tests whenever the reference loader format changes.

The automated tests cover the command-line path. An interactive smoke check
verified that the native file picker opens correctly and Cancel closes it without
writing files. Selecting a file and completing patching through that dialog has
not been tested; its conversion function is the same one used by the CLI.
Realm acceptance of the resulting modified client is independent of whether
patching and restoration succeed.

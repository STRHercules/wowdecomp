# Volumetric clouds

Video Options → Effects → **Volumetric Clouds**. Off by default. The saved
`skyClouds` CVar accepts `0` (existing clouds) or `1` (volumetric clouds).
It works independently of the Visuals dropdown. Apply, Okay, Cancel and Defaults
use the existing video options control lifecycle. `skyCloudsInfo` reports the
renderer status, including resource failures and unsupported backends.

The cloud layer comes from the Manastorm share pack at
`G:\Downloads\HDWoWModels\VolumetricClouds` (2026-11-15). The source and generated
shaders are under `WoW/Source/WorldClient/Clouds/`; the download is not needed at
build time or runtime. Only cloud code was ported, rather than replacing donor
integration files wholesale.

The Vulkan GxEffects path generates tileable Perlin-Worley volumes asynchronously,
ray marches a 1536 × 768 panorama a sixteenth per refresh, and draws the clouds
over the existing sky. Teleports and abrupt sun changes trigger a full refresh.
Zone colours, cloud density, weather, sun/moon light and lightning drive the
appearance. A copied depth buffer and the current sky portal rectangle restrict
the draw. The existing cloud layer remains until resources are ready, when a
pass fails, or on a backend without GxEffects. Switching off restores it.

This port keeps the current atmosphere, stars, sun, moon, custom zone skyboxes
and fog. The donor's replacement atmosphere, ground cloud shadows and cloud
light shafts are not included. The panorama uses about 18 MiB plus noise,
lighting LUT and a screen-sized depth copy. The first complete refresh costs
more than subsequent slices.

## Verification

- All three HLSL shaders compile to Vulkan SPIR-V with `Clouds/Compile.py`.
- `Tools/VolumetricClouds` tests noise tiling, deterministic generation,
  coverage ranking, mip filtering, and compiled shader bindings/formats/layout.
- Release `WorldClient`, `UIUtil`, and the full `WowClient` target build in
  `build/retail-satchel`. Output: `bin/Win/WowClientRetailInventory.exe`.
- **Pending live acceptance:** checkbox layout and Apply/Cancel/Defaults,
  on/off comparison, day/night and storms, terrain/building occlusion,
  portal views, water views, relog persistence and GPU frame time. No game
  installation was changed for this port.

Run native checks with:

```powershell
rtk proxy cmake -S Tools/VolumetricClouds -B build/volumetric-clouds-tests -A x64
rtk proxy cmake --build build/volumetric-clouds-tests --config Release
rtk proxy ctest --test-dir build/volumetric-clouds-tests -C Release --output-on-failure
```

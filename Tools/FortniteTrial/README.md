# Journey Mentor Shield trial

Local art proof for the supplied FortnitePorting UEFORMAT v10 mesh. Converts only this rigid shield;
it is not a general Fortnite importer. The original exports are read without modification.

Build assets into a new external directory:

```powershell
rtk proxy python Tools/FortniteTrial/journey_shield.py --out G:/FortniteTrial/JourneyMentorShield-new
```

The script validates source geometry, rigid weights, triangle winding, M2/SKIN references, opacity tracks,
BLP mipmaps and exact StormLib MPQ readback. It refuses to overwrite an existing output archive.
The lit diffuse trial does not reproduce Unreal's normal/specular material response.

Build the client with the existing `build/retail-satchel` configuration and target `WowClient`.
Compile `test_cloak_anchor.cpp` with the repository root on the C++17 include path and run the executable;
asserts must remain enabled. It checks upper-cloak bone selection, depth fitting and invalid input rejection.

## Installed trial

- Executable: `G:/3.3.5a - Dev/WowJourneyShieldTrial.exe` (normal `Wow.exe` is unchanged).
- Assets: `G:/3.3.5a - Dev/Data/Patch-ZzzzzzzzzzzJourneyShield.MPQ` (three namespaced entries, no DBC changes).
- Final asset stage and hashes: `G:/FortniteTrial/JourneyMentorShield-v4/receipt.json`.
- Installed hashes and previous trial executables: same directory, `install-receipt.json`.

Launch the trial executable, equip/show an ordinary cloak, then use:

```lua
/run JourneyShieldTrial("on")
/run JourneyShieldTrial("off")
```

With `-console`, the equivalent console commands are `journeyshield on`, `journeyshield off`,
and `journeyshield model` for the standalone model. `retailmodel hide` hides that standalone frame.
`WOW_JOURNEY_SHIELD_TRIAL=on` enables the local cloak replacement at startup; `model` opens the standalone preview.

The trial affects the active player or selected roster character only. It does not create an inventory item,
change server state, or replicate the cosmetic to other clients. Turning it off restores the ordinary cloak.

## Fit and evidence

The upper edge of the selected cloak geoset supplies the animated bone, height, lateral center and width.
Visible torso geometry supplies back depth within the shield's footprint. At the user's request the depth is
halfway between the original mount and full back clearance; height and scale are unchanged.
Human male `retail/1011653.m2` uses geoset 1502, bone 16: original X -0.088, full clearance -0.226,
midpoint approximately -0.157; Z 1.739 and scale 1.097.

Native loading and textured standalone rendering were observed. The user confirmed back attachment in-world,
then requested the depth adjustments above and accepted the final midpoint fit ("That one looks great").
All races, equipment combinations and movement poses have not been accepted. Models whose cloak lives only
in a separate collection are outside this trial's parent-mesh fitting path.

To roll back, close the trial and use the normal executable. The three namespaced art files are inert there.
The added trial executable and archive can be removed when no client holds them open; nothing in the database
needs reversing. Previous trial executables are retained outside the mountable Data directory.

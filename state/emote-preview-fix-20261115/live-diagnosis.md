Live diagnosis on the earlier WowCollectionsPreview.exe:

- Observed all six Emotes cards standing idle and the selected Billy Bounce stage standing idle.
- Changed only the seven emote preview model paths to retail/1011653.m2 using their existing SetModel API.
- Observed distinct dance poses in all six cards and the stage across two screenshots. The existing Lua clocks work.
- The bare-path diagnostic produced dark equipment/texture artifacts. Restored every diagnostic frame with SetUnit("player") and verified the original appearance returned.
- Permanent code fix uses CGCharacterModelBase::GetDuplicateUnitCharacterModel for mapped, untransformed players, preserving the live model and collection bindings instead of loading the bare path.
- Built and installed WowCollectionsPlaybackFix.exe, SHA256 cc2567b394540f7e224b19672b7c2d105e46f3a901fa06d5d49f8ca397fde370.
- The earlier executable remained running. Live diagnosis is confirmed; the corrected executable's post-restart appearance and playback acceptance remain pending.

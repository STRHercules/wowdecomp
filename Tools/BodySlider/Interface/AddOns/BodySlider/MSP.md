# Body Slider MSP extension

This addon carries a body-shape field using [Mary Sue Protocol v1](https://moonshyne.org/msp/)
request/response grammar. Its field parsing was checked against
[the supplied TRP3 WotLK implementation](https://github.com/joyvanderveeken/Total-RP-3-WotLK/blob/main/totalRP3/modules/register/msp/register_msp.lua).

## Sharing transports

The default transport joins the custom `BodySliderMSP` channel and broadcasts a
printable message with the envelope `[BodySliderMSP1]`, followed by the MSP-style
field response `BS<version>=BodySlider/2:...`. This is ordinary server-visible chat
sent through `SendChatMessage(..., "CHANNEL", nil, channelId)`. The channel
envelope is a Body Slider extension, **not standard MSP whisper transport**.
Stock 3.3.5 does not support `SendAddonMessage` with `CHANNEL` distribution.

The addon suppresses only valid Body Slider envelopes on that exact channel from
normal chat display. Other channel messages and all original server warnings
remain visible. A display filter does not make the channel private or invisible
to the server. Joining is attempted at most three times, ten seconds apart.
Updates are batched at most once per three seconds and repeated every thirty
seconds for later arrivals. There are no per-peer channel request storms.

Group mode broadcasts `MSP` addon messages to `PARTY` or `RAID`, with the same field
response and no whispers or channel joins. It waits when there is no group.
Whisper mode is explicit opt-in and retains normal MSP requests/responses for
compatibility. It may be unusable on realms that restrict low-level whispers.
There is no automatic fallback to whispers in channel or group mode.

Use `/bodyslider channel`, `/bodyslider group`, `/bodyslider whisper`, or
`/bodyslider status`. The selected transport persists with the character's saved
settings. Upgraded characters without a saved transport default to channel mode.
An explicit recent server rejection pauses the current transport for the session
and prints one Body Slider status line; original server errors are not hidden.
Selecting a transport again explicitly retries it. An exact missing-player reply
backs off only that outgoing whisper recipient. Empty and localized unknown names
are never outgoing whisper targets.

`BS` is an **experimental unregistered field**, not a standardized MSP body-shape
field or an allocation approved by the MSP author. Fields beginning X, Y or Z
are reserved by the original spec, so those are deliberately not used. Before a
public interoperable release, coordinate a field allocation with MSP maintainers.
Unrelated `BS` contents are ignored unless a supported `BodySlider/1:` or `BodySlider/2:` namespace and
the complete numeric schema validate.

The current value is `BodySlider/2:` followed by exactly thirty-seven comma-separated
integers. BreastSize and ButtSize accept 0..100; all other controls accept -100..100.
The order is Height, HeadSize, NeckLength, ShoulderWidth, ArmLength, ArmGirth,
ChestSize, WaistWidth, TorsoLength, HipWidth, LegLength, LegGirth, BreastSize,
ButtSize, JawSize, JawLength, FaceWidth, FaceLength, BrowHeight, BrowDepth,
EyeSize, EyeSpacing, EyeHeight, NoseSize, NoseWidth, NoseLength, NoseHeight,
MouthWidth, LipFullness, MouthHeight, ChinLength, ChinDepth, CheekWidth,
CheekHeight, JawWidth, EarSize, EarHeight. A value of zero restores that proportion.

The decoder also accepts the original `BodySlider/1:` twelve-control schema,
mapping its first twelve controls unchanged and clearing all twenty-five added
controls to zero. SavedVariables keep the same per-realm/character keys and named
values; missing new values initialize to zero. Version 1 addons cannot decode
version 2 payloads, so both viewers should upgrade for the new controls.
The payload has no GUID: sender identity is bound to game-observed player names
and GUIDs, never a caller-supplied target encoded in the message.

Requests are `?BS` or `?BS<version>`. Responses are `BS<version>=<value>` or
`!BS<version>` when unchanged. Incoming `MSP\1`, `MSP\2`, `MSP\3` fragments are
reassembled with a 4096-byte cap and ten-second expiry. Our own messages fit in
one packet, including the worst-case schema values and ten-digit MSP version.
The schema decoder rejects overlong or incomplete payloads and negative mesh
values before applying any native changes. In whisper mode each peer is polled
no more than once per fifteen seconds. Whisper sends are
queued at five packets per second, with a 64-packet queue and 128-peer limit;
ChatThrottleLib is used when another addon supplies it.

In whisper mode, with a public `_G.msp` library, the addon publishes only `msp.my.BS` and calls
`msp:Update()`, allowing that library to answer requests. Otherwise it answers
only `BS` itself; it neither registers `msp_RPAddOn` nor answers or overwrites
name, description, or other RP fields. The linked TRP3 fork keeps its MSP object
private and can answer unknown `BS` requests with an empty response in parallel;
Body Slider ignores that empty response. This coexistence is covered by a
simulated protocol test; it still needs verification with actual RP addons.
Channel and group modes do not publish a field into LibMSP. Leaving whisper mode
or pausing it removes only the field this addon previously published and updates
its library version, without altering other roleplay fields.

Discovery uses target, mouseover, focus, party/raid and, when exported by the
DLL, `BodySlider_Units()` for visible players. Without native enumeration,
players must be targeted/moused over or grouped before received settings can be
bound to the correct GUID. An incoming valid broadcast can trigger a fresh native
discovery, but cannot supply a target GUID. A later heartbeat handles initially
uncached names. Server faction, channel and addon-message restrictions still
apply; no transport is guaranteed on every realm.

The panel and protocol start on `PLAYER_LOGIN` only. The addon makes no login,
GlueXML, character-creation, DBC, model-file or server changes.
The panel has Body, Face and Features pages, with at most fourteen controls on
screen. Native model support enables or disables each control independently.
The optional native `BodySlider_HasSlider(name)` checks the local model's actual
skeleton. Unavailable sliders are disabled and labelled unsupported; the panel
shows `Unsupported model` if no controls apply. Availability refreshes when the
panel opens and once a second while visible to handle delayed model loads or
model changes. `BodySlider_Supported()` indicates active in-game hooks, not
support for every slider or model.

Tests: `python test_protocol.py` (requires `lupa`, Lua 5.1 runtime).

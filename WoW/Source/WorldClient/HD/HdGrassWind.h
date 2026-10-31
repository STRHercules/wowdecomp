// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOW_SOURCE_WORLDCLIENT_HD_HDGRASSWIND_H
#define WOW_SOURCE_WORLDCLIENT_HD_HDGRASSWIND_H

#include <cstdint>

// Not in the original binary. Azeroth Remastered (Waystone HD): grass wind and grass parting around the player (not part of the original client).
// Algorithm and default values adapted from wxl-grasswind (WarcraftXL, by iThorgrim, GPLv3,
// https://www.gnu.org/licenses/): WarcraftXL patches Blizzard's grass bytecode, here the same motion lives in our own
// HLSL grass vertex shader (HdDetailDoodad.cpp, function GrassOffset) and the constants go in c53..c59.
//   - wind: two travelling sine waves in the world plane plus a small downwind lean; the blade bends with
//     ((1 - uv.y - anchor) / (1 - anchor))^2 (uv.y is 1 at the foot of the blade, 0 at its tip), so the foot never
//     moves, on flat ground or on a slope; a per-blade spread of amplitude and phase, less sway far away;
//   - parting: blades inside a radius around the camera's target (the player, or what the player rides) lean away from it,
//     strongest at the centre, only the part of the blade higher than 0.3 yd above the player's feet, and times
//     the blade weight (not in WarcraftXL: on a slope above the player the foot of the blade must not move either).
//   hdGrassWind 1 = wind on (default), 0 = off
//   hdGrassWindSpeed      wave speed in yards per second (default 3)
//   hdGrassWindDirection  where the wind goes, degrees in the world plane (default 45)
//   hdGrassWindStrength   sway multiplier (default 1 = WarcraftXL's amplitudes)
//   hdGrassReact 1 = grass parts around the player (default), 0 = off
//   hdGrassReactRadius    radius of the parting in yards (default 2)
// Works only while our grass shaders are in place (hdGrassLight 1, 2 or 3).

namespace HD {
    // Registers the cvars above (once).
    void GrassWindRegister();

    // True while hdGrassWind or hdGrassReact is on.
    bool GrassWindWanted();

    // Once per frame, before the grass chunks are drawn: advances the wave phases, reads the player's position.
    void GrassWindPrepare();

    // c53..c67 (60 floats) for one grass chunk: the wind, then the 8 units it parts around. chunkOffset = the chunk's translation in the grass world matrix
    // (CWorldScene::RenderDetailDoodads: chunk->m_topLeftCoords - CWorldScene::s_activeWorldView).
    void GrassWindConstants(float chunkOffsetX, float chunkOffsetY, float chunkOffsetZ, float* out);

    // The batch class of a GroundEffectDoodad (0 = no GroundEffectDoodadWind.cdbc row): doodads bake into batches of
    // their class, and c68 (GrassWindBatchConstants, 4 floats) says how that class bends. Not in the donor.
    uint16_t GrassWindClassOf(uint32_t doodadId);
    void GrassWindBatchConstants(uint16_t windClass, float* out);
}

#endif

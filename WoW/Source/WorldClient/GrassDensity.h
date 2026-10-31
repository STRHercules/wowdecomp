#ifndef WOW_SOURCE_WORLDCLIENT_GRASSDENSITY_H
#define WOW_SOURCE_WORLDCLIENT_GRASSDENSITY_H

#include <cstdint>

// Not in the original binary. Denser grass near the player, from GroundEffectDoodadDensity.cdbc (seyris-living-azeroth,
// GPL-3.0): each placed plant gets copies on its own terrain triangle, full density within half the row's Radius, back
// to normal at Radius. The donor makes the copies on the GPU each frame; here they are baked with the chunk (a chunk
// that comes into the radius is baked again, see CMapChunk::RenderPrep), so they cost kept grass memory.
// The player's grassDensity* cvars cap the table's values.
namespace GrassDensity {

void RegisterCVars();

struct Values {
    float multiplier = 1.0f;   // plants drawn per placed plant
    float radius = 0.0f;       // yards around the player
    float spread = 0.0f;       // most yards a copy moves from its plant
};

// The row for a doodad in a chunk's area (chunk-side, not where the player stands), capped by the player's limits.
Values Resolve(uint32_t chunkAreaId, int32_t mapId, uint32_t doodadId, uint32_t groundEffectId);

// 0..1: how much of the density applies at this distance from the player (1 within half the radius, 0 at it).
float Weight(const Values& values, float distance);

// Far grass thinning (the donor's GrassThinning): the share of a chunk's plants drawn at this distance, 1 within
// grassThinStart, grassThinMinShare from grassThinEnd on. Quantised to eighths, so a chunk is baked again only when it
// crosses a step.
float ThinShare(float distanceToPlayer);

// What a chunk is baked for at this distance (its density weight and thinning step), to tell when its bake is out of
// date; 0 when neither applies.
uint32_t Level(float distanceToPlayer);

// True while density or thinning can change a chunk's bake.
bool Active();

// The most any chunk asks for (the player's caps), for sizing the kept grass pool.
float MaxMultiplier();
float MaxRadius();

}  // namespace GrassDensity

#endif

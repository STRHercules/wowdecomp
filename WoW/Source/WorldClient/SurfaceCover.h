#ifndef WOW_SOURCE_WORLDCLIENT_SURFACECOVER_H
#define WOW_SOURCE_WORLDCLIENT_SURFACECOVER_H

class CMapRenderChunk;

// Not in the original binary. Painted snow (and sand, ash) lying on the terrain as a layer of its own, from
// seyris-living-azeroth's tables (GPL-3.0): GroundMaterialSelector.cdbc says which material a terrain layer is (by its
// texture, ground effect or TerrainType, per area, map or everywhere), GroundMaterial.cdbc chains materials to their
// parents, SurfaceCover.cdbc says how deep the cover on a material lies and how it looks.
//
// Unlike the donor (a 5-level clipmap lifted by vertex texture fetch, which Gx has no binding for), the cover is each
// covered terrain chunk drawn a second time right after it: the chunk's own vertices lifted by the cover's depth
// (worked out from the alpha maps per vertex), its own index buffer, and the terrain's lighting, fog and shadows,
// which are still bound. Where the cover ends follows the alpha maps per pixel. Around the player a fine patch (a
// quarter yard per cell, 48 yd across) replaces it: lumpy, with trenches that units press into it and that fill back
// in over RelaxSeconds, its border blending into the chunks' mesh. No melting or fallen snow yet. Our shaders
// (HD/HdSurfaceCover.cpp) need the Vulkan backend's translated modules (Tools/HdMaterials/install_hd_shaders.py), like
// the HD materials.
//   surfaceCover 1 on, surfaceCoverDepth depth multiplier, surfaceCoverOthers sand and ash too (not only snow),
//   surfaceCoverBrightness, surfaceCoverBreakup ragged edges multiplier, surfaceCoverDistanceLift yards added far away,
//   surfaceCoverNear the fine patch, surfaceCoverTrenches, surfaceCoverTrenchScale trench width per unit size,
//   surfaceCoverInfo status: /run print(GetCVar("surfaceCoverInfo"))
namespace SurfaceCover {

void RegisterCVars();

// After CMapRenderChunk::RenderSinglePassVertexPixelShader drew a chunk: its cover on top, with the chunk's terrain
// constants and samplers still in place.
void DrawAfter(CMapRenderChunk* chunk);

// Once per frame (CGWorldFrame::OnWorldUpdate): the near patch follows the player, trenches form and fill in.
void Update(float elapsedSec);

// After the terrain (CWorldScene::RenderChunksSinglePass): the near patch, with the last chunk's terrain constants.
void DrawNear();

// The render chunk's terrain vertices were made again, or it is going away: its cover is made again at its next draw.
void Forget(CMapRenderChunk* chunk);

}  // namespace SurfaceCover

#endif

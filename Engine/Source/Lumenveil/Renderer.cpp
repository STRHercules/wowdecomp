// Not in the original 3.3.5a binary. Lumenveil's fog passes over GxEffects: the mod's renderer.cpp (RenderPasses and
// the functions it calls) with Direct3D 9 swapped for the State below. The constant registers, texture stages,
// sizes and formats are the mod's; where a part is not ported yet (see Renderer.h) its stage stays empty and its
// flag in the composite constants stays off, as the mod has it when that part is switched off.
#include "Engine/Source/Lumenveil/Renderer.h"
#include "Engine/Source/Lumenveil/WetExclusions.h"

#include "Engine/Source/Lumenveil/Core/belt_lantern.h"
#include "Engine/Source/Lumenveil/Core/cloud_noise.h"
#include "Engine/Source/Lumenveil/Core/drift_noise.h"
#include "Engine/Source/Lumenveil/Core/fog_data.h"
#include "Engine/Source/Lumenveil/Core/log.h"
#include "Engine/Source/Lumenveil/Core/wet_surfaces.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "Engine/Source/Lumenveil/Shaders/Spirv.inc"

namespace {
    constexpr uint32_t kShaftConstantsRegister = 36;
    constexpr uint32_t kDriftConstantsRegister = 38;
    constexpr uint32_t kDriftConstantCount = 5;
    constexpr uint32_t kTerrainShadowRegister = 43;
    constexpr uint32_t kShaftMemoryRegister = 45;
    constexpr uint32_t kShaftMemoryConstants = 4;
    constexpr uint32_t kShaftMemoryBeforeRegister = 49;
    constexpr uint32_t kShaftMemoryBeforeStage = 4;
    constexpr uint32_t kCloudRegister = 53;
    constexpr uint32_t kCloudConstants = 2;
    constexpr uint32_t kShaftBodyRegister = 55;
    constexpr uint32_t kShaftBodyConstants = 2;
    constexpr uint32_t kHazeFromSkyRegister = 57;
    constexpr uint32_t kSkyReferenceRegister = 58;
    constexpr uint32_t kSkyReferenceStage = 6;
    constexpr uint32_t kBankRegister = 59;
    constexpr uint32_t kBankConstants = 8;
    constexpr uint32_t kBankFloorStage = 7;
    constexpr float kBankFlattening = 1.3f;
    constexpr uint32_t kSkyColumns = 64;
    constexpr uint32_t kSkyRows = 32;
    constexpr uint32_t kSkyPassRegister = 9;
    constexpr uint32_t kSkyPassConstants = 4;
    constexpr uint32_t kSkyFirstStage = 0;
    constexpr uint32_t kSkySecondStage = 1;
    constexpr float kSkyElevationSpread = 0.08f;
    constexpr uint32_t kSkyLightRegister = 13;
    constexpr float kSkyAwayFromLightFullCos = 0.7071068f;
    constexpr float kSkyAwayFromLightNoneCos = 0.9396926f;
    constexpr float kSkyColumnSpread = 3.0f;
    constexpr float kSkyMinimumWeight = 1e-3f;
    constexpr float kSkyFullWeight = 1.0f;
    constexpr double kSkySmoothingSeconds = 0.15;
    constexpr double kSkyMaxStepSeconds = 0.5;
    constexpr float kSkyMinExposure = 1e-3f;
    constexpr uint32_t kDriftNoiseStage = 1;
    constexpr uint32_t kTerrainShadowStage = 2;
    constexpr uint32_t kEdgeOccluderStage = 3;
    constexpr uint32_t kCloudStage = 5;
    constexpr uint32_t kEdgeBlockWidth = 8;
    constexpr uint32_t kEdgeBlockHeight = 32;
    constexpr uint32_t kCompositeTerrainShadowStage = 5;
    constexpr uint32_t kCompositeCloudStage = 8;
    constexpr uint32_t kCompositeSurfaceShadowRegister = 13;
    constexpr uint32_t kCompositeLampRegister = 17;
    constexpr uint32_t kCompositeLampSurfaceRegister = 18;
    constexpr uint32_t kCompositeLampHaloStage = 6;
    constexpr uint32_t kCompositeLampSurfaceStage = 7;
    constexpr uint32_t kCompositeScreenShadeRegister = 33;
    constexpr uint32_t kCompositeScreenShadowStage = 13;
    constexpr float kLightShadowYards = 30.0f;
    constexpr float kWaterReflectionYards = 400.0f;
    constexpr uint32_t kWaterMemorySize = 1024;
    constexpr uint32_t kWaterMemoryStage = 6;
    constexpr uint32_t kWaterRainRegister = 26;
    constexpr uint32_t kWaterRippleRegister = 28;
    constexpr uint32_t kWaterRippleStage = 3;
    constexpr uint32_t kWaterRainStage = 4;
    constexpr uint32_t kCompositeWetStage = 12;
    constexpr uint32_t kWetRegister = 9;
    constexpr uint32_t kWetConstants = 20;
    constexpr float kMaterialTagShare = 0.01f;
    constexpr float kMaterialTagYards = 0.05f;
    constexpr float kGlintCellYards = 0.03f;
    constexpr float kGlintCellShare = 0.02f;
    constexpr uint32_t kWaterLampRegister = 30;
    constexpr uint32_t kWaterLampStage = 7;
    constexpr uint32_t kWaterSceneBeforeStage = 2;
    constexpr uint32_t kWaterMemoryRegister = 24;
    constexpr float kWaterMemoryForgetYards = 200.0f;
    constexpr double kWaterMemoryForgetSeconds = 30.0;
    constexpr uint32_t kLampSurfaceDepthStage = 0;
    constexpr uint32_t kLampSurfaceTableStage = 1;
    constexpr uint32_t kLampSurfaceOccluderStage = 2;
    constexpr uint32_t kLampSurfaceTileStage = 3;
    constexpr uint32_t kLampSurfaceConstants = 5;
    constexpr uint32_t kLampBlurSourceStage = 1;
    constexpr uint32_t kLampPassRegister = 9;
    constexpr uint32_t kLampVisibilityDepthStage = 0;
    constexpr uint32_t kLampPreviousVisibilityStage = 1;
    constexpr uint32_t kLampVisibilityTableStage = 2;
    constexpr uint32_t kLampDepthStage = 0;
    constexpr uint32_t kLampFogStage = 1;
    constexpr uint32_t kLampVisibilityStage = 2;
    constexpr uint32_t kLampTableStage = 3;
    constexpr uint32_t kLampTileStage = 4;
    constexpr uint32_t kEffectPassRegister = 9;
    constexpr uint32_t kEffectSceneNowStage = 0;
    constexpr uint32_t kEffectSceneBeforeStage = 1;
    constexpr uint32_t kEffectDepthBeforeStage = 2;
    constexpr uint32_t kEffectDepthNowStage = 3;
    constexpr uint32_t kEffectLightStage = 0;
    constexpr uint32_t kEffectPlaceStage = 1;
    constexpr uint32_t kCompositeOvercastRegister = 21;
    constexpr uint32_t kCompositeLowSunRegister = 22;
    constexpr uint32_t kCompositeCloudRegister = 24;
    constexpr uint32_t kCompositeCloudConstants = 3;
    constexpr uint32_t kCompositeGradeRegister = 27;
    constexpr uint32_t kCompositeBalanceRegister = 28;
    constexpr uint32_t kCompositeSkyGlowRegister = 29;
    constexpr uint32_t kCompositeWhiteBalanceRegister = 30;
    constexpr uint32_t kCompositeWhiteBalanceConstants = 2;
    constexpr uint32_t kCompositeSunAndMoonRegister = 32;
    constexpr float kFixedFunctionAlbedo = 0.3f;
    constexpr float kAlbedoCap = 0.9f;
    constexpr float kSurfaceShadowMapMarginYards = 16.0f;
    constexpr float kSurfaceShadowMapFadeYards = 128.0f;
    constexpr float kShaftFadeStart = 0.7f;
    constexpr float kEdgeOccluderViewZRatio = 2.5f;
    constexpr uint32_t kLightGlowStage = 4;
    constexpr double kPi = 3.14159265358979;
    constexpr double kMaxDriftStepSeconds = 0.25;
    constexpr float kDriftFlattening = 2.5f;
    constexpr float kFineDriftScale = 5.0f;
    constexpr float kDriftFootprintPerStep = 0.25f;
    constexpr float kSwirlYardsPerPatch = 0.8f;
    constexpr double kSwirlCentre = 0.5;
    constexpr double kFineDriftHeadingOffset = 0.7;
    constexpr double kFineDriftSpeedScale = 1.6;
    constexpr double kCoarseDriftRisePerWind = 0.05;
    constexpr double kFineDriftRisePerWind = 0.15;
    constexpr float kMistRange = 500.0f;
    constexpr uint32_t kGlowSourceScale = 2;
    constexpr uint32_t kGlowScale = 8;
    constexpr float kGlowBlurSpacings[] = { 1.0f, 2.5f };
    constexpr float kMinLightGlow = 0.005f;
    constexpr float kLightGlowGain = 200.0f;
    constexpr float kLightGlowCeiling = 0.4f;
    constexpr uint32_t kRayScale = 4;
    constexpr float kMinViewportDepthExtent = 0.01f;
    constexpr float kDeepestWorldDepthInFullRangeViewport = 0.9999995f;
    constexpr float kWorldDepthMargin = 2.0e-6f;
    constexpr double kSummarySeconds = 60.0;
    constexpr float kRayFalloff = 8.0f;
    constexpr float kRayThreshold = 0.12f;
    constexpr float kRayStep = 0.075f;
    constexpr float kRayDecay = 0.9f;
    constexpr int kRayTaps = 8;
    constexpr double kHistoryMaxSeconds = 0.25;
    constexpr float kHistoryMaxMove = 30.0f;
    constexpr float kShadowMinStep = 1.5f;
    constexpr float kShadowStepPerYard = 0.035f;
    constexpr float kShadowThicknessSteps = 4.0f;
    constexpr uint32_t kLitTerrainTexel = 0xFFFFFFFF;
    constexpr uint32_t kMemoryScatterStride = 4;
    constexpr int kMemoryScatterOrder[kMemoryScatterStride * kMemoryScatterStride][2] = {
        { 0, 0 }, { 2, 2 }, { 2, 0 }, { 0, 2 }, { 1, 1 }, { 3, 3 }, { 3, 1 }, { 1, 3 },
        { 1, 0 }, { 3, 2 }, { 3, 0 }, { 1, 2 }, { 0, 1 }, { 2, 3 }, { 2, 1 }, { 0, 3 },
    };
    constexpr uint32_t kMemoryPassRegister = 9;
    constexpr uint32_t kMemoryPreviousStage = 0;
    constexpr uint32_t kMemoryDepthStage = 1;
    constexpr uint32_t kMemoryVertexSampler = 0;   // D3DVERTEXTEXTURESAMPLER0: binding 0, which the vertex stage shares
    constexpr uint32_t kMemoryGridRegister = 255;  // the grid width the translated vertex shaders read (Shaders/Compile.py)
    constexpr double kMemoryBiasYards = 0.5;
    constexpr double kMemoryRealignDegrees = 0.2;
    constexpr double kMemoryReanchorYards = 2048.0;
    constexpr unsigned kMemoryRealignSeeds = 4096;
    constexpr double kMemoryFadeShareOfInterval = 0.9;
    constexpr double kMemoryMinFadeSeconds = 0.25;
    constexpr double kMemoryMaxFadeSeconds = 3.0;
    constexpr double kMemoryFirstFadeSeconds = 1.0;

    // The composite's colour writes: red, green and blue, as the mod's D3DCOLORWRITEENABLE.
    constexpr uint32_t kColourChannels = 0x7;

    struct Float4 {
        float x, y, z, w;
    };

    struct BodyConstants {
        Float4 base;
        Float4 up;
    };

    BodyConstants BodyConstantsOf(const CarrierBody& body) {
        return { { body.base[0], body.base[1], body.base[2], body.radius }, { body.up[0], body.up[1], body.up[2], body.height } };
    }

    bool TerrainMapWanted(const Config& cfg) {
        return cfg.terrainShadows || cfg.surfaceShadows > 0.0f || cfg.lowSunLight > 0.0f || cfg.cloudShadows > 0.0f || cfg.banks != kBanksOff
            || cfg.fogCeiling > 0.0f || WetSurfacesWanted(cfg);
    }

    bool FloorMapWanted(const Config& cfg) {
        return cfg.banks != kBanksOff || cfg.fogCeiling > 0.0f;
    }

    struct BankConstants {
        Float4 floor;
        Float4 shape;
        Float4 grounds[kBankGrounds];
        Float4 light;
        Float4 misc;
        Float4 noise;
    };
    static_assert(sizeof(BankConstants) == kBankConstants * sizeof(Float4), "the bank constants are c59-c66");

    BankConstants BuildBankConstants(const FrameInputs& in, const FogParams& fog, const TerrainShadowConstants& floors, bool floorsKnown) {
        BankConstants c = {};
        const bool known = floorsKnown && floors.uvPerYard > 0.0f;
        c.floor = { floors.uvAtCamera[0], floors.uvAtCamera[1], known ? floors.uvPerYard : 0.0f, known ? 1.0f : 0.0f };
        c.shape = { fog.bankSharpness, fog.bankThresholdRise, fog.bankPoolGain, fog.bankLift };
        for (int g = 0; g < kBankGrounds; ++g) {
            c.grounds[g] = { fog.banks[g].threshold, fog.banks[g].norm, std::max(fog.banks[g].height, 1.0f), fog.banks[g].density };
        }
        c.light = { in.toLight[0] * fog.bankShadeYards, in.toLight[1] * fog.bankShadeYards, in.toLight[2] * fog.bankShadeYards, fog.bankShade };
        c.misc = { fog.referenceZ, fog.bankStrength, fog.bankValleysOnly, fog.bankShadeNoiseScale };
        c.noise = { kBankWispErosion, 0.0f, 0.0f, 0.0f };
        return c;
    }

    struct CloudLookup {
        Float4 map;
        Float4 shape;
    };

    constexpr float kCloudMinLightRise = 0.02f;

    CloudLookup CloudConstants(const FrameInputs& in, const FogParams& fog, const double* offset, bool noise) {
        if (!noise || !(fog.cloudShadow > 0.0f) || !(fog.cloudTileYards > 0.0f)) {
            return { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f, 0.0f } };
        }
        const double tile = fog.cloudTileYards;
        const double u = (static_cast<double>(in.camPos[0]) - offset[0]) / tile;
        const double v = (static_cast<double>(in.camPos[1]) - offset[1]) / tile;
        const float rise = std::max(in.toLight[2], kCloudMinLightRise);
        return { { static_cast<float>(u - std::floor(u)), static_cast<float>(v - std::floor(v)), static_cast<float>(1.0 / tile), fog.cloudHeight },
                 { in.toLight[0] / rise, in.toLight[1] / rise, 1.0f - fog.cloudCover, fog.cloudShadow } };
    }

    Float4 ShaftFadeConstants(float shaftRange, float shaftDepth, float shaftGlare) {
        const float edgeThicknessLevels = std::log(1.0f + shaftDepth) / std::log(kEdgeOccluderViewZRatio);
        const float glarePerBrightness = 1.0f / std::max(shaftGlare, kMinShaftGlare);
        if (shaftRange <= 0.0f) {
            return { 0.0f, 1.0f, edgeThicknessLevels, glarePerBrightness };
        }
        const float perYard = 1.0f / ((1.0f - kShaftFadeStart) * shaftRange);
        return { perYard, -kShaftFadeStart * shaftRange * perYard, edgeThicknessLevels, glarePerBrightness };
    }

    void Normalize3(float* v) {
        const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (len > 1e-6f) {
            v[0] /= len;
            v[1] /= len;
            v[2] /= len;
        }
    }

    bool WorldViewFromCameraRelative(const FrameInputs& in, float* viewToWorld, float* worldToView) {
        if (!Invert4x4(in.cameraRelativeView, viewToWorld)) {
            return false;
        }
        viewToWorld[12] = in.camPos[0];
        viewToWorld[13] = in.camPos[1];
        viewToWorld[14] = in.camPos[2];
        return Invert4x4(viewToWorld, worldToView);
    }

    struct WorldDepthMapping {
        float atInfinity;
        float perInverseViewDepth;
        float deepest;
    };

    WorldDepthMapping MapWorldDepth(const float* proj, const D3DVIEWPORT9& vp) {
        const bool usableRange = vp.MaxZ - vp.MinZ > kMinViewportDepthExtent && vp.MinZ >= 0.0f && vp.MaxZ <= 1.0f;
        const float worldMinZ = usableRange ? vp.MinZ : 0.0f;
        const float worldExtent = usableRange ? vp.MaxZ - vp.MinZ : 1.0f;
        const float worldMaxZ = worldMinZ + worldExtent;
        return { worldMinZ + worldExtent * (1.0f + proj[10]) * 0.5f, worldExtent * proj[14] * 0.5f,
                 worldMaxZ >= kDeepestWorldDepthInFullRangeViewport ? kDeepestWorldDepthInFullRangeViewport : worldMaxZ + kWorldDepthMargin };
    }

    enum class FogBlend {
        GammaFixedFunction,
        LinearOverSceneCopy,
        LinearFixedFunction,
    };

    const char* FogBlendName(FogBlend blend) {
        static const char* const kNames[] = { "gamma, fixed function", "linear over a scene copy", "linear, fixed function (no scene copy)" };
        return kNames[static_cast<int>(blend)];
    }

    double Wrapped(double textureCoordinate) {
        return textureCoordinate - std::floor(textureCoordinate);
    }

    float CoarseDriftPerYard(const Config& cfg) {
        return 1.0f / (kDriftCellsPerTile * cfg.driftSize);
    }

    const char* WeatherName(WeatherKind weather) {
        static const char* const kNames[] = { "clear", "rain", "snow", "sand" };
        return kNames[static_cast<int>(weather)];
    }

    struct DriftConstants {
        Float4 coarse;
        Float4 fine;
        Float4 scale;
        Float4 mist;
        Float4 detail;
    };
    static_assert(sizeof(DriftConstants) == kDriftConstantCount * sizeof(Float4), "drift constants are c38-c42");

    DriftConstants BuildDriftConstants(const FrameInputs& in, const FogParams& fog, const Config& cfg, const double* coarseDrift,
                                       const double* fineDrift, bool noiseBound) {
        const float coarseXY = CoarseDriftPerYard(cfg);
        const float flattening = kDriftFlattening + (kBankFlattening - kDriftFlattening) * std::clamp(fog.bankStrength, 0.0f, 1.0f);
        const float coarseZ = coarseXY * flattening;
        const float fineXY = coarseXY * kFineDriftScale;
        const float fineZ = fineXY * flattening;
        const double coarsePerYard[3] = { coarseXY, coarseXY, coarseZ };
        const double finePerYard[3] = { fineXY, fineXY, fineZ };
        const float swirl = cfg.driftSwirl * kSwirlYardsPerPatch * cfg.driftSize * fineXY;
        const double swirlCentring[3] = { -kSwirlCentre * swirl, -kSwirlCentre * swirl, 0.0 };
        float coarseOrigin[3];
        float fineOrigin[3];
        for (int k = 0; k < 3; ++k) {
            coarseOrigin[k] = static_cast<float>(Wrapped(in.camPos[k] * coarsePerYard[k] + coarseDrift[k]));
            fineOrigin[k] = static_cast<float>(Wrapped(in.camPos[k] * finePerYard[k] + fineDrift[k] + swirlCentring[k]));
        }
        const float amplitude = noiseBound ? fog.drift : 0.0f;
        DriftConstants c;
        c.coarse = { coarseOrigin[0], coarseOrigin[1], coarseOrigin[2], 2.0f * amplitude };
        c.fine = { fineOrigin[0], fineOrigin[1], fineOrigin[2], std::log2(kFineDriftScale) };
        c.scale = { coarseXY, coarseZ, fineXY, fineZ };
        c.mist = { fog.mistTop, fog.mistFalloff, fog.mistBoost, 1.0f / kMistRange };
        c.detail = { kDriftNoiseSize * coarseXY * kDriftFootprintPerStep, swirl, cfg.driftDetail, 1.0f - amplitude };
        return c;
    }
}

// --- the D3D9 state -------------------------------------------------------------------------------------------------

void LumenveilRenderer::SetConstants(uint32_t reg, const float* values, uint32_t rows) {
    if (reg + rows <= GxEffects::kMaxConstantRows) {
        std::memcpy(m_state.constants[reg], values, rows * sizeof(m_state.constants[0]));
    }
}

void LumenveilRenderer::Bind(uint32_t stage, GxEffects::Texture* texture, bool linear, bool wrap, bool mips) {
    m_state.stages[stage].texture = texture;
    m_state.stages[stage].linear = linear;
    m_state.stages[stage].wrap = wrap;
    m_state.stages[stage].mips = mips;
}

// D3D9's SetRenderTarget: the viewport becomes the whole target.
void LumenveilRenderer::SetTarget(GxEffects::Texture* texture) {
    m_state.target = texture;
    m_state.x = 0;
    m_state.y = 0;
    m_device->Size(texture, m_state.width, m_state.height);
}

void LumenveilRenderer::SetWorldTarget(const D3DVIEWPORT9& vp) {
    m_state.target = nullptr;
    m_state.x = static_cast<int32_t>(vp.X);
    m_state.y = static_cast<int32_t>(vp.Y);
    m_state.width = vp.Width;
    m_state.height = vp.Height;
}

bool LumenveilRenderer::DrawPoints(const GxEffects::Program* vertexProgram, uint32_t gridW, uint32_t points) {
    m_state.constants[kMemoryGridRegister][0] = static_cast<float>(gridW);
    m_state.vertexProgram = vertexProgram;
    m_state.points = points;
    const bool drawn = DrawFullscreen();
    m_state.vertexProgram = nullptr;
    m_state.points = 0;
    return drawn;
}

bool LumenveilRenderer::DrawFullscreen() {
    GxEffects::Pass pass;
    pass.target = m_state.target;
    pass.x = m_state.x;
    pass.y = m_state.y;
    pass.width = m_state.width;
    pass.height = m_state.height;
    pass.program = m_state.program;
    pass.vertexProgram = m_state.vertexProgram;
    pass.points = m_state.points;
    pass.clear = m_state.clear;
    std::memcpy(pass.textures, m_state.stages, sizeof(pass.textures));
    pass.constants = m_state.constants;
    pass.constantRows = GxEffects::kMaxConstantRows;
    pass.blend = m_state.blend;
    pass.writeMask = m_state.writeMask;
    return m_device->Draw(pass);
}

// --- resources ------------------------------------------------------------------------------------------------------

bool LumenveilRenderer::Skip(const char* reason, const char** skip) {
    *skip = reason;
    return false;
}

void LumenveilRenderer::Release() {
    if (m_device) {
        for (EffectReadback& r : m_effectRing) {
            m_device->CancelRead(r.read);
            m_device->Destroy(r.target);
        }
        GxEffects::Texture* const textures[] = { m_marchTarget, m_history[0], m_history[1], m_rays[0], m_rays[1], m_sceneCopy, m_driftNoise,
                                                 m_cloudNoise, m_terrainTexture, m_bankFloorTexture, m_glowSource, m_glow[0], m_glow[1],
                                                 m_edgeOccluders, m_skyGatherTarget, m_skyColumns, m_skyReference[0], m_skyReference[1],
                                                 m_occluderDepth, m_memory[0], m_memory[1], m_memoryBefore, m_lampTable, m_lampVisibility[0],
                                                 m_lampVisibility[1], m_lampHalos, m_lampSurfaces, m_lampSurfacesBlur, m_lampTileTexture, m_effectBaseline, m_effectGather[0], m_effectGather[1], m_lightShadow[0], m_lightShadow[1], m_fine.tex[0], m_fine.tex[1], m_fine.before, m_waterScene[0], m_waterScene[1], m_waterSlopes, m_waterWaveTable, m_waterWaves[0], m_waterWaves[1],
                                                 m_waterWaves[2], m_waterSlopeTarget, m_waterMemory[0], m_waterMemory[1], m_waterRainTable, m_waterRippleTable, m_waterLampTable, m_wetTarget };
        for (GxEffects::Texture* t : textures) {
            m_device->Destroy(t);
        }
    }
    // The programs belong to the device for its life; a new device makes them again.
    *this = LumenveilRenderer();
}

bool LumenveilRenderer::EnsurePrograms() {
    if (m_composite) {
        return true;
    }
    auto program = [this](const uint32_t* words, size_t bytes, const char* name) { return m_device->CreateProgram(words, bytes, name); };
    m_march[0] = program(kPsMarchLow, sizeof kPsMarchLow, "Lumenveil march low");
    m_march[1] = program(kPsMarchMid, sizeof kPsMarchMid, "Lumenveil march mid");
    m_march[2] = program(kPsMarchHigh, sizeof kPsMarchHigh, "Lumenveil march high");
    m_temporal = program(kPsTemporal, sizeof kPsTemporal, "Lumenveil temporal");
    m_rayMask = program(kPsRayMask, sizeof kPsRayMask, "Lumenveil ray mask");
    m_rayBlur = program(kPsRayBlur, sizeof kPsRayBlur, "Lumenveil ray blur");
    m_glowMask = program(kPsGlowMask, sizeof kPsGlowMask, "Lumenveil glow mask");
    m_glowBlur = program(kPsGlowBlur, sizeof kPsGlowBlur, "Lumenveil glow blur");
    m_edgeOccluder = program(kPsEdgeOccluders, sizeof kPsEdgeOccluders, "Lumenveil edge occluders");
    m_skyGather = program(kPsSkyGather, sizeof kPsSkyGather, "Lumenveil sky gather");
    m_skyColumn = program(kPsSkyColumns, sizeof kPsSkyColumns, "Lumenveil sky columns");
    m_skyReferenceProgram = program(kPsSkyReference, sizeof kPsSkyReference, "Lumenveil sky reference");
    m_marchMemory[0] = program(kPsMarchMemoryLow, sizeof kPsMarchMemoryLow, "Lumenveil march memory low");
    m_marchMemory[1] = program(kPsMarchMemoryMid, sizeof kPsMarchMemoryMid, "Lumenveil march memory mid");
    m_marchMemory[2] = program(kPsMarchMemoryHigh, sizeof kPsMarchMemoryHigh, "Lumenveil march memory high");
    m_waterDepth = program(kPsWaterDepth, sizeof kPsWaterDepth, "Lumenveil water depth");
    m_memoryScatterVs = program(kVsMemoryScatter, sizeof kVsMemoryScatter, "Lumenveil memory scatter (vertex)");
    m_memoryScatterPs = program(kPsMemoryScatter, sizeof kPsMemoryScatter, "Lumenveil memory scatter");
    m_memoryRealignVs = program(kVsMemoryRealign, sizeof kVsMemoryRealign, "Lumenveil memory realign (vertex)");
    m_memoryKeep = program(kPsMemoryKeep, sizeof kPsMemoryKeep, "Lumenveil memory keep");
    m_memoryFill = program(kPsMemoryFill, sizeof kPsMemoryFill, "Lumenveil memory fill");
    m_lampVisibilityProgram = program(kPsLampVisibility, sizeof kPsLampVisibility, "Lumenveil lamp visibility");
    m_lampProgram = program(kPsLamps, sizeof kPsLamps, "Lumenveil lamps");
    m_lampSurfaceProgram = program(kPsLampSurfaces, sizeof kPsLampSurfaces, "Lumenveil lamp surfaces");
    m_lampBlurProgram = program(kPsLampBlur, sizeof kPsLampBlur, "Lumenveil lamp blur");
    if (!m_lampVisibilityProgram || !m_lampProgram || !m_lampSurfaceProgram || !m_lampBlurProgram) {
        m_lampProgram = nullptr;
        VF_LOG_ERROR("lamp shader creation failed; no night lights for this device");
    }
    m_effectGatherLight = program(kPsEffectGatherLight, sizeof kPsEffectGatherLight, "Lumenveil effect gather (light)");
    m_effectGatherPlace = program(kPsEffectGatherPlace, sizeof kPsEffectGatherPlace, "Lumenveil effect gather (place)");
    m_effectReduce = program(kPsEffectReduce, sizeof kPsEffectReduce, "Lumenveil effect reduce");
    if (!m_effectGatherLight || !m_effectGatherPlace || !m_effectReduce) {
        m_effectsFailed = true;
        VF_LOG_ERROR("effect light unavailable: shader creation failed");
    }
    m_waterProgram = program(kPsWater, sizeof kPsWater, "Lumenveil water");
    if (!m_waterProgram) {
        VF_LOG_ERROR("water shader creation failed; no Lumenveil water");
    }
    m_waterWaveProgram[0] = program(kPsWaterWaves0, sizeof kPsWaterWaves0, "Lumenveil water waves 0");
    m_waterWaveProgram[1] = program(kPsWaterWaves1, sizeof kPsWaterWaves1, "Lumenveil water waves 1");
    m_waterWaveProgram[2] = program(kPsWaterWaves2, sizeof kPsWaterWaves2, "Lumenveil water waves 2");
    m_waterSlopeProgram = program(kPsWaterSlope, sizeof kPsWaterSlope, "Lumenveil water slope");
    if (!m_waterWaveProgram[0] || !m_waterWaveProgram[1] || !m_waterWaveProgram[2] || !m_waterSlopeProgram) {
        m_waterWavesFailed = true;
        VF_LOG_ERROR("water wave shader creation failed; the water stays flat");
    }
    m_waterMemoryProgram = program(kPsWaterMemory, sizeof kPsWaterMemory, "Lumenveil water memory");
    if (!m_waterMemoryProgram) {
        m_waterMemoryFailed = true;
        VF_LOG_ERROR("water memory shader creation failed; reflections use the screen alone");
    }
    m_wetProgram = program(kPsWetSurfaces, sizeof kPsWetSurfaces, "Lumenveil wet surfaces");
    if (!m_wetProgram) {
        VF_LOG_ERROR("wet surface shader creation failed; no wet or shiny surfaces");
    }
    m_waterLampProgram = program(kPsWaterLamps, sizeof kPsWaterLamps, "Lumenveil water lamps");
    if (!m_waterLampProgram) {
        VF_LOG_ERROR("water lamp shader creation failed; lamps are not mirrored on the water");
    }
    m_waterReflectProgram = program(kPsWaterReflect, sizeof kPsWaterReflect, "Lumenveil water reflect");
    if (!m_waterReflectProgram) {
        VF_LOG_ERROR("water reflection shader creation failed; no water reflections");
    }
    m_lightShadowProgram = program(kPsLightShadow, sizeof kPsLightShadow, "Lumenveil light shadow");
    GxEffects::Program* composite = program(kPsComposite, sizeof kPsComposite, "Lumenveil composite");
    const bool ok = m_march[0] && m_march[1] && m_march[2] && m_temporal && m_rayMask && m_rayBlur && m_glowMask && m_glowBlur && m_edgeOccluder
        && m_skyGather && m_skyColumn && m_skyReferenceProgram && m_marchMemory[0] && m_marchMemory[1] && m_marchMemory[2] && m_waterDepth
        && m_memoryScatterVs && m_memoryScatterPs && m_memoryRealignVs && m_memoryKeep && m_memoryFill && composite;
    if (!ok) {
        VF_LOG_ERROR("shader creation failed; fog disabled for this device");
        m_failed = true;
        return false;
    }
    m_composite = composite;
    return true;
}

bool LumenveilRenderer::EnsureTargets(uint32_t lowW, uint32_t lowH, uint32_t rayW, uint32_t rayH) {
    if (m_marchTarget && m_lowW == lowW && m_lowH == lowH && m_rayW == rayW && m_rayH == rayH) {
        return true;
    }
    for (GxEffects::Texture** t : { &m_marchTarget, &m_history[0], &m_history[1], &m_rays[0], &m_rays[1] }) {
        m_device->Destroy(*t);
        *t = nullptr;
    }
    m_historyValid = false;
    m_marchTarget = m_device->CreateTexture(lowW, lowH, GxEffects::Format::RGBA16F, true);
    m_history[0] = m_device->CreateTexture(lowW, lowH, GxEffects::Format::RGBA16F, true);
    m_history[1] = m_device->CreateTexture(lowW, lowH, GxEffects::Format::RGBA16F, true);
    m_rays[0] = m_device->CreateTexture(rayW, rayH, GxEffects::Format::BGRA8, true);
    m_rays[1] = m_device->CreateTexture(rayW, rayH, GxEffects::Format::BGRA8, true);
    if (!m_marchTarget || !m_history[0] || !m_history[1] || !m_rays[0] || !m_rays[1]) {
        VF_LOG_ERROR("render target creation failed (%ux%u)", lowW, lowH);
        m_lowW = 0;
        return false;
    }
    m_lowW = lowW;
    m_lowH = lowH;
    m_rayW = rayW;
    m_rayH = rayH;
    VF_LOG_INFO("targets: fog %ux%u (fp16, filter 1), rays %ux%u", lowW, lowH, rayW, rayH);
    return true;
}

bool LumenveilRenderer::CopyWorldViewport(const D3DVIEWPORT9& vp) {
    if (!m_sceneCopy || m_sceneCopyW != vp.Width || m_sceneCopyH != vp.Height) {
        m_device->Destroy(m_sceneCopy);
        m_sceneCopy = nullptr;
        m_sceneCopyW = m_sceneCopyH = 0;
        if (m_sceneCopyFailed) {
            return false;
        }
        m_sceneCopy = m_device->CreateTexture(vp.Width, vp.Height, GxEffects::Format::BGRA8, true);
        if (!m_sceneCopy) {
            m_sceneCopyFailed = true;
            VF_LOG_ERROR("scene copy creation failed (%lux%lu); fog blends in gamma space", vp.Width, vp.Height);
            return false;
        }
        m_sceneCopyW = vp.Width;
        m_sceneCopyH = vp.Height;
    }
    return m_device->CopyColor(m_sceneCopy, static_cast<int32_t>(vp.X), static_cast<int32_t>(vp.Y), vp.Width, vp.Height, false);
}

bool LumenveilRenderer::EnsureDriftNoise() {
    if (m_driftNoise) {
        return true;
    }
    if (m_driftNoiseFailed) {
        return false;
    }
    const std::vector<DriftNoiseLevel> levels = BuildDriftNoise();
    std::vector<const void*> texels;
    for (const DriftNoiseLevel& level : levels) {
        texels.push_back(level.argb.data());
    }
    // D3DFMT_A8R8G8B8 lies in memory as B, G, R, A: the Vulkan B8G8R8A8 format, so the texels go up as they are.
    m_driftNoise = m_device->CreateVolume(kDriftNoiseSize, kDriftNoiseSize, kDriftNoiseSize, static_cast<uint32_t>(levels.size()),
                                          GxEffects::Format::BGRA8, texels.data());
    if (!m_driftNoise) {
        m_driftNoiseFailed = true;
        VF_LOG_ERROR("drift noise volume creation failed; the fog does not drift");
        return false;
    }
    VF_LOG_INFO("drift noise: %d^3 volume, %zu levels, %d channels", kDriftNoiseSize, levels.size(), kDriftChannels);
    return true;
}

bool LumenveilRenderer::EnsureCloudNoise() {
    if (m_cloudNoise) {
        return true;
    }
    if (m_cloudNoiseFailed) {
        return false;
    }
    const std::vector<CloudNoiseLevel> levels = BuildCloudNoise();
    std::vector<const void*> texels;
    for (const CloudNoiseLevel& level : levels) {
        texels.push_back(level.texels.data());
    }
    m_cloudNoise = m_device->CreateFilled(kCloudNoiseSize, kCloudNoiseSize, static_cast<uint32_t>(levels.size()), GxEffects::Format::R8, texels.data());
    if (!m_cloudNoise) {
        m_cloudNoiseFailed = true;
        VF_LOG_ERROR("cloud noise texture creation failed; no cloud shadows");
        return false;
    }
    VF_LOG_INFO("cloud noise: %dx%d L8, %zu levels", kCloudNoiseSize, kCloudNoiseSize, levels.size());
    return true;
}

bool LumenveilRenderer::EnsureTerrainTexture() {
    if (m_terrainTexture) {
        return true;
    }
    constexpr uint32_t side = TerrainShadowMap::kSide;
    m_terrainTexture = m_device->CreateTexture(side, side, GxEffects::Format::RG32F, false);
    if (m_terrainTexture) {
        m_terrainFallback = false;
        m_terrainUploaded = m_terrain.Version() - 1;
        if (UploadTerrainShadows()) {
            VF_LOG_INFO("terrain shadows: %ux%u map of %.0f yd cells, linear filtering", side, side, TerrainShadowMap::kCellYards);
            return true;
        }
        m_device->Destroy(m_terrainTexture);
    }
    const void* lit = &kLitTerrainTexel;
    m_terrainTexture = m_device->CreateFilled(1, 1, 1, GxEffects::Format::BGRA8, &lit);
    if (m_terrainTexture) {
        m_terrainFallback = true;
        VF_LOG_ERROR("terrain shadow map creation failed; no terrain shadows");
        return true;
    }
    return false;
}

bool LumenveilRenderer::UploadTerrainShadows() {
    if (m_terrainUploaded == m_terrain.Version()) {
        return true;
    }
    constexpr int side = TerrainShadowMap::kSide;
    std::vector<float> lightAndGround(static_cast<size_t>(side) * side * 2);
    for (int row = 0; row < side; ++row) {
        const float* light = m_terrain.Texels() + row * side;
        const float* ground = m_terrain.GroundTexels() + row * side;
        float* out = &lightAndGround[static_cast<size_t>(row) * side * 2];
        for (int i = 0; i < side; ++i) {
            out[2 * i] = light[i];
            out[2 * i + 1] = ground[i];
        }
    }
    if (!m_device->Upload(m_terrainTexture, lightAndGround.data())) {
        return false;
    }
    m_terrainUploaded = m_terrain.Version();
    return true;
}

TerrainShadowConstants LumenveilRenderer::UpdateTerrainShadows(const FrameInputs& in, const Config& cfg) {
    const HeightSource source = CurrentHeightSource();
    if (!source.probe) {
        m_terrain.Forget();
    }
    if (!TerrainMapWanted(cfg) || m_terrainFallback || !source.probe) {
        return kTerrainShadowsOff;
    }
    m_terrain.Update(in.camPos, in.mapId, in.toLight, source, CurrentLiquidSource(), FloorMapWanted(cfg));
    return UploadTerrainShadows() ? m_terrain.Constants(in.camPos) : kTerrainShadowsOff;
}

FogCeilingConstants LumenveilRenderer::UpdateFogCeiling(const FrameInputs& in, const Config& cfg, bool floorsKnown) {
    FogCeilingConstants c = { 0.0f, 0.0f, in.camPos[2] };
    float target = 0.0f;
    if (cfg.fogCeiling > 0.0f && floorsKnown) {
        c.groundAtCamera = m_terrain.FloorAt(in.camPos[0], in.camPos[1]).nearFloor;
        c.perYard = 1.0f / (kFogCeilingFadeShare * cfg.fogCeiling);
        target = FogCeilingGate(cfg.fogCeiling, in.camPos[2] - c.groundAtCamera);
    }
    if (m_ceilingSeconds < 0.0 || !(in.seconds >= m_ceilingSeconds)) {
        m_ceilingGate = target;
    } else {
        m_ceilingGate = FogCeilingGateStep(m_ceilingGate, target, in.seconds - m_ceilingSeconds);
    }
    m_ceilingSeconds = in.seconds;
    c.strength = c.perYard > 0.0f ? m_ceilingGate : 0.0f;
    return c;
}

bool LumenveilRenderer::UploadBankFloors() {
    if (m_bankFloorFailed) {
        return false;
    }
    constexpr uint32_t side = TerrainShadowMap::kFloorSide;
    if (!m_bankFloorTexture) {
        m_bankFloorTexture = m_device->CreateTexture(side, side, GxEffects::Format::RGBA32F, false);
        if (!m_bankFloorTexture) {
            m_bankFloorFailed = true;
            VF_LOG_ERROR("fog banks: the %ux%u floor map could not be created; the banks follow your own height", side, side);
            return false;
        }
        m_bankFloorUploaded = m_terrain.FloorVersion() - 1;
    }
    if (m_bankFloorUploaded == m_terrain.FloorVersion()) {
        return true;
    }
    static_assert(sizeof(BankFloor) == 4 * sizeof(float), "a bank floor is one A32B32G32R32F texel");
    if (!m_device->Upload(m_bankFloorTexture, m_terrain.Floors())) {
        return false;
    }
    m_bankFloorUploaded = m_terrain.FloorVersion();
    if (!m_bankFloorLogged) {
        m_bankFloorLogged = true;
        VF_LOG_INFO("fog banks: floor map %ux%u of %.0f yd cells, linear filtering; water from %s", side, side, TerrainShadowMap::kFloorBinYards,
                    CurrentLiquidSource().probe ? "the liquid map" : "nowhere (no liquid map)");
    }
    return true;
}

void LumenveilRenderer::AdvanceDrift(const FrameInputs& in, const FogParams& fog, const Config& cfg) {
    const double seconds = m_driftClockValid ? std::clamp(in.seconds - m_driftSeconds, 0.0, kMaxDriftStepSeconds) : 0.0;
    m_driftSeconds = in.seconds;
    m_driftClockValid = true;
    const double coarseXY = CoarseDriftPerYard(cfg);
    const double coarseZ = coarseXY * kDriftFlattening;
    const double fineXY = coarseXY * kFineDriftScale;
    const double fineZ = fineXY * kDriftFlattening;
    const double heading = cfg.windDirection * kPi / 180.0;
    const double fineHeading = heading + kFineDriftHeadingOffset;
    const double travelled = fog.windSpeed * seconds;
    const double fineTravelled = travelled * kFineDriftSpeedScale;
    m_coarseDrift[0] = Wrapped(m_coarseDrift[0] - std::cos(heading) * travelled * coarseXY);
    m_coarseDrift[1] = Wrapped(m_coarseDrift[1] + std::sin(heading) * travelled * coarseXY);
    m_coarseDrift[2] = Wrapped(m_coarseDrift[2] - kCoarseDriftRisePerWind * travelled * coarseZ);
    m_fineDrift[0] = Wrapped(m_fineDrift[0] - std::cos(fineHeading) * fineTravelled * fineXY);
    m_fineDrift[1] = Wrapped(m_fineDrift[1] + std::sin(fineHeading) * fineTravelled * fineXY);
    m_fineDrift[2] = Wrapped(m_fineDrift[2] - kFineDriftRisePerWind * travelled * fineZ);
}

void LumenveilRenderer::AdvanceClouds(const FrameInputs& in, const FogParams& fog) {
    const double seconds = m_cloudClockValid ? std::clamp(in.seconds - m_cloudSeconds, 0.0, kMaxDriftStepSeconds) : 0.0;
    m_cloudSeconds = in.seconds;
    m_cloudClockValid = true;
    if (!(fog.cloudTileYards > 0.0f)) {
        return;
    }
    const double tile = fog.cloudTileYards;
    for (int k = 0; k < 2; ++k) {
        const double moved = m_cloudOffset[k] + static_cast<double>(fog.cloudWind[k]) * seconds;
        m_cloudOffset[k] = moved - std::floor(moved / tile) * tile;
    }
}

bool LumenveilRenderer::RenderLightGlow(const D3DVIEWPORT9& vp, const Config& cfg) {
    const uint32_t sourceW = std::max(1u, (static_cast<uint32_t>(vp.Width) + kGlowSourceScale - 1) / kGlowSourceScale);
    const uint32_t sourceH = std::max(1u, (static_cast<uint32_t>(vp.Height) + kGlowSourceScale - 1) / kGlowSourceScale);
    const uint32_t glowW = std::max(1u, (static_cast<uint32_t>(vp.Width) + kGlowScale - 1) / kGlowScale);
    const uint32_t glowH = std::max(1u, (static_cast<uint32_t>(vp.Height) + kGlowScale - 1) / kGlowScale);
    if (!m_glowSource || m_glowSourceW != sourceW || m_glowSourceH != sourceH || m_glowW != glowW || m_glowH != glowH) {
        for (GxEffects::Texture** t : { &m_glowSource, &m_glow[0], &m_glow[1] }) {
            m_device->Destroy(*t);
            *t = nullptr;
        }
        m_glowSourceW = m_glowSourceH = m_glowW = m_glowH = 0;
        if (m_glowFailed) {
            return false;
        }
        m_glowSource = m_device->CreateTexture(sourceW, sourceH, GxEffects::Format::BGRA8, true);
        m_glow[0] = m_device->CreateTexture(glowW, glowH, GxEffects::Format::RGBA16F, true);
        m_glow[1] = m_device->CreateTexture(glowW, glowH, GxEffects::Format::RGBA16F, true);
        if (!m_glowSource || !m_glow[0] || !m_glow[1]) {
            m_glowFailed = true;
            VF_LOG_ERROR("light glow target creation failed (%ux%u); lights do not glow in the fog", glowW, glowH);
            return false;
        }
        m_glowSourceW = sourceW;
        m_glowSourceH = sourceH;
        m_glowW = glowW;
        m_glowH = glowH;
        VF_LOG_INFO("light glow targets: source %ux%u, glow %ux%u", sourceW, sourceH, glowW, glowH);
    }
    if (!m_device->CopyColor(m_glowSource, static_cast<int32_t>(vp.X), static_cast<int32_t>(vp.Y), vp.Width, vp.Height, true)) {
        return false;
    }

    SetTarget(m_glow[0]);
    m_state.program = m_glowMask;
    const Float4 mask[2] = {
        { cfg.glowThreshold, static_cast<float>(kGlowScale), 1.0f / glowW, 1.0f / glowH },
        { 1.0f / sourceW, 1.0f / sourceH, 0.0f, 0.0f },
    };
    SetConstants(9, &mask[0].x, 2);
    Bind(0, m_depth, false);
    Bind(1, m_glowSource, true);
    DrawFullscreen();

    m_state.program = m_glowBlur;
    auto blur = [this](GxEffects::Texture* source, GxEffects::Texture* destination, float stepU, float stepV) {
        SetTarget(destination);
        const Float4 constants[2] = {
            { stepU, stepV, 0.0f, 0.0f },
            { static_cast<float>(m_glowW), static_cast<float>(m_glowH), 1.0f / m_glowW, 1.0f / m_glowH },
        };
        SetConstants(9, &constants[0].x, 2);
        Bind(0, source, true);
        DrawFullscreen();
    };
    for (float spacing : kGlowBlurSpacings) {
        blur(m_glow[0], m_glow[1], spacing / glowW, 0.0f);
        blur(m_glow[1], m_glow[0], 0.0f, spacing / glowH);
    }
    return true;
}

void LumenveilRenderer::RenderEdgeOccluders(const D3DVIEWPORT9& vp, const CarrierBody& body) {
    const uint32_t w = std::max(1u, (static_cast<uint32_t>(vp.Width) + kEdgeBlockWidth - 1) / kEdgeBlockWidth);
    const uint32_t h = std::max(1u, (static_cast<uint32_t>(vp.Height) + kEdgeBlockHeight - 1) / kEdgeBlockHeight);
    if (!m_edgeOccluders || m_edgeW != w || m_edgeH != h) {
        if (m_edgeOccludersFailed) {
            return;
        }
        m_device->Destroy(m_edgeOccluders);
        m_edgeW = m_edgeH = 0;
        m_edgeOccluders = m_device->CreateTexture(w, h, GxEffects::Format::BGRA8, true);
        if (!m_edgeOccluders) {
            m_edgeOccludersFailed = true;
            VF_LOG_ERROR("edge occluder target creation failed (%ux%u); shafts from things past the screen edge are off", w, h);
            return;
        }
        m_edgeW = w;
        m_edgeH = h;
        VF_LOG_INFO("edge occluders: %ux%u blocks of up to %ux%u pixels for shafts from things past the screen edge", w, h, kEdgeBlockWidth,
                    kEdgeBlockHeight);
    }
    SetTarget(m_edgeOccluders);
    m_state.program = m_edgeOccluder;
    const Float4 block = { static_cast<float>(vp.Width) / w, static_cast<float>(vp.Height) / h, 0.0f, 0.0f };
    SetConstants(9, &block.x, 1);
    const BodyConstants bodyConstants = BodyConstantsOf(body);
    SetConstants(10, &bodyConstants.base.x, 2);
    Bind(0, m_depth, false);
    DrawFullscreen();
}

bool LumenveilRenderer::EnsureShaftMemory(const D3DVIEWPORT9& vp) {
    if (!m_memoryChecked) {
        m_memoryChecked = true;
        m_memoryUnsupported = !m_device->CanBlend(GxEffects::Format::R32F);
        if (m_memoryUnsupported) {
            VF_LOG_ERROR("shaft memory: the device cannot blend R32F targets; shafts use the screen alone");
        }
    }
    if (m_memoryUnsupported || m_memoryFailed) {
        return false;
    }
    if (!m_occluderDepth || m_occluderDepthW != m_targetW || m_occluderDepthH != m_targetH) {
        if (m_occluderDepthFailed) {
            return false;
        }
        m_device->Destroy(m_occluderDepth);
        m_occluderDepth = m_device->CreateTexture(m_targetW, m_targetH, GxEffects::Format::R32F, true);
        if (!m_occluderDepth) {
            m_occluderDepthFailed = true;
            VF_LOG_ERROR("occluder depth target creation failed (%ux%u)", m_targetW, m_targetH);
            return false;
        }
        m_occluderDepthW = m_targetW;
        m_occluderDepthH = m_targetH;
    }
    if (!m_memory[0]) {
        m_memory[0] = m_device->CreateTexture(kShaftMemorySize, kShaftMemorySize, GxEffects::Format::R32F, true);
        m_memory[1] = m_device->CreateTexture(kShaftMemorySize, kShaftMemorySize, GxEffects::Format::R32F, true);
        m_memoryBefore = m_device->CreateTexture(kShaftMemorySize, kShaftMemorySize, GxEffects::Format::R32F, true);
        if (!m_memory[0] || !m_memory[1] || !m_memoryBefore) {
            m_memoryFailed = true;
            VF_LOG_ERROR("shaft memory: target creation failed; shafts use the screen alone");
            return false;
        }
        m_memoryValid = false;
        VF_LOG_INFO("shaft memory: two %ux%u R32F maps seen from the light, %.3f yd texels (%.0f yd around the camera), realigned when "
                    "the light moves %.1f degrees",
                    kShaftMemorySize, kShaftMemorySize, kShaftMemoryTexelYards,
                    (kShaftMemorySize / 2.0 - kShaftMemoryWindowMarginTexels) * kShaftMemoryTexelYards, kMemoryRealignDegrees);
    }
    const uint32_t gridW = (vp.Width + kMemoryScatterStride - 1) / kMemoryScatterStride;
    const uint32_t gridH = (vp.Height + kMemoryScatterStride - 1) / kMemoryScatterStride;
    if (m_memoryGridW != gridW || m_memoryGridH != gridH) {
        m_memoryGridW = gridW;
        m_memoryGridH = gridH;
        VF_LOG_INFO("shaft memory: %ux%u points, every %u px, cycling through the %u offsets in %u frames", gridW, gridH, kMemoryScatterStride,
                    kMemoryScatterStride * kMemoryScatterStride, kMemoryScatterStride * kMemoryScatterStride);
    }
    return true;
}

// The mod's UpdateShaftMemory. Its point lists from vertex buffers of the grid are point draws whose vertex shaders
// take the grid position from the vertex index (Shaders/Compile.py).
bool LumenveilRenderer::UpdateShaftMemory(const FrameInputs& in, const CarrierBody& body) {
    const D3DVIEWPORT9& vp = in.viewport;
    if (!EnsureShaftMemory(vp)) {
        return false;
    }
    // The depth as the liquids left it, as the R32F map the memory's vertex shaders read (the mod's CopyWaterDepth).
    float common1[4];
    std::memcpy(common1, m_state.constants[1], sizeof common1);
    SetTarget(m_occluderDepth);
    m_state.x = static_cast<int32_t>(vp.X);
    m_state.y = static_cast<int32_t>(vp.Y);
    m_state.width = vp.Width;
    m_state.height = vp.Height;
    m_state.program = m_waterDepth;
    const Float4 grid = { 1.0f, 0.0f, 1.0f / m_targetW, 1.0f / m_targetH };
    SetConstants(1, &grid.x, 1);
    Bind(0, m_occluderSource, false);
    DrawFullscreen();
    SetConstants(1, common1, 1);

    const double size = kShaftMemorySize;
    const float halfWindowTexels = static_cast<float>(size / 2.0 - kShaftMemoryWindowMarginTexels);
    ShaftMemoryCamera camera = ShaftMemoryCameraIn(m_memoryFrame, in.camPos);
    const long long shiftX = camera.cell[0] - m_memoryCell[0];
    const long long shiftY = camera.cell[1] - m_memoryCell[1];
    const long long limit = static_cast<long long>(kShaftMemorySize);
    const bool forget = !m_memoryValid || in.mapId != m_memoryMapId || shiftX <= -limit || shiftX >= limit || shiftY <= -limit || shiftY >= limit;
    if (forget) {
        m_memoryFrame = MakeShaftMemoryFrame(in.toLight, in.camPos, m_memoryFrame.u);
        camera = ShaftMemoryCameraIn(m_memoryFrame, in.camPos);
        m_memoryFading = false;
        m_memoryRealignedBefore = false;
    }
    auto frameConstants = [&](const ShaftMemoryFrame& frame, const ShaftMemoryCamera& at, Float4* out) {
        out[0] = { at.index[0], at.index[1], at.fraction[0], at.fraction[1] };
        out[1] = { static_cast<float>(frame.u[0]), static_cast<float>(frame.u[1]), static_cast<float>(frame.u[2]),
                   static_cast<float>(kShaftMemoryTexelYards) };
        out[2] = { static_cast<float>(frame.v[0]), static_cast<float>(frame.v[1]), static_cast<float>(frame.v[2]), static_cast<float>(size) };
        out[3] = { static_cast<float>(frame.l[0]), static_cast<float>(frame.l[1]), static_cast<float>(frame.l[2]), static_cast<float>(at.depth) };
    };

    const int write = m_memoryIndex ^ 1;
    const float focalPixels = in.glProjection[5] * vp.Height * 0.5f;
    Float4 keep[5];
    keep[0] = { forget ? 0.0f : static_cast<float>(shiftX), forget ? 0.0f : static_cast<float>(shiftY), forget ? 1.0f : 0.0f, focalPixels };
    frameConstants(m_memoryFrame, camera, &keep[1]);
    SetTarget(m_memory[write]);
    m_state.program = m_memoryKeep;
    SetConstants(kMemoryPassRegister, &keep[0].x, 5);
    Bind(kMemoryPreviousStage, m_memory[m_memoryIndex], false);
    Bind(kMemoryDepthStage, m_occluderDepth, false);
    DrawFullscreen();

    const double* l = m_memoryFrame.l;
    const double lightLength = std::sqrt(in.toLight[0] * in.toLight[0] + in.toLight[1] * in.toLight[1] + in.toLight[2] * in.toLight[2]);
    const double cosine = lightLength > 0.0 ? (in.toLight[0] * l[0] + in.toLight[1] * l[1] + in.toLight[2] * l[2]) / lightLength : 1.0;
    const double fromAnchor[3] = { in.camPos[0] - m_memoryFrame.anchor[0], in.camPos[1] - m_memoryFrame.anchor[1],
                                   in.camPos[2] - m_memoryFrame.anchor[2] };
    const bool reanchor = fromAnchor[0] * fromAnchor[0] + fromAnchor[1] * fromAnchor[1] + fromAnchor[2] * fromAnchor[2]
        > kMemoryReanchorYards * kMemoryReanchorYards;
    // The point passes blend with MAX into the red channel, as the mod's BeginMemoryPoints sets them.
    auto beginPoints = [this](GxEffects::Texture* vertexTexture) {
        for (uint32_t stage = 0; stage < GxEffects::kMaxTextures; ++stage) {
            Bind(stage, nullptr, false);
        }
        Bind(kMemoryVertexSampler, vertexTexture, false);
        m_state.program = m_memoryScatterPs;
        m_state.blend = GxEffects::Blend::Max;
        m_state.writeMask = 0x1;
    };
    auto endPoints = [this]() {
        m_state.blend = GxEffects::Blend::Opaque;
        m_state.writeMask = 0xF;
        Bind(kMemoryVertexSampler, nullptr, false);
    };
    if (!forget && (cosine < std::cos(kMemoryRealignDegrees * kPi / 180.0) || reanchor)) {
        const ShaftMemoryFrame next = MakeShaftMemoryFrame(in.toLight, in.camPos, m_memoryFrame.u);
        const ShaftMemoryCamera nextCamera = ShaftMemoryCameraIn(next, in.camPos);
        Float4 realign[9];
        realign[0] = { 0.0f, 0.0f, halfWindowTexels, static_cast<float>(m_memoryRealignmentsTotal % kMemoryRealignSeeds) };
        frameConstants(next, nextCamera, &realign[1]);
        frameConstants(m_memoryFrame, camera, &realign[5]);
        SetTarget(m_memory[m_memoryIndex]);
        beginPoints(m_memory[write]);
        SetConstants(kMemoryPassRegister, &realign[0].x, 9);
        const uint32_t tiles = kShaftMemorySize / kShaftMemoryRealignGrid;
        for (uint32_t ty = 0; ty < tiles; ++ty) {
            for (uint32_t tx = 0; tx < tiles; ++tx) {
                realign[0].x = static_cast<float>(tx * kShaftMemoryRealignGrid);
                realign[0].y = static_cast<float>(ty * kShaftMemoryRealignGrid);
                SetConstants(kMemoryPassRegister, &realign[0].x, 1);
                // The mod clears the target to 0 before the first tile: the first draw's clear.
                m_state.clear = tx == 0 && ty == 0;
                DrawPoints(m_memoryRealignVs, kShaftMemoryRealignGrid, kShaftMemoryRealignGrid * kShaftMemoryRealignGrid);
                m_state.clear = false;
            }
        }
        endPoints();
        const double sinceLast = in.seconds - m_memoryLastRealign;
        m_memoryFadeSeconds = m_memoryRealignedBefore && sinceLast > 0.0
            ? std::clamp(kMemoryFadeShareOfInterval * sinceLast, kMemoryMinFadeSeconds, kMemoryMaxFadeSeconds)
            : kMemoryFirstFadeSeconds;
        m_memoryLastRealign = in.seconds;
        m_memoryRealignedBefore = true;
        m_memoryFadeStart = in.seconds;
        m_memoryFading = true;
        m_memoryBeforeFrame = m_memoryFrame;
        m_memoryBeforeCell[0] = camera.cell[0];
        m_memoryBeforeCell[1] = camera.cell[1];
        std::swap(m_memory[write], m_memoryBefore);
        SetTarget(m_memory[write]);
        m_state.program = m_memoryFill;
        const Float4 fill = { static_cast<float>(1.0 / size), 0.0f, 0.0f, 0.0f };
        SetConstants(kMemoryPassRegister, &fill.x, 1);
        Bind(kMemoryPreviousStage, m_memory[m_memoryIndex], false);
        DrawFullscreen();
        m_memoryFrame = next;
        camera = nextCamera;
        ++m_memoryRealignmentsTotal;
    }

    const uint32_t offsets = kMemoryScatterStride * kMemoryScatterStride;
    const uint32_t firstOffset = forget ? 0u : m_frame % offsets;
    const uint32_t scatters = forget ? offsets : 1u;
    Float4 scatter[7];
    scatter[0] = { static_cast<float>(kMemoryScatterStride), 0.0f, 0.0f, halfWindowTexels };
    frameConstants(m_memoryFrame, camera, &scatter[1]);
    const BodyConstants scatterBody = BodyConstantsOf(body);
    scatter[5] = scatterBody.base;
    scatter[6] = scatterBody.up;
    SetTarget(m_memory[write]);
    beginPoints(m_occluderDepth);
    for (uint32_t s = 0; s < scatters; ++s) {
        const uint32_t order = (firstOffset + s) % offsets;
        scatter[0].y = static_cast<float>(kMemoryScatterOrder[order][0]);
        scatter[0].z = static_cast<float>(kMemoryScatterOrder[order][1]);
        SetConstants(kMemoryPassRegister, &scatter[0].x, 7);
        DrawPoints(m_memoryScatterVs, m_memoryGridW, m_memoryGridW * m_memoryGridH);
    }
    endPoints();

    auto lookupIn = [&](const ShaftMemoryFrame& frame, const ShaftMemoryCamera& at, float window, float weight, float (&out)[4][4]) {
        const double* u = frame.u;
        const double* v = frame.v;
        const double* ll = frame.l;
        const float cameraTexel[2] = { at.index[0] + at.fraction[0], at.index[1] + at.fraction[1] };
        const float lookup[4][4] = {
            { static_cast<float>(u[0]), static_cast<float>(u[1]), static_cast<float>(u[2]), cameraTexel[0] },
            { static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]), cameraTexel[1] },
            { static_cast<float>(ll[0]), static_cast<float>(ll[1]), static_cast<float>(ll[2]),
              static_cast<float>(at.depth + kShaftMemoryDepthOffset + kMemoryBiasYards) },
            { static_cast<float>(1.0 / kShaftMemoryTexelYards), static_cast<float>(1.0 / size), window, weight },
        };
        std::memcpy(out, lookup, sizeof(lookup));
    };
    lookupIn(m_memoryFrame, camera, halfWindowTexels, 0.0f, m_memoryLookup);

    float beforeWeight = 0.0f;
    if (m_memoryFading) {
        const ShaftMemoryCamera before = ShaftMemoryCameraIn(m_memoryBeforeFrame, in.camPos);
        const long long moved = std::max(std::llabs(before.cell[0] - m_memoryBeforeCell[0]), std::llabs(before.cell[1] - m_memoryBeforeCell[1]));
        const float beforeWindow = halfWindowTexels - static_cast<float>(moved);
        const double elapsed = in.seconds - m_memoryFadeStart;
        if (elapsed >= 0.0 && elapsed < m_memoryFadeSeconds && beforeWindow > 0.0f) {
            beforeWeight = static_cast<float>(1.0 - elapsed / m_memoryFadeSeconds);
        }
        m_memoryFading = beforeWeight > 0.0f;
        lookupIn(m_memoryBeforeFrame, before, std::max(beforeWindow, 0.0f), beforeWeight, m_memoryBeforeLookup);
    }
    if (!m_memoryFading) {
        std::memset(m_memoryBeforeLookup, 0, sizeof(m_memoryBeforeLookup));
    }

    m_memoryIndex = write;
    m_memoryCell[0] = camera.cell[0];
    m_memoryCell[1] = camera.cell[1];
    m_memoryMapId = in.mapId;
    m_memoryValid = true;
    return true;
}

// --- the fine shadow memory -----------------------------------------------------------------------------------------
//
// Not in Lumenveil. A second shaft memory (the same passes and shaders as UpdateShaftMemory, which this duplicates with its
// own state) at 0.15 yd texels over the 75 yd around the camera: a shadow map from the sun or moon filled from what the camera
// sees and kept as it turns, so the shadow of a wall stays when the wall leaves the screen, at a resolution good enough for the
// ground. The shaft memory itself (0.625 yd texels, 600 yd) is too coarse for that: shadows from it looked blocky and merged.
// shortcut: duplicated rather than folded into one struct with the shaft memory; fold them when a third map is needed.
namespace {
    constexpr double kFineTexelYards = 0.15;
    constexpr uint32_t kFineSize = 1024;
    constexpr uint32_t kFineRealignGrid = 512;
    constexpr uint32_t kFineStride = 2;
    constexpr int kFineScatterOrder[kFineStride * kFineStride][2] = { { 0, 0 }, { 1, 1 }, { 1, 0 }, { 0, 1 } };

    ShaftMemoryCamera FineCameraIn(const ShaftMemoryFrame& frame, const float* camPos) {
        const double rel[3] = { camPos[0] - frame.anchor[0], camPos[1] - frame.anchor[1], camPos[2] - frame.anchor[2] };
        const double* axes[2] = { frame.u, frame.v };
        const double size = kFineSize;
        ShaftMemoryCamera camera = {};
        for (int k = 0; k < 2; ++k) {
            const double texels = (rel[0] * axes[k][0] + rel[1] * axes[k][1] + rel[2] * axes[k][2]) / kFineTexelYards;
            const double cell = std::floor(texels);
            camera.cell[k] = static_cast<long long>(cell);
            camera.index[k] = static_cast<float>(cell - size * std::floor(cell / size));
            camera.fraction[k] = static_cast<float>(texels - cell);
        }
        camera.depth = rel[0] * frame.l[0] + rel[1] * frame.l[1] + rel[2] * frame.l[2];
        return camera;
    }
}

bool LumenveilRenderer::UpdateFineShadowMemory(const FrameInputs& in, const CarrierBody& body) {
    FineShadowMap& f = m_fine;
    const D3DVIEWPORT9& vp = in.viewport;
    if (f.failed || m_memoryUnsupported || !m_memoryScatterVs || !m_memoryScatterPs || !m_memoryRealignVs || !m_memoryKeep || !m_memoryFill) {
        return false;
    }
    if (!f.tex[0]) {
        f.tex[0] = m_device->CreateTexture(kFineSize, kFineSize, GxEffects::Format::R32F, true);
        f.tex[1] = m_device->CreateTexture(kFineSize, kFineSize, GxEffects::Format::R32F, true);
        f.before = m_device->CreateTexture(kFineSize, kFineSize, GxEffects::Format::R32F, true);
        if (!f.tex[0] || !f.tex[1] || !f.before) {
            f.failed = true;
            VF_LOG_ERROR("fine shadow memory: target creation failed; sun and moon shadows use the screen alone");
            return false;
        }
        f.valid = false;
        VF_LOG_INFO("fine shadow memory: two %ux%u R32F maps seen from the light, %.2f yd texels (%.0f yd around the camera)", kFineSize, kFineSize,
                    kFineTexelYards, (kFineSize / 2.0 - kShaftMemoryWindowMarginTexels) * kFineTexelYards);
    }
    // The depth as the liquids left it, as the R32F map the vertex shaders read.
    if (!m_occluderDepth || m_occluderDepthW != m_targetW || m_occluderDepthH != m_targetH) {
        m_device->Destroy(m_occluderDepth);
        m_occluderDepth = m_device->CreateTexture(m_targetW, m_targetH, GxEffects::Format::R32F, true);
        m_occluderDepthW = m_targetW;
        m_occluderDepthH = m_targetH;
        if (!m_occluderDepth) {
            f.failed = true;
            return false;
        }
    }
    {
        float common1[4];
        std::memcpy(common1, m_state.constants[1], sizeof common1);
        SetTarget(m_occluderDepth);
        m_state.x = static_cast<int32_t>(vp.X);
        m_state.y = static_cast<int32_t>(vp.Y);
        m_state.width = vp.Width;
        m_state.height = vp.Height;
        m_state.program = m_waterDepth;
        m_state.blend = GxEffects::Blend::Opaque;
        m_state.writeMask = 0xF;
        const Float4 grid = { 1.0f, 0.0f, 1.0f / m_targetW, 1.0f / m_targetH };
        SetConstants(1, &grid.x, 1);
        Bind(0, m_occluderSource, false);
        DrawFullscreen();
        SetConstants(1, common1, 1);
    }
    f.gridW = (vp.Width + kFineStride - 1) / kFineStride;
    f.gridH = (vp.Height + kFineStride - 1) / kFineStride;

    const double size = kFineSize;
    const float halfWindowTexels = static_cast<float>(size / 2.0 - kShaftMemoryWindowMarginTexels);
    ShaftMemoryCamera camera = FineCameraIn(f.frame, in.camPos);
    const long long shiftX = camera.cell[0] - f.cell[0];
    const long long shiftY = camera.cell[1] - f.cell[1];
    const long long limit = static_cast<long long>(kFineSize);
    const bool forget = !f.valid || in.mapId != f.mapId || shiftX <= -limit || shiftX >= limit || shiftY <= -limit || shiftY >= limit;
    if (forget) {
        f.frame = MakeShaftMemoryFrame(in.toLight, in.camPos, f.frame.u);
        camera = FineCameraIn(f.frame, in.camPos);
        f.fading = false;
        f.realignedBefore = false;
    }
    auto frameConstants = [&](const ShaftMemoryFrame& frame, const ShaftMemoryCamera& at, Float4* out) {
        out[0] = { at.index[0], at.index[1], at.fraction[0], at.fraction[1] };
        out[1] = { static_cast<float>(frame.u[0]), static_cast<float>(frame.u[1]), static_cast<float>(frame.u[2]), static_cast<float>(kFineTexelYards) };
        out[2] = { static_cast<float>(frame.v[0]), static_cast<float>(frame.v[1]), static_cast<float>(frame.v[2]), static_cast<float>(size) };
        out[3] = { static_cast<float>(frame.l[0]), static_cast<float>(frame.l[1]), static_cast<float>(frame.l[2]), static_cast<float>(at.depth) };
    };

    const int write = f.index ^ 1;
    const float focalPixels = in.glProjection[5] * vp.Height * 0.5f;
    Float4 keep[5];
    keep[0] = { forget ? 0.0f : static_cast<float>(shiftX), forget ? 0.0f : static_cast<float>(shiftY), forget ? 1.0f : 0.0f, focalPixels };
    frameConstants(f.frame, camera, &keep[1]);
    SetTarget(f.tex[write]);
    m_state.program = m_memoryKeep;
    SetConstants(kMemoryPassRegister, &keep[0].x, 5);
    Bind(kMemoryPreviousStage, f.tex[f.index], false);
    Bind(kMemoryDepthStage, m_occluderDepth, false);
    DrawFullscreen();

    const double* l = f.frame.l;
    const double lightLength = std::sqrt(in.toLight[0] * in.toLight[0] + in.toLight[1] * in.toLight[1] + in.toLight[2] * in.toLight[2]);
    const double cosine = lightLength > 0.0 ? (in.toLight[0] * l[0] + in.toLight[1] * l[1] + in.toLight[2] * l[2]) / lightLength : 1.0;
    const double fromAnchor[3] = { in.camPos[0] - f.frame.anchor[0], in.camPos[1] - f.frame.anchor[1], in.camPos[2] - f.frame.anchor[2] };
    const bool reanchor = fromAnchor[0] * fromAnchor[0] + fromAnchor[1] * fromAnchor[1] + fromAnchor[2] * fromAnchor[2]
        > (kFineSize * kFineTexelYards * 0.4) * (kFineSize * kFineTexelYards * 0.4);
    auto beginPoints = [this](GxEffects::Texture* vertexTexture) {
        for (uint32_t stage = 0; stage < GxEffects::kMaxTextures; ++stage) {
            Bind(stage, nullptr, false);
        }
        Bind(kMemoryVertexSampler, vertexTexture, false);
        m_state.program = m_memoryScatterPs;
        m_state.blend = GxEffects::Blend::Max;
        m_state.writeMask = 0x1;
    };
    auto endPoints = [this]() {
        m_state.blend = GxEffects::Blend::Opaque;
        m_state.writeMask = 0xF;
        Bind(kMemoryVertexSampler, nullptr, false);
    };
    if (!forget && (cosine < std::cos(kMemoryRealignDegrees * kPi / 180.0) || reanchor)) {
        const ShaftMemoryFrame next = MakeShaftMemoryFrame(in.toLight, in.camPos, f.frame.u);
        const ShaftMemoryCamera nextCamera = FineCameraIn(next, in.camPos);
        Float4 realign[9];
        realign[0] = { 0.0f, 0.0f, halfWindowTexels, static_cast<float>(f.realignments % kMemoryRealignSeeds) };
        frameConstants(next, nextCamera, &realign[1]);
        frameConstants(f.frame, camera, &realign[5]);
        SetTarget(f.tex[f.index]);
        beginPoints(f.tex[write]);
        SetConstants(kMemoryPassRegister, &realign[0].x, 9);
        const uint32_t tiles = kFineSize / kFineRealignGrid;
        for (uint32_t ty = 0; ty < tiles; ++ty) {
            for (uint32_t tx = 0; tx < tiles; ++tx) {
                realign[0].x = static_cast<float>(tx * kFineRealignGrid);
                realign[0].y = static_cast<float>(ty * kFineRealignGrid);
                SetConstants(kMemoryPassRegister, &realign[0].x, 1);
                m_state.clear = tx == 0 && ty == 0;
                DrawPoints(m_memoryRealignVs, kFineRealignGrid, kFineRealignGrid * kFineRealignGrid);
                m_state.clear = false;
            }
        }
        endPoints();
        const double sinceLast = in.seconds - f.lastRealign;
        f.fadeSeconds = f.realignedBefore && sinceLast > 0.0
            ? std::clamp(kMemoryFadeShareOfInterval * sinceLast, kMemoryMinFadeSeconds, kMemoryMaxFadeSeconds)
            : kMemoryFirstFadeSeconds;
        f.lastRealign = in.seconds;
        f.realignedBefore = true;
        f.fadeStart = in.seconds;
        f.fading = true;
        f.beforeFrame = f.frame;
        f.beforeCell[0] = camera.cell[0];
        f.beforeCell[1] = camera.cell[1];
        std::swap(f.tex[write], f.before);
        SetTarget(f.tex[write]);
        m_state.program = m_memoryFill;
        const Float4 fill = { static_cast<float>(1.0 / size), 0.0f, 0.0f, 0.0f };
        SetConstants(kMemoryPassRegister, &fill.x, 1);
        Bind(kMemoryPreviousStage, f.tex[f.index], false);
        DrawFullscreen();
        f.frame = next;
        camera = nextCamera;
        ++f.realignments;
    }

    const uint32_t offsets = kFineStride * kFineStride;
    const uint32_t firstOffset = forget ? 0u : m_frame % offsets;
    const uint32_t scatters = forget ? offsets : 1u;
    Float4 scatter[7];
    scatter[0] = { static_cast<float>(kFineStride), 0.0f, 0.0f, halfWindowTexels };
    frameConstants(f.frame, camera, &scatter[1]);
    const BodyConstants scatterBody = BodyConstantsOf(body);
    scatter[5] = scatterBody.base;
    scatter[6] = scatterBody.up;
    SetTarget(f.tex[write]);
    beginPoints(m_occluderDepth);
    for (uint32_t s = 0; s < scatters; ++s) {
        const uint32_t order = (firstOffset + s) % offsets;
        scatter[0].y = static_cast<float>(kFineScatterOrder[order][0]);
        scatter[0].z = static_cast<float>(kFineScatterOrder[order][1]);
        SetConstants(kMemoryPassRegister, &scatter[0].x, 7);
        DrawPoints(m_memoryScatterVs, f.gridW, f.gridW * f.gridH);
    }
    endPoints();

    auto lookupIn = [&](const ShaftMemoryFrame& frame, const ShaftMemoryCamera& at, float window, float weight, float (&out)[4][4]) {
        const float cameraTexel[2] = { at.index[0] + at.fraction[0], at.index[1] + at.fraction[1] };
        const float lookup[4][4] = {
            { static_cast<float>(frame.u[0]), static_cast<float>(frame.u[1]), static_cast<float>(frame.u[2]), cameraTexel[0] },
            { static_cast<float>(frame.v[0]), static_cast<float>(frame.v[1]), static_cast<float>(frame.v[2]), cameraTexel[1] },
            { static_cast<float>(frame.l[0]), static_cast<float>(frame.l[1]), static_cast<float>(frame.l[2]),
              static_cast<float>(at.depth + kShaftMemoryDepthOffset + kMemoryBiasYards) },
            { static_cast<float>(1.0 / kFineTexelYards), static_cast<float>(1.0 / size), window, weight },
        };
        std::memcpy(out, lookup, sizeof(lookup));
    };
    lookupIn(f.frame, camera, halfWindowTexels, 0.0f, f.lookup);

    float beforeWeight = 0.0f;
    if (f.fading) {
        const ShaftMemoryCamera before = FineCameraIn(f.beforeFrame, in.camPos);
        const long long moved = std::max(std::llabs(before.cell[0] - f.beforeCell[0]), std::llabs(before.cell[1] - f.beforeCell[1]));
        const float beforeWindow = halfWindowTexels - static_cast<float>(moved);
        const double elapsed = in.seconds - f.fadeStart;
        if (elapsed >= 0.0 && elapsed < f.fadeSeconds && beforeWindow > 0.0f) {
            beforeWeight = static_cast<float>(1.0 - elapsed / f.fadeSeconds);
        }
        f.fading = beforeWeight > 0.0f;
        lookupIn(f.beforeFrame, before, std::max(beforeWindow, 0.0f), beforeWeight, f.beforeLookup);
    }
    if (!f.fading) {
        std::memset(f.beforeLookup, 0, sizeof(f.beforeLookup));
    }

    f.index = write;
    f.cell[0] = camera.cell[0];
    f.cell[1] = camera.cell[1];
    f.mapId = in.mapId;
    f.valid = true;
    return true;
}

bool LumenveilRenderer::EnsureSkyReference() {
    if (m_skyGatherTarget) {
        return true;
    }
    if (m_skyReferenceFailed) {
        return false;
    }
    m_skyGatherTarget = m_device->CreateTexture(kSkyColumns, kSkyRows, GxEffects::Format::RGBA16F, true);
    m_skyColumns = m_device->CreateTexture(kSkyColumns, 1, GxEffects::Format::RGBA16F, true);
    m_skyReference[0] = m_device->CreateTexture(kSkyColumns, 1, GxEffects::Format::RGBA16F, true);
    m_skyReference[1] = m_device->CreateTexture(kSkyColumns, 1, GxEffects::Format::RGBA16F, true);
    if (!m_skyGatherTarget || !m_skyColumns || !m_skyReference[0] || !m_skyReference[1]) {
        m_skyReferenceFailed = true;
        VF_LOG_ERROR("sky reference unavailable: its targets could not be made; the haze takes the light data's horizon");
        return false;
    }
    m_skyReferenceValid = false;
    VF_LOG_INFO("sky reference: the haze before the land takes the sky as drawn, in %u screen columns from %ux%u blocks (fp16)", kSkyColumns,
                kSkyColumns, kSkyRows);
    return true;
}

bool LumenveilRenderer::SkyReferenceFresh(const FrameInputs& in) const {
    const double age = in.seconds - m_skyReferenceSeconds;
    return m_skyReferenceValid && age > 0.0 && age <= kSkyMaxStepSeconds;
}

void LumenveilRenderer::MeasureSky(const FrameInputs& in, const Config& cfg, const FogParams& fog) {
    const double step = in.seconds - m_skyReferenceSeconds;
    const bool kept = SkyReferenceFresh(in);
    m_skyReferenceValid = false;
    m_skyReferenceSeconds = in.seconds;
    if (!EnsureSkyReference() || !CopyWorldViewport(in.viewport)) {
        return;
    }
    m_state.blend = GxEffects::Blend::Opaque;
    m_state.writeMask = 0xF;

    SetTarget(m_skyGatherTarget);
    m_state.program = m_skyGather;
    const Float4 gather = { kSkyElevationSpread, kSkyAwayFromLightFullCos, kSkyAwayFromLightNoneCos, 0.0f };
    SetConstants(kSkyPassRegister, &gather.x, 1);
    const Float4 light = { in.toLight[0], in.toLight[1], in.toLight[2], 0.0f };
    SetConstants(kSkyLightRegister, &light.x, 1);
    Bind(kSkyFirstStage, m_depth, false);
    Bind(kSkySecondStage, m_sceneCopy, false);
    DrawFullscreen();

    SetTarget(m_skyColumns);
    m_state.program = m_skyColumn;
    Bind(kSkyFirstStage, m_skyGatherTarget, false);
    Bind(kSkySecondStage, nullptr, false);
    DrawFullscreen();

    const int write = m_skyReferenceIndex ^ 1;
    SetTarget(m_skyReference[write]);
    m_state.program = m_skyReferenceProgram;
    const float exposure = std::max(fog.authored ? cfg.classicExposure : cfg.exposure, kSkyMinExposure);
    const float newShare = kept ? static_cast<float>(1.0 - std::exp(-step / kSkySmoothingSeconds)) : 1.0f;
    const Float4 reference[kSkyPassConstants] = {
        { kSkyColumnSpread, kSkyMinimumWeight, newShare, kept ? 1.0f : 0.0f },
        { std::clamp(cfg.hazeSkyLevel, kMinHazeSkyLevel, kMaxHazeSkyLevel), fog.saturation, exposure, kSkyFullWeight },
        { fog.whiteBalance[0], fog.whiteBalance[1], fog.whiteBalance[2], 0.0f },
        { fog.hazeFromSky[0], fog.hazeFromSky[1], fog.hazeFromSky[2], 0.0f },
    };
    SetConstants(kSkyPassRegister, &reference[0].x, kSkyPassConstants);
    Bind(kSkyFirstStage, m_skyColumns, false);
    Bind(kSkySecondStage, m_skyReference[m_skyReferenceIndex], false);
    DrawFullscreen();
    m_skyReferenceIndex = write;
    m_skyReferenceValid = true;
}

void LumenveilRenderer::LogWater(const FrameInputs& in, const Config& cfg) {
    if (m_waterDrawn == 0 && m_waterSkips == 0) {
        return;
    }
    VF_LOG_INFO("  water: drawn in %u frames (%u with waves, %u with memory, %u with lamps, %u in rain), %u ripples started, skipped %u%s%s; memory forgot %u times, "
                "clarity %.2f reflect %.2f glitter %.2f waves %.2f",
                m_waterDrawn, m_waterWaveFrames, m_waterMemoryFrames, m_waterLampFrames, m_waterRainFrames, m_waterRippleStarts, m_waterSkips,
                m_waterSkips ? ", last because " : "", m_waterSkips ? m_waterSkipReason : "", m_waterMemoryForgets, cfg.waterClarity, cfg.waterReflect,
                cfg.waterGlitter, cfg.waterWaves);
    m_waterDrawn = m_waterSkips = m_waterWaveFrames = m_waterMemoryFrames = m_waterLampFrames = m_waterRainFrames = m_waterRippleStarts = 0;
    (void)in;
}

void LumenveilRenderer::LogSummary(const FrameInputs& in, const FogParams& fog, const float* viewToWorld, const float* toLightInView,
                                   const float* sunPx, float rayStrength) {
    const D3DVIEWPORT9& vp = in.viewport;
    const float* proj = in.glProjection;
    VF_LOG_INFO("frame %u: viewport %lu,%lu %lux%lu of %ux%u depth %.4f..%.4f; near %.3f far %.1f (farclip %.1f) maxdist %.0f", m_frame, vp.X,
                vp.Y, vp.Width, vp.Height, m_targetW, m_targetH, vp.MinZ, vp.MaxZ, -proj[14] / (1.0f + proj[10]), proj[14] / (1.0f - proj[10]),
                in.farClip, fog.maxDistance);
    VF_LOG_INFO("  camera (%.2f %.2f %.2f) inverse-view origin (%.2f %.2f %.2f) target (%.2f %.2f %.2f)", in.camPos[0], in.camPos[1], in.camPos[2],
                viewToWorld[12], viewToWorld[13], viewToWorld[14], in.camTarget[0], in.camTarget[1], in.camTarget[2]);
    VF_LOG_INFO("  day %.4f %s toLight (%.3f %.3f %.3f) view (%.3f %.3f %.3f) vis %.2f above %.2f sunPx (%.0f %.0f) rays %.2f", in.dayFraction,
                in.lightIsMoon ? "moon" : "sun", in.toLight[0], in.toLight[1], in.toLight[2], toLightInView[0], toLightInView[1], toLightInView[2],
                fog.lightVisibility, fog.lightAboveHorizon, sunPx[0], sunPx[1], rayStrength);
    VF_LOG_INFO("  map %d fog %08X start %.1f end %.1f zone %.1f sun %08X direct %08X ambient %08X refZ %.1f glow %.2f", in.mapId, in.fogColor,
                in.fogStart, in.fogEnd, in.zoneFogDistance, in.sunColor, in.directColor, in.ambientColor, fog.referenceZ, in.clientGlowAmount);
    for (int i = 0; i < kFogLayers; ++i) {
        const FogLayer& l = fog.layers[i];
        VF_LOG_INFO("  layer %d (%s): start %.0f density %.6f curve %.2f^%.2f g %.2f diffuse %.2f %.2f %.2f emissive %.2f %.2f %.2f", i,
                    fog.authored && i < kSceneLayers ? "classic" : "derived", l.start, l.density, l.strength, l.exponent, l.g, l.diffuse[0],
                    l.diffuse[1], l.diffuse[2], l.emissive[0], l.emissive[1], l.emissive[2]);
    }
    VF_LOG_INFO("  %s: weather %s %.2f, wind %.1f yd/s, drift %.2f, mist %.2f, evening haze %.2f, twilight %.2f, light glow %.2f",
                in.lightIsMoon ? "moon" : "sun", WeatherName(in.weather), fog.weather, fog.windSpeed, fog.drift, fog.mistBoost, fog.eveningHaze,
                fog.twilight, fog.lightGlow);
}

// --- the wet surfaces -----------------------------------------------------------------------------------------------

bool LumenveilRenderer::EnsureWetTarget(uint32_t w, uint32_t h) {
    if (m_wetTarget && m_wetW == w && m_wetH == h) {
        return true;
    }
    if (m_wetFailed) {
        return false;
    }
    m_device->Destroy(m_wetTarget);
    m_wetW = m_wetH = 0;
    m_wetTarget = m_device->CreateTexture(w, h, GxEffects::Format::RGBA16F, true);
    if (!m_wetTarget) {
        m_wetFailed = true;
        VF_LOG_ERROR("wet surface target creation failed (%ux%u); no wet or shiny surfaces", w, h);
        return false;
    }
    m_wetW = w;
    m_wetH = h;
    VF_LOG_INFO("wet surfaces: %ux%u fp16 target, sheen toward the sun or moon, wetness, puddles and screen-space reflections", w, h);
    return true;
}

// The mod's RenderWetSurfaces. Left out: the ground materials (they need the relief and material passes, so the pass runs as the mod
// does with them off) and the scene before the late draws (not captured).
bool LumenveilRenderer::RenderWetSurfaces(GxEffects::Texture* fogTexture, const FrameInputs& in, const Config& cfg, const FogParams& fog,
                                          const TerrainShadowConstants& terrain, const float* cloudMap, const float* cloudShape, float exposure,
                                          bool sceneBlend) {
    const bool waterKnown = m_waterKnown && m_waterDepthBefore;
    m_waterKnown = false;
    if (!WetSurfacesWanted(cfg) || !m_wetProgram) {
        m_wetness.Reset();
        return false;
    }
    m_wetness.Update(in);
    m_wetParams = BuildWetSurfaceParams(in, cfg, m_wetness);
    if (!sceneBlend || !WetSurfacesShown(m_wetParams) || !EnsureWetTarget(in.viewport.Width, in.viewport.Height)) {
        return false;
    }
    const bool puddles = m_wetParams.puddleShare > 0.0f && EnsureCloudNoise();
    const bool cloudy = cloudShape[3] > 0.0f && m_cloudNoise;
    const bool noise = puddles || cloudy;
    const bool lampsUsable = m_lampSurfacesDrawn && m_wetLampSlots > 0.0f;

    SetTarget(m_wetTarget);
    m_state.blend = GxEffects::Blend::Opaque;
    m_state.writeMask = 0xF;
    m_state.program = m_wetProgram;

    const SunFacing facing = DaylightFacing(in);
    const double tile = kPuddleTileYards;
    const double u = static_cast<double>(in.camPos[0]) / tile;
    const double v = static_cast<double>(in.camPos[1]) / tile;
    const double turn = static_cast<double>(kPuddleSecondTurnDegrees) * kPi / 180.0;
    const double turnCos = std::cos(turn) / static_cast<double>(kPuddleSecondTileYards);
    const double turnSin = std::sin(turn) / static_cast<double>(kPuddleSecondTileYards);
    const double u2 = turnCos * in.camPos[0] - turnSin * in.camPos[1];
    const double v2 = turnSin * in.camPos[0] + turnCos * in.camPos[1];
    const float focalPixels = 0.5f * in.glProjection[5] * static_cast<float>(in.viewport.Height);
    float horizon[3];
    float above[3];
    UnpackColor(in.skyHorizonColor, horizon);
    UnpackColor(in.skyAboveHorizonColor, above);
    for (int i = 0; i < 3; ++i) {
        horizon[i] = std::pow(std::max(horizon[i], 0.0f), 2.2f);
        above[i] = std::pow(std::max(above[i], 0.0f), 2.2f);
    }
    float albedoPerLight[3];
    AlbedoPerSceneLight(in, albedoPerLight);
    const float threshold = puddles ? PuddleThreshold(m_wetParams.puddleShare) : 1.0f;
    // The ground materials (grass, soil, stone) as the mod gives them to the shader, which uses them only when they are known.
    const Float4 constants[kWetConstants] = {
        { lampsUsable ? m_wetLampSlots : 0.0f, m_wetParams.sheen, m_wetParams.wetness, threshold },
        { facing.toLightView[0], facing.toLightView[1], facing.toLightView[2], m_wetParams.light },
        { m_wetParams.lightColor[0], m_wetParams.lightColor[1], m_wetParams.lightColor[2], exposure },
        { m_wetLampTiles[0], m_wetLampTiles[1], m_wetLampTiles[2], m_wetLampTiles[3] },
        { terrain.uvAtCamera[0], terrain.uvAtCamera[1], terrain.uvPerYard, terrain.sharpness },
        { static_cast<float>(TerrainShadowMap::kSide), TerrainShadowMap::kExtentYards / kSurfaceShadowMapFadeYards,
          -kSurfaceShadowMapMarginYards / kSurfaceShadowMapFadeYards, TerrainShadowMap::kUncoveredGain },
        { cloudMap[0], cloudMap[1], cloudMap[2], cloudMap[3] },
        { cloudShape[0], cloudShape[1], cloudShape[2], cloudy ? cloudShape[3] : 0.0f },
        { static_cast<float>(u - std::floor(u)), static_cast<float>(v - std::floor(v)), static_cast<float>(1.0 / tile), focalPixels },
        { horizon[0], horizon[1], horizon[2], waterKnown ? 1.0f : 0.0f },
        { above[0], above[1], above[2], 0.0f },
        { fog.overcastSky[0], fog.overcastSky[1], fog.overcastSky[2], fog.overcastShare },
        { albedoPerLight[0], albedoPerLight[1], albedoPerLight[2], fog.nightDarkness },
        { SelfLitShare(in), 0.0f, 0.0f, 0.0f },
        { static_cast<float>(u2 - std::floor(u2)), static_cast<float>(v2 - std::floor(v2)), static_cast<float>(turnCos), static_cast<float>(turnSin) },
        { 0.0f, kMaterialTagShare, kMaterialTagYards, kGlintCellYards },
        { 0.3f, 0.0f, 0.0f, 0.0f },
        { 1.0f, 0.45f, 0.6f, 1.0f },
        { 1.0f, 0.05f, 1.0f, 1.0f },
        { 1.0f, 0.0f, 0.0f, kGlintCellShare },
    };
    SetConstants(kWetRegister, &constants[0].x, kWetConstants);
    Float4 exclusions[17] = {};
    const auto bounds=Lumenveil::CurrentWetExclusions();
    exclusions[0].x=static_cast<float>(bounds.size());
    for(size_t i=0;i<bounds.size();++i) {
        exclusions[1+i*2]={bounds[i].low[0]-in.camPos[0],bounds[i].low[1]-in.camPos[1],bounds[i].low[2]-in.camPos[2],0};
        exclusions[2+i*2]={bounds[i].high[0]-in.camPos[0],bounds[i].high[1]-in.camPos[1],bounds[i].high[2]-in.camPos[2],0};
    }
    SetConstants(29,&exclusions[0].x,17);
    for (uint32_t stage = 0; stage < GxEffects::kMaxTextures; ++stage) {
        Bind(stage, nullptr, false);
    }
    Bind(0, m_depth, false);
    Bind(1, lampsUsable ? m_lampTable : nullptr, false);
    Bind(2, lampsUsable && m_wetLampTiles[3] > 0.0f ? m_lampTileTexture : nullptr, false);
    Bind(3, m_sceneCopy, true);
    Bind(4, lampsUsable ? m_lampSurfaces : nullptr, false);
    Bind(5, terrain.uvPerYard > 0.0f ? m_terrainTexture : nullptr, false);
    if (noise) {
        Bind(6, m_cloudNoise, true, true, true);
    }
    Bind(7, waterKnown ? m_waterDepthBefore : nullptr, false);
    Bind(8, fogTexture, true);
    const bool drawn = DrawFullscreen();
    for (uint32_t stage = 0; stage < GxEffects::kMaxTextures; ++stage) {
        Bind(stage, nullptr, false);
    }
    if (drawn && ++m_wetFrames == 1) {
        VF_LOG_INFO("wet surfaces: drawn (sheen %.2f, wetness %.2f, puddles %.2f of the flat ground)", m_wetParams.sheen, m_wetParams.wetness,
                    m_wetParams.puddleShare);
    }
    return drawn;
}

// --- the water ------------------------------------------------------------------------------------------------------

bool LumenveilRenderer::EnsureWaterRainTable() {
    if (m_waterRainTable) {
        return true;
    }
    if (m_waterRainFailed) {
        return false;
    }
    std::vector<uint32_t> texels(static_cast<size_t>(kWaterRainTableTexels) * kWaterRainTableTexels);
    BuildWaterRainTable(texels.data());
    m_waterRainTable = m_device->CreateTexture(kWaterRainTableTexels, kWaterRainTableTexels, GxEffects::Format::BGRA8, false);
    if (!m_waterRainTable || !m_device->Upload(m_waterRainTable, texels.data())) {
        m_device->Destroy(m_waterRainTable);
        m_waterRainTable = nullptr;
        m_waterRainFailed = true;
        VF_LOG_ERROR("water rain table creation failed; no rain rings on the water");
        return false;
    }
    VF_LOG_INFO("water rain: rings on the water from a %dx%d drop table, %.1f yd cells with %d drops each", kWaterRainTableTexels, kWaterRainTableTexels,
                kWaterRainCellYards, kWaterRainSlots);
    return true;
}

bool LumenveilRenderer::FillWaterRippleTable(const WaterRippleConstants& ripples) {
    if (!m_waterRippleTable) {
        if (m_waterRippleFailed) {
            return false;
        }
        m_waterRippleTable = m_device->CreateTexture(kWaterRippleSources, kWaterRippleTableRows, GxEffects::Format::RGBA32F, false);
        if (!m_waterRippleTable) {
            m_waterRippleFailed = true;
            VF_LOG_ERROR("water ripple table creation failed; no ripples around the player");
            return false;
        }
    }
    return m_device->Upload(m_waterRippleTable, ripples.table);
}

bool LumenveilRenderer::EnsureWaterMemory() {
    if (m_waterMemoryFailed) {
        return false;
    }
    if (m_waterMemory[0] && m_waterMemory[1]) {
        return true;
    }
    bool ok = true;
    for (GxEffects::Texture*& memory : m_waterMemory) {
        m_device->Destroy(memory);
        memory = m_device->CreateTexture(kWaterMemorySize, kWaterMemorySize, GxEffects::Format::RGBA32F, true);
        ok = ok && memory;
    }
    if (!ok) {
        for (GxEffects::Texture*& memory : m_waterMemory) {
            m_device->Destroy(memory);
            memory = nullptr;
        }
        m_waterMemoryFailed = true;
        VF_LOG_ERROR("water memory: target creation failed; reflections use the screen alone");
        return false;
    }
    m_waterMemoryValid = false;
    VF_LOG_INFO("water memory: two %ux%u fp32 octahedral maps of the point and colour the camera saw in each direction", kWaterMemorySize, kWaterMemorySize);
    return true;
}

// The mod's UpdateWaterMemory: one pass over the whole map, from the scene before the water and what the map held before,
// moved by how far the camera went. The motion and grid constants (c24, c25) stay set for the reflection pass.
bool LumenveilRenderer::UpdateWaterMemory(const FrameInputs& in, const Config& cfg) {
    const Float4 off[2] = {};
    SetConstants(kWaterMemoryRegister, &off[0].x, 2);
    m_waterMemoryOn = false;
    if (!cfg.waterMemory || !m_waterMemoryProgram) {
        m_waterMemoryValid = false;
        return false;
    }
    if (!EnsureWaterMemory()) {
        return false;
    }
    float moved[3];
    for (int k = 0; k < 3; ++k) {
        moved[k] = in.camPos[k] - m_waterMemoryCamPos[k];
    }
    const float movedYards = std::sqrt(moved[0] * moved[0] + moved[1] * moved[1] + moved[2] * moved[2]);
    const double elapsed = in.seconds - m_waterMemorySeconds;
    const bool history = m_waterMemoryValid && in.mapId == m_waterMemoryMapId && movedYards <= kWaterMemoryForgetYards && elapsed >= 0.0
        && elapsed <= kWaterMemoryForgetSeconds;
    if (!history) {
        ++m_waterMemoryForgets;
    }
    const Float4 constants[2] = {
        { history ? moved[0] : 0.0f, history ? moved[1] : 0.0f, history ? moved[2] : 0.0f, history ? 1.0f : 0.0f },
        { static_cast<float>(kWaterMemorySize), 1.0f / kWaterMemorySize, static_cast<float>(std::sqrt(4.0 * kPi) / kWaterMemorySize), 1.0f },
    };
    SetConstants(kWaterMemoryRegister, &constants[0].x, 2);

    const int write = m_waterMemoryIndex ^ 1;
    SetTarget(m_waterMemory[write]);
    m_state.blend = GxEffects::Blend::Opaque;
    m_state.writeMask = 0xF;
    m_state.program = m_waterMemoryProgram;
    Bind(kWaterMemoryStage, m_waterMemory[m_waterMemoryIndex], false);
    const bool drawn = DrawFullscreen();
    Bind(kWaterMemoryStage, nullptr, false);
    if (!drawn) {
        m_waterMemoryValid = false;
        return false;
    }
    m_waterMemoryIndex = write;
    m_waterMemoryValid = true;
    m_waterMemoryOn = true;
    std::memcpy(m_waterMemoryCamPos, in.camPos, sizeof(m_waterMemoryCamPos));
    m_waterMemorySeconds = in.seconds;
    m_waterMemoryMapId = in.mapId;
    return true;
}

// The wave field levels (256 down to 1 texels) stacked in one atlas a layer: a render target cannot be drawn into a mip
// level here, so Core/shaders/vf_water_slope.hlsl filters the atlas itself.
bool LumenveilRenderer::EnsureWaterWaves() {
    if (m_waterWaveTable) {
        return true;
    }
    if (m_waterWavesFailed) {
        return false;
    }
    m_waterWaveTable = m_device->CreateTexture(kWaterWavesPerLayer, kWaterWaveLayers, GxEffects::Format::RGBA32F, false);
    bool ok = m_waterWaveTable;
    for (GxEffects::Texture*& waves : m_waterWaves) {
        waves = m_device->CreateTexture(kWaterWaveTexels, 2 * kWaterWaveTexels - 1, GxEffects::Format::RGBA16F, true);
        ok = ok && waves;
    }
    if (!ok) {
        for (GxEffects::Texture** t : { &m_waterWaveTable, &m_waterWaves[0], &m_waterWaves[1], &m_waterWaves[2] }) {
            m_device->Destroy(*t);
            *t = nullptr;
        }
        m_waterWavesFailed = true;
        VF_LOG_ERROR("water wave textures could not be made; the water stays flat");
        return false;
    }
    VF_LOG_INFO("water waves: %d layers of %d waves, %dx%d fp16 with %d levels each (atlas)", kWaterWaveLayers, kWaterWavesPerLayer, kWaterWaveTexels,
                kWaterWaveTexels, kWaterWaveLevels);
    return true;
}

bool LumenveilRenderer::DrawWaterWaves(const WaterWaveField& field) {
    float table[kWaterWaveLayers][kWaterWavesPerLayer][4];
    for (int layer = 0; layer < kWaterWaveLayers; ++layer) {
        for (int i = 0; i < kWaterWavesPerLayer; ++i) {
            const WaterWave& w = field.waves[layer][i];
            table[layer][i][0] = w.cyclesX;
            table[layer][i][1] = w.cyclesY;
            table[layer][i][2] = w.slope;
            table[layer][i][3] = w.phase;
        }
    }
    if (!m_device->Upload(m_waterWaveTable, table)) {
        return false;
    }
    m_state.blend = GxEffects::Blend::Opaque;
    m_state.writeMask = 0xF;
    bool ok = true;
    for (int level = 0; level < kWaterWaveLevels; ++level) {
        const uint32_t texels = static_cast<uint32_t>(WaterWaveLevelTexels(level));
        const uint32_t row = 2 * kWaterWaveTexels - (2 * kWaterWaveTexels >> level);
        const Float4 levelConstants[2] = {
            { static_cast<float>(texels), 1.0f / texels, kWaterWaveFadeFromTexels, 1.0f / kWaterWaveFadeWidthTexels },
            { static_cast<float>(row), 0.0f, 0.0f, 0.0f },
        };
        SetConstants(9, &levelConstants[0].x, 2);
        Bind(0, m_waterWaveTable, false);
        for (int layer = 0; layer < kWaterWaveLayers; ++layer) {
            SetTarget(m_waterWaves[layer]);
            m_state.y = static_cast<int32_t>(row);
            m_state.width = texels;
            m_state.height = texels;
            m_state.program = m_waterWaveProgram[layer];
            ok = DrawFullscreen() && ok;
        }
    }
    Bind(0, nullptr, false);
    return ok;
}

bool LumenveilRenderer::EnsureWaterSlopeTarget(uint32_t w, uint32_t h) {
    if (m_waterSlopeTarget && m_waterSlopeW == w && m_waterSlopeH == h) {
        return true;
    }
    m_device->Destroy(m_waterSlopeTarget);
    m_waterSlopeTarget = m_device->CreateTexture(w, h, GxEffects::Format::RGBA16F, true);
    m_waterSlopeW = m_waterSlopeH = 0;
    if (!m_waterSlopeTarget) {
        m_waterWavesFailed = true;
        VF_LOG_ERROR("water slope target could not be made (%ux%u); the water stays flat", w, h);
        return false;
    }
    m_waterSlopeW = w;
    m_waterSlopeH = h;
    return true;
}

bool LumenveilRenderer::CaptureWaterBefore(GxEffects::Device* device, const D3DVIEWPORT9& vp) {
    if (device != m_device) {
        Release();
        m_device = device;
    }
    m_waterBeforeFresh = false;
    if (m_waterFailed || vp.Width < 16 || vp.Height < 16) {
        return false;
    }
    if (!m_waterScene[0] || !m_waterScene[1] || m_waterSceneW != vp.Width || m_waterSceneH != vp.Height) {
        for (GxEffects::Texture*& t : m_waterScene) {
            m_device->Destroy(t);
            t = m_device->CreateTexture(vp.Width, vp.Height, GxEffects::Format::BGRA8, true);
        }
        m_waterSceneW = vp.Width;
        m_waterSceneH = vp.Height;
        if (!m_waterScene[0] || !m_waterScene[1]) {
            m_waterFailed = true;
            VF_LOG_ERROR("water scene copies could not be made (%lux%lu); no Lumenveil water", vp.Width, vp.Height);
            return false;
        }
    }
    m_waterBeforeFresh = m_device->CopyColor(m_waterScene[0], static_cast<int32_t>(vp.X), static_cast<int32_t>(vp.Y), vp.Width, vp.Height, false);
    return m_waterBeforeFresh;
}

bool LumenveilRenderer::DrawWater(GxEffects::Device* device, GxEffects::Texture* depthBefore, GxEffects::Texture* depthAfter, const FrameInputs& in,
                                  const Config& cfg) {
    const bool before = m_waterBeforeFresh && device == m_device;
    m_waterBeforeFresh = false;
    if (!before) {
        return WaterSkip("no scene copy from the start of the liquid pass");
    }
    if (m_waterFailed || m_failed || !EnsurePrograms() || !m_waterProgram || !depthBefore || !depthAfter) {
        return WaterSkip("water shaders or targets unavailable");
    }
    const D3DVIEWPORT9 vp = in.viewport;
    uint32_t targetW = 0;
    uint32_t targetH = 0;
    if (!m_device->TargetSize(targetW, targetH) || vp.Width != m_waterSceneW || vp.Height != m_waterSceneH) {
        return WaterSkip("the viewport changed during the liquid pass");
    }
    float viewToWorld[16];
    float worldToView[16];
    if (!WorldViewFromCameraRelative(in, viewToWorld, worldToView)) {
        return WaterSkip("view matrix not invertible");
    }
    if (!m_device->CopyColor(m_waterScene[1], static_cast<int32_t>(vp.X), static_cast<int32_t>(vp.Y), vp.Width, vp.Height, false)) {
        return WaterSkip("scene copy after the liquid pass failed");
    }
    m_state = State();
    const float* proj = in.glProjection;
    const WorldDepthMapping worldDepth = MapWorldDepth(proj, vp);
    float common[9][4] = {
        { static_cast<float>(vp.X), static_cast<float>(vp.Y), static_cast<float>(vp.Width), static_cast<float>(vp.Height) },
        { 1.0f, static_cast<float>(m_frame % 1024u), 1.0f / targetW, 1.0f / targetH },
        { proj[0], proj[5], proj[8], proj[9] },
        { worldDepth.atInfinity, worldDepth.perInverseViewDepth, std::max(cfg.maxDistance, in.farClip), worldDepth.deepest },
        {},
        {},
        {},
        {},
        {},
    };
    std::memcpy(common[4], viewToWorld, sizeof(viewToWorld));
    SetConstants(0, &common[0][0], 9);
    const WaterParams water = BuildWaterParams(in, cfg);
    float toLight[3] = { in.toLight[0], in.toLight[1], in.toLight[2] };
    Normalize3(toLight);
    // No wave textures yet: flat water. Its glitter spreads as the waves would, so the slope texture is one texel of the
    // wave field's total roughness (the unresolved share plus what the resolved waves would have added).
    const WaterWaveField field = BuildWaterWaveField(water.windSpeed, water.windDirection, water.waves, in.seconds);
    const float roughness = WaterBaseRoughness(field) + field.resolvedMeanSquareSlope;
    if (!m_waterSlopes) {
        m_waterSlopes = m_device->CreateTexture(1, 1, GxEffects::Format::RGBA32F, false);
        m_waterSlopesRoughness = -1.0f;
    }
    if (m_waterSlopes && std::fabs(roughness - m_waterSlopesRoughness) > 1.0e-6f) {
        const float texel[4] = { 0.0f, 0.0f, roughness, 1.0f };
        if (m_device->Upload(m_waterSlopes, texel)) {
            m_waterSlopesRoughness = roughness;
        }
    }
    const bool terrainMap = m_terrainTexture && !m_terrainFallback;
    const TerrainShadowConstants terrain = terrainMap ? m_terrain.Constants(in.camPos) : kTerrainShadowsOff;
    // The wave textures first (their passes use c9 and c10 for themselves).
    const bool waves = water.waves > 0.0f && EnsureWaterWaves() && DrawWaterWaves(field);
    const Float4 waterConstants[7] = {
        { water.reflect, water.clarity, kWaterReflectionYards, WaterFresnel(1.0f) },
        { toLight[0], toLight[1], toLight[2], water.glitter },
        { water.glitterColor[0], water.glitterColor[1], water.glitterColor[2], proj[5] * vp.Height * 0.5f },
        { water.skyHorizon[0], water.skyHorizon[1], water.skyHorizon[2], in.fogStart },
        { water.skyZenith[0], water.skyZenith[1], water.skyZenith[2], 1.0f / std::max(in.fogEnd - in.fogStart, 1.0f) },
        { terrain.uvAtCamera[0], terrain.uvAtCamera[1], terrain.uvPerYard, terrain.sharpness },
        { 0.0f, 0.0f, in.camPos[2], WaterBaseRoughness(field) },
    };
    SetConstants(9, &waterConstants[0].x, 7);
    Bind(0, depthBefore, false);
    Bind(kWaterSceneBeforeStage, m_waterScene[0], true);
    const bool memory = (water.reflect > 0.0f || water.glitter > 0.0f) && UpdateWaterMemory(in, cfg);
    Bind(0, nullptr, false);
    Bind(kWaterSceneBeforeStage, nullptr, false);
    const float rainStrength = WaterRainStrength(cfg.waterRain);
    float rain = rainStrength > 0.0f ? WaterRainActiveShare(in.weather == WeatherKind::Rain, in.weatherIntensity) : 0.0f;
    if (rain > 0.0f && !EnsureWaterRainTable()) {
        rain = 0.0f;
    }
    const WaterRainConstants rainConstants = BuildWaterRainConstants(in.camPos, in.seconds, rain, rainStrength);
    SetConstants(kWaterRainRegister, rainConstants.rain, 1);
    SetConstants(kWaterRainRegister + 1, rainConstants.cell, 1);
    const bool swimming = (in.playerMoveFlags & engine::kMovementSwimming) != 0;
    if (m_waterRipples.Update(in.playerKnown, in.playerPos, swimming, in.seconds)) {
        ++m_waterRippleStarts;
    }
    m_waterRainFrames += rain > 0.0f ? 1u : 0u;
    WaterRippleConstants ripples = m_waterRipples.Constants(in.camPos, in.seconds, water.waves);
    if (ripples.ripples[0] > 0.0f && !FillWaterRippleTable(ripples)) {
        ripples.ripples[0] = 0.0f;
    }
    SetConstants(kWaterRippleRegister, ripples.ripples, 1);
    SetConstants(kWaterRippleRegister + 1, ripples.reach, 1);
    GxEffects::Texture* slopes = m_waterSlopes;
    if (waves && EnsureWaterSlopeTarget(targetW, targetH)) {
        for (int lookup = 0; lookup < kWaterWaveLookups; ++lookup) {
            const double turn = WaterLookupTurnRadians(lookup);
            const double tile = kWaterWaveLayerSpecs[kWaterWaveLookupSpecs[lookup].layer].tileYards;
            double u = 0.0;
            double v = 0.0;
            WaterLookupUv(lookup, in.camPos[0], in.camPos[1], &u, &v);
            const Float4 row = { static_cast<float>(Wrapped(u)), static_cast<float>(Wrapped(v)), static_cast<float>(std::cos(turn) / tile),
                                 static_cast<float>(std::sin(turn) / tile) };
            SetConstants(16 + lookup, &row.x, 1);
        }
        const double firstTurn = WaterLookupTurnRadians(2);
        const double secondTurn = WaterLookupTurnRadians(3);
        const Float4 turns = { static_cast<float>(std::cos(firstTurn)), static_cast<float>(std::sin(firstTurn)), static_cast<float>(std::cos(secondTurn)),
                               static_cast<float>(std::sin(secondTurn)) };
        SetConstants(16 + kWaterWaveLookups, &turns.x, 1);
        SetTarget(m_waterSlopeTarget);
        m_state.x = static_cast<int32_t>(vp.X);
        m_state.y = static_cast<int32_t>(vp.Y);
        m_state.width = vp.Width;
        m_state.height = vp.Height;
        m_state.blend = GxEffects::Blend::Opaque;
        m_state.writeMask = 0xF;
        m_state.program = m_waterSlopeProgram;
        Bind(0, depthBefore, false);
        Bind(1, depthAfter, false);
        for (int layer = 0; layer < kWaterWaveLayers; ++layer) {
            Bind(5 + layer, m_waterWaves[layer], false);
        }
        Bind(kWaterRainStage, rain > 0.0f ? m_waterRainTable : nullptr, false);
        Bind(kWaterRippleStage, ripples.ripples[0] > 0.0f ? m_waterRippleTable : nullptr, false);
        if (DrawFullscreen()) {
            slopes = m_waterSlopeTarget;
        }
        for (int layer = 0; layer < kWaterWaveLayers; ++layer) {
            Bind(5 + layer, nullptr, false);
        }
        Bind(kWaterRainStage, nullptr, false);
        Bind(kWaterRippleStage, nullptr, false);
    }
    SetWorldTarget(vp);
    m_state.blend = GxEffects::Blend::Opaque;
    m_state.writeMask = kColourChannels;
    m_state.program = m_waterProgram;
    Bind(0, depthBefore, false);
    Bind(1, depthAfter, false);
    Bind(2, m_waterScene[0], true);
    Bind(3, m_waterScene[1], false);
    Bind(5, slopes, false);
    bool drawn = DrawFullscreen();
    if (drawn && m_waterReflectProgram && slopes && (water.reflect > 0.0f || water.glitter > 0.0f)) {
        m_state.program = m_waterReflectProgram;
        m_state.blend = GxEffects::Blend::Premultiplied;
        Bind(4, terrainMap && terrain.uvPerYard > 0.0f ? m_terrainTexture : nullptr, !m_terrainFallback);
        Bind(kWaterMemoryStage, memory ? m_waterMemory[m_waterMemoryIndex] : nullptr, false);
        drawn = DrawFullscreen() && drawn;
    }
    // The lamps and lit windows of the last frame's table, mirrored on the water, added over the reflections.
    if (drawn && m_waterLampProgram && !m_waterLampFailed && cfg.waterGlitter > 0.0f && WaterLampsCurrent(m_waterLampRecords, in)) {
        const WaterLampTable lampTable = BuildWaterLampTable(m_waterLampRecords, in);
        if (lampTable.count > 0) {
            if (!m_waterLampTable) {
                m_waterLampTable = m_device->CreateTexture(kWaterLampSlots, kWaterLampTableRows, GxEffects::Format::RGBA32F, false);
                m_waterLampFailed = !m_waterLampTable;
            }
            if (m_waterLampTable && m_device->Upload(m_waterLampTable, lampTable.texels)) {
                const Float4 lamps = { static_cast<float>(lampTable.count), cfg.waterGlitter * kWaterLampGain, 1.0f / kWaterLampSlots, 0.0f };
                SetConstants(kWaterLampRegister, &lamps.x, 1);
                m_state.program = m_waterLampProgram;
                m_state.blend = GxEffects::Blend::Add;
                Bind(5, slopes, false);
                Bind(kWaterLampStage, m_waterLampTable, false);
                drawn = DrawFullscreen() && drawn;
                Bind(kWaterLampStage, nullptr, false);
                if (!m_waterLampsLogged) {
                    m_waterLampsLogged = true;
                    VF_LOG_INFO("water lamps: %d lamps and lit windows mirrored on the water", lampTable.count);
                }
                ++m_waterLampFrames;
            }
        }
    }
    if (!drawn) {
        return WaterSkip("a water pass could not be drawn");
    }
    m_waterWaveFrames += slopes == m_waterSlopeTarget ? 1u : 0u;
    m_waterMemoryFrames += memory ? 1u : 0u;
    if (drawn) {
        m_waterKnown = true;
        m_waterDepthBefore = depthBefore;
    }
    if (drawn && ++m_waterDrawn == 1) {
        VF_LOG_INFO("water: drawn (clarity %.2f, %s, %s)", water.clarity, slopes == m_waterSlopeTarget ? "waves" : "flat", memory ? "memory" : "screen reflections only");
    }
    return drawn;
}

// --- the spell lights -----------------------------------------------------------------------------------------------

void LumenveilRenderer::CaptureEffectBaseline(GxEffects::Device* device, const D3DVIEWPORT9& vp) {
    if (device != m_device) {
        Release();
        m_device = device;
    }
    m_effectBaselineFresh = false;
    if (m_effectsFailed || vp.Width < 16 || vp.Height < 16) {
        return;
    }
    if (!m_effectBaseline || m_effectBaselineW != vp.Width || m_effectBaselineH != vp.Height) {
        m_device->Destroy(m_effectBaseline);
        m_effectBaselineW = m_effectBaselineH = 0;
        m_effectBaseline = m_device->CreateTexture(vp.Width, vp.Height, GxEffects::Format::BGRA8, true);
        if (!m_effectBaseline) {
            m_effectsFailed = true;
            VF_LOG_ERROR("effect light unavailable: the scene copy after the liquid pass could not be made (%lux%lu)", vp.Width, vp.Height);
            return;
        }
        m_effectBaselineW = vp.Width;
        m_effectBaselineH = vp.Height;
    }
    m_effectBaselineFresh = m_device->CopyColor(m_effectBaseline, static_cast<int32_t>(vp.X), static_cast<int32_t>(vp.Y), vp.Width, vp.Height, false);
}

bool LumenveilRenderer::EnsureEffectTargets(uint32_t w, uint32_t h) {
    if (m_effectGather[0] && m_effectW == w && m_effectH == h) {
        return true;
    }
    if (m_effectsFailed) {
        return false;
    }
    for (GxEffects::Texture*& gather : m_effectGather) {
        m_device->Destroy(gather);
        gather = nullptr;
    }
    for (EffectReadback& r : m_effectRing) {
        m_device->CancelRead(r.read);
        m_device->Destroy(r.target);
        r = EffectReadback();
    }
    const uint32_t gatherW = (w + kEffectGatherPixels - 1) / kEffectGatherPixels;
    const uint32_t gatherH = (h + kEffectGatherPixels - 1) / kEffectGatherPixels;
    const uint32_t columns = (gatherW + kEffectReduceTexels - 1) / kEffectReduceTexels;
    const uint32_t rows = (gatherH + kEffectReduceTexels - 1) / kEffectReduceTexels;
    bool ok = true;
    for (GxEffects::Texture*& gather : m_effectGather) {
        gather = m_device->CreateTexture(gatherW, gatherH, GxEffects::Format::RGBA32F, true);
        ok = ok && gather;
    }
    for (EffectReadback& r : m_effectRing) {
        r.target = m_device->CreateTexture(2 * columns, rows, GxEffects::Format::RGBA32F, true);
        ok = ok && r.target;
    }
    if (!ok) {
        for (GxEffects::Texture*& gather : m_effectGather) {
            m_device->Destroy(gather);
            gather = nullptr;
        }
        for (EffectReadback& r : m_effectRing) {
            m_device->Destroy(r.target);
            r = EffectReadback();
        }
        m_effectsFailed = true;
        VF_LOG_ERROR("effect light unavailable: its targets could not be made (%ux%u)", w, h);
        return false;
    }
    m_effectW = w;
    m_effectH = h;
    m_effectGatherW = gatherW;
    m_effectGatherH = gatherH;
    m_effectColumns = columns;
    m_effectRows = rows;
    VF_LOG_INFO("effect light: %ux%u sums of %dx%d pixels, %ux%u cells of %d pixels read back through a ring of %d", gatherW, gatherH,
                kEffectGatherPixels, kEffectGatherPixels, columns, rows, kEffectCellPixels, kEffectReadbackRing);
    return true;
}

// The newest finished readback becomes the measurement; older finished ones are dropped.
bool LumenveilRenderer::TakeEffectMeasurement() {
    EffectReadback* newest = nullptr;
    std::vector<float> texels;
    const size_t rowFloats = static_cast<size_t>(2 * m_effectColumns) * 4;
    for (EffectReadback& r : m_effectRing) {
        if (!r.read) {
            continue;
        }
        std::vector<float> got(rowFloats * m_effectRows);
        if (!m_device->PollRead(r.read, got.data())) {
            continue;
        }
        r.read = nullptr;
        if (!newest || r.order > newest->order) {
            newest = &r;
            texels.swap(got);
        }
    }
    if (!newest) {
        return false;
    }
    m_effectMeasurement.frame = newest->frame;
    m_effectMeasurement.columns = static_cast<int>(m_effectColumns);
    m_effectMeasurement.rows = static_cast<int>(m_effectRows);
    m_effectMeasurement.texels.swap(texels);
    ++m_effectMeasurements;
    return true;
}

void LumenveilRenderer::GatherEffects(const FrameInputs& in) {
    const D3DVIEWPORT9& vp = in.viewport;
    if (!EnsureEffectTargets(vp.Width, vp.Height)) {
        return;
    }
    EffectReadback* slot = nullptr;
    for (EffectReadback& r : m_effectRing) {
        if (!r.read && !slot) {
            slot = &r;
        }
    }
    if (!slot) {
        ++m_effectRingFull;
        return;
    }
    m_state.blend = GxEffects::Blend::Opaque;
    m_state.writeMask = 0xF;
    const Float4 gather[2] = {
        { kEffectThreshold, kEffectNearestYards, kEffectFarthestYards, kEffectDepthChange },
        { 1.0f / vp.Width, 1.0f / vp.Height, 1.0f / (kEffectGatherPixels * kEffectGatherPixels), 0.0f },
    };
    SetConstants(kEffectPassRegister, &gather[0].x, 2);
    Bind(kEffectSceneNowStage, m_sceneCopy, false);
    Bind(kEffectSceneBeforeStage, m_effectBaseline, false);
    Bind(kEffectDepthBeforeStage, m_occluderSource, false);
    Bind(kEffectDepthNowStage, m_depth, false);
    // The mod's gather writes both sums in one pass (two render targets); here it is one pass for each.
    SetTarget(m_effectGather[0]);
    m_state.program = m_effectGatherLight;
    bool drawn = DrawFullscreen();
    SetTarget(m_effectGather[1]);
    m_state.program = m_effectGatherPlace;
    drawn = DrawFullscreen() && drawn;

    SetTarget(slot->target);
    m_state.program = m_effectReduce;
    const Float4 reduce[2] = {
        { static_cast<float>(m_effectColumns), 1.0f / (kEffectReduceTexels * kEffectReduceTexels), 0.0f, 0.0f },
        { static_cast<float>(m_effectGatherW), static_cast<float>(m_effectGatherH), 1.0f / m_effectGatherW, 1.0f / m_effectGatherH },
    };
    SetConstants(kEffectPassRegister, &reduce[0].x, 2);
    for (uint32_t stage = 0; stage < GxEffects::kMaxTextures; ++stage) {
        Bind(stage, nullptr, false);
    }
    Bind(kEffectLightStage, m_effectGather[0], false);
    Bind(kEffectPlaceStage, m_effectGather[1], false);
    drawn = DrawFullscreen() && drawn;
    if (!drawn) {
        return;
    }
    slot->read = m_device->BeginRead(slot->target, 0, 0, 2 * m_effectColumns, m_effectRows);
    if (!slot->read) {
        return;
    }
    slot->issuedFrame = m_frame;
    slot->order = ++m_effectOrder;
    EffectFrame& f = slot->frame;
    std::memcpy(f.cameraRelativeView, in.cameraRelativeView, sizeof(f.cameraRelativeView));
    std::memcpy(f.glProjection, in.glProjection, sizeof(f.glProjection));
    std::memcpy(f.camPos, in.camPos, sizeof(f.camPos));
    f.viewportWidth = static_cast<float>(vp.Width);
    f.viewportHeight = static_cast<float>(vp.Height);
    f.seconds = in.seconds;
    f.mapId = in.mapId;
    f.weatherIntensity = in.weather == WeatherKind::Clear ? 0.0f : in.weatherIntensity;
    f.falling = in.weather == WeatherKind::Rain || in.weather == WeatherKind::Snow;
    const LanternLamp& lantern = m_lantern.Lamp();
    EffectFramePlayer(in, lantern.pivotMeasured ? lantern.pivotHeight : kDefaultPivotHeight, f);
    f.targetKnown = in.targetKnown;
    if (in.targetKnown) {
        std::memcpy(f.targetFeet, in.targetPos, sizeof(f.targetFeet));
        f.targetRingYards = EffectTargetRingYards(in.targetBoundingRadius);
    }
    f.anchorCount = EffectUnitAnchors(in.units, in.unitCount, f.anchors);
    ++m_effectGathers;
}

const std::vector<EffectLamp>* LumenveilRenderer::UpdateEffects(const FrameInputs& in, const FogParams& fog, bool gather) {
    if (!(fog.effectLight > 0.0f) || m_effectsFailed) {
        m_effectTracker.Reset();
        for (EffectReadback& r : m_effectRing) {
            m_device->CancelRead(r.read);
            r.read = nullptr;
        }
        return nullptr;
    }
    const bool measured = TakeEffectMeasurement() && m_effectMeasurement.frame.mapId == in.mapId;
    std::vector<EffectCluster> clusters;
    if (measured) {
        clusters = ClusterEffects(m_effectMeasurement, GlobalLampCatalog().Map(in.mapId), nullptr);
    }
    m_effectTracker.Update(measured ? &clusters : nullptr, in.camPos, in.seconds, in.mapId, measured ? m_effectMeasurement.frame.seconds : -1.0,
                           in.units, in.unitCount);
    if (gather) {
        GatherEffects(in);
    }
    return &m_effectTracker.Lights();
}

// The sun's or moon's shadow of what is on screen: the lamp shadow march aimed at the light, then the lamps' depth-aware
// blur to smooth its dither. The composite darkens by the surface shade where this says shadow.
bool LumenveilRenderer::RenderLightShadow(const FrameInputs& in, const float* toLightInView, bool fine) {
    if (!m_lightShadowProgram || !m_lampBlurProgram || m_lightShadowFailed) {
        return false;
    }
    const uint32_t w = in.viewport.Width;
    const uint32_t h = in.viewport.Height;
    if (!m_lightShadow[0] || m_lightShadowW != w || m_lightShadowH != h) {
        for (GxEffects::Texture*& t : m_lightShadow) {
            m_device->Destroy(t);
            t = m_device->CreateTexture(w, h, GxEffects::Format::RGBA16F, true);
        }
        m_lightShadowW = w;
        m_lightShadowH = h;
        if (!m_lightShadow[0] || !m_lightShadow[1]) {
            m_lightShadowFailed = true;
            VF_LOG_ERROR("light shadow target creation failed (%ux%u); no sun or moon shadow from objects", w, h);
            return false;
        }
        VF_LOG_INFO("light shadow: %ux%u, %.0f yd march toward the sun or moon", w, h, kLightShadowYards);
    }
    m_state.blend = GxEffects::Blend::Opaque;
    m_state.writeMask = 0xF;
    SetTarget(m_lightShadow[0]);
    m_state.program = m_lightShadowProgram;
    const Float4 light = { toLightInView[0], toLightInView[1], toLightInView[2], kLightShadowYards };
    SetConstants(kLampPassRegister, &light.x, 1);
    for (uint32_t stage = 0; stage < GxEffects::kMaxTextures; ++stage) {
        Bind(stage, nullptr, false);
    }
    Bind(0, m_depth, false);
    Bind(2, m_occluderSource, false);
    // With the fine memory running, what the camera has seen along the light also shadows.
    const Float4 memoryOn = { fine ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
    SetConstants(10, &memoryOn.x, 1);
    if (fine) {
        SetConstants(60, &m_fine.lookup[0][0], 4);
        SetConstants(64, &m_fine.beforeLookup[0][0], 4);
        Bind(kEdgeOccluderStage, m_fine.tex[m_fine.index], false, true);
        Bind(kShaftMemoryBeforeStage, m_fine.before, false, true);
    }
    bool drawn = DrawFullscreen();
    m_state.program = m_lampBlurProgram;
    Bind(2, nullptr, false);
    Bind(kEdgeOccluderStage, nullptr, false);
    Bind(kShaftMemoryBeforeStage, nullptr, false);
    BlurLampLight(m_lightShadow[0], m_lightShadow[1], 1.0f, 0.0f);
    BlurLampLight(m_lightShadow[1], m_lightShadow[0], 0.0f, 1.0f);
    for (uint32_t stage = 0; stage < GxEffects::kMaxTextures; ++stage) {
        Bind(stage, nullptr, false);
    }
    static int s_logged = -1;
    if (s_logged != (drawn ? 1 : 0)) {
        s_logged = drawn ? 1 : 0;
        VF_LOG_INFO("light shadow: %s (toward the light %.2f %.2f %.2f in view)", drawn ? "drawn" : "DRAW FAILED", toLightInView[0], toLightInView[1],
                    toLightInView[2]);
    }
    return drawn;
}

// --- the night lights -----------------------------------------------------------------------------------------------

bool LumenveilRenderer::EnsureLampTargets(uint32_t lowW, uint32_t lowH) {
    if (m_lampHalos && m_lampW == lowW && m_lampH == lowH) {
        return true;
    }
    if (m_lampsFailed) {
        return false;
    }
    m_device->Destroy(m_lampHalos);
    m_lampHalos = nullptr;
    m_lampW = m_lampH = 0;
    if (!m_lampTable) {
        m_lampTable = m_device->CreateTexture(kLampSlots, kLampTableRows, GxEffects::Format::RGBA32F, false);
    }
    for (GxEffects::Texture*& visibility : m_lampVisibility) {
        if (!visibility) {
            visibility = m_device->CreateTexture(kLampSlots, 1, GxEffects::Format::BGRA8, true);
        }
    }
    m_lampHalos = m_device->CreateTexture(lowW, lowH, GxEffects::Format::RGBA16F, true);
    if (!m_lampTable || !m_lampVisibility[0] || !m_lampVisibility[1] || !m_lampHalos) {
        for (GxEffects::Texture** t : { &m_lampTable, &m_lampVisibility[0], &m_lampVisibility[1], &m_lampHalos }) {
            m_device->Destroy(*t);
            *t = nullptr;
        }
        m_lampsFailed = true;
        VF_LOG_ERROR("lamp halo target creation failed (%ux%u); no lamp halos", lowW, lowH);
        return false;
    }
    m_lampW = lowW;
    m_lampH = lowH;
    m_lamps.ForgetVisibility();
    VF_LOG_INFO("lamp halo targets: %ux%u (fp16), lamp table %dx%d", lowW, lowH, kLampSlots, kLampTableRows);
    return true;
}

bool LumenveilRenderer::EnsureLampSurfaceTarget(uint32_t w, uint32_t h) {
    if (m_lampSurfaces && m_lampSurfaceW == w && m_lampSurfaceH == h) {
        return true;
    }
    if (m_lampSurfacesFailed) {
        return false;
    }
    m_device->Destroy(m_lampSurfaces);
    m_device->Destroy(m_lampSurfacesBlur);
    m_lampSurfacesBlur = nullptr;
    m_lampSurfaceW = m_lampSurfaceH = 0;
    m_lampSurfaces = m_device->CreateTexture(w, h, GxEffects::Format::RGBA16F, true);
    if (!m_lampSurfaces) {
        m_lampSurfacesFailed = true;
        VF_LOG_ERROR("lamp light target creation failed (%ux%u); no lamp light on surfaces", w, h);
        return false;
    }
    m_lampSurfaceW = w;
    m_lampSurfaceH = h;
    m_lampSurfacesBlur = m_device->CreateTexture(w, h, GxEffects::Format::RGBA16F, true);
    VF_LOG_INFO("lamp light target: %ux%u (fp16), %s", w, h, m_lampSurfacesBlur ? "shadow edges smoothed" : "no second target, shadow edges not smoothed");
    return true;
}

bool LumenveilRenderer::UploadLampTiles(const LampTable& table, const FrameInputs& in) {
    if (m_lampTilesFailed) {
        return false;
    }
    m_lampTiles.Build(table, in.glProjection, in.viewport, FarCoreAnglePerTexel(in, static_cast<int>(m_lowH)));
    const uint32_t w = m_lampTiles.TextureWidth();
    const uint32_t h = m_lampTiles.TextureHeight();
    if (!m_lampTileTexture || m_lampTileW != w || m_lampTileH != h) {
        m_device->Destroy(m_lampTileTexture);
        m_lampTileTexture = m_device->CreateTexture(w, h, GxEffects::Format::R8, false);
        if (!m_lampTileTexture) {
            m_lampTilesFailed = true;
            VF_LOG_ERROR("lamp tile list creation failed (%ux%u); every pixel tests every lamp", w, h);
            return false;
        }
        m_lampTileW = w;
        m_lampTileH = h;
        VF_LOG_INFO("lamp tiles: %dx%d tiles of %d px, lists in a %ux%u L8 texture", m_lampTiles.TilesX(), m_lampTiles.TilesY(), kLampTilePixels, w, h);
    }
    m_lampTileBytes.resize(static_cast<size_t>(w) * h);
    m_lampTiles.Write(m_lampTileBytes.data(), static_cast<int>(w), 1);
    return m_device->Upload(m_lampTileTexture, m_lampTileBytes.data());
}

void LumenveilRenderer::BlurLampLight(GxEffects::Texture* source, GxEffects::Texture* destination, float stepX, float stepY) {
    Bind(kLampBlurSourceStage, source, false);
    SetTarget(destination);
    const Float4 blur = { stepX, stepY, 0.0f, 0.0f };
    SetConstants(kLampPassRegister, &blur.x, 1);
    DrawFullscreen();
}

// The mod's RenderLamps without the water lamp records.
void LumenveilRenderer::RenderLamps(GxEffects::Texture* fogTexture, const FrameInputs& in, const Config& cfg, const FogParams& fog) {
    // The spell lights compare this scene (the client's glowing effects drawn) with the baseline from the liquid pass.
    const bool baselineFresh = m_effectBaselineFresh;
    m_effectBaselineFresh = false;
    const bool effectScene = baselineFresh && m_occluderSource != m_depth && fog.effectLight > 0.0f && !m_effectsFailed && CopyWorldViewport(in.viewport);
    const bool halosBefore = m_lampsDrawn;
    m_lampsDrawn = false;
    m_lampSurfacesDrawn = false;
    m_wetLampSlots = 0.0f;
    if (!halosBefore) {
        m_lamps.ForgetVisibility();
    }
    if (!m_lampProgram) {
        return;
    }
    const LanternLamp& lantern = m_lantern.Update(in, cfg, fog.lanternLight);
    const std::vector<EffectLamp>& corpses = m_corpses.Update(in, fog.corpseLight);
    const std::vector<EffectLamp>* effects = UpdateEffects(in, fog, effectScene);
    m_carried.clear();
    if (effects && fog.effectLight > 0.0f) {
        for (EffectLamp e : *effects) {
            e.intensity *= fog.effectLight;
            m_carried.push_back(e);
        }
    }
    m_carried.insert(m_carried.end(), corpses.begin(), corpses.end());
    std::stable_sort(m_carried.begin(), m_carried.end(), [](const EffectLamp& a, const EffectLamp& b) { return a.intensity > b.intensity; });
    const LampTable& table = m_lamps.Update(GlobalLampCatalog(), in, cfg, fog.lampHaze, fog.lampLight, fog.windowLight, fog.lanternGlow, &lantern,
                                            fog.lanternLight, m_carried.empty() ? nullptr : &m_carried, 1.0f, fog.farLightCore);
    std::bitset<kLampSlots> carriedSlots;
    for (const EffectLamp& carried : m_carried) {
        const int slot = m_lamps.EffectSlot(carried.id);
        if (slot >= 0) {
            carriedSlots.set(static_cast<size_t>(slot));
        }
    }
    m_waterLampRecords = RecordWaterLamps(table, in, fog.lampHaze, fog.lampLight, carriedSlots);
    const bool sunFacing = std::max({ fog.lowSunLift[0], fog.lowSunLift[1], fog.lowSunLift[2] }) > 0.0f;
    const bool lamps = table.slots > 0 && EnsureLampTargets(m_lowW, m_lowH) && m_device->Upload(m_lampTable, table.texels);
    if (!lamps && !sunFacing) {
        return;
    }
    const bool tiled = lamps && m_lampTilesWanted && UploadLampTiles(table, in);
    const float tileTexels[2] = { tiled ? 1.0f / static_cast<float>(m_lampTileW) : 0.0f, tiled ? 1.0f / static_cast<float>(m_lampTileH) : 0.0f };
    const float tilePixels = tiled ? static_cast<float>(kLampTilePixels) : 0.0f;
    m_wetLampSlots = lamps ? static_cast<float>(table.surfaceSlots) : 0.0f;
    m_wetLampTiles[0] = tileTexels[0];
    m_wetLampTiles[1] = tileTexels[1];
    m_wetLampTiles[2] = 0.0f;
    m_wetLampTiles[3] = tilePixels;

    m_state.blend = GxEffects::Blend::Opaque;
    m_state.writeMask = 0xF;
    if ((sunFacing || fog.lampLight > 0.0f || fog.windowLight > 0.0f || m_lamps.Stats().lanternDrawn || m_lamps.Stats().effectsDrawn > 0)
        && EnsureLampSurfaceTarget(in.viewport.Width, in.viewport.Height)) {
        SetTarget(m_lampSurfaces);
        m_state.program = m_lampSurfaceProgram;
        const SunFacing facing = DaylightFacing(in);
        const CarrierBody carrier = CarrierBodyInView(in, lantern);
        const Float4 surfaces[kLampSurfaceConstants] = {
            { lamps ? static_cast<float>(table.surfaceSlots) : 0.0f, 0.0f, 0.0f, 0.0f },
            { facing.toLightView[0], facing.toLightView[1], facing.toLightView[2], 0.0f },
            { carrier.base[0], carrier.base[1], carrier.base[2], carrier.radius },
            { carrier.up[0], carrier.up[1], carrier.up[2], carrier.height },
            { tileTexels[0], tileTexels[1], 0.0f, tilePixels },
        };
        SetConstants(kLampPassRegister, &surfaces[0].x, kLampSurfaceConstants);
        Bind(kLampSurfaceDepthStage, m_depth, false);
        Bind(kLampSurfaceTableStage, lamps ? m_lampTable : nullptr, false);
        Bind(kLampSurfaceOccluderStage, m_occluderSource, false);
        Bind(kLampSurfaceTileStage, tiled ? m_lampTileTexture : nullptr, false);
        DrawFullscreen();
        if (lamps && m_lampSurfacesBlur) {
            m_state.program = m_lampBlurProgram;
            BlurLampLight(m_lampSurfaces, m_lampSurfacesBlur, 1.0f, 0.0f);
            BlurLampLight(m_lampSurfacesBlur, m_lampSurfaces, 0.0f, 1.0f);
        }
        m_lampSurfacesDrawn = true;
    }
    if (lamps && (fog.lampHaze > 0.0f || fog.farLightCore > 0.0f)) {
        const int previous = m_lampVisibilityIndex;
        const int current = previous ^ 1;
        SetTarget(m_lampVisibility[current]);
        m_state.program = m_lampVisibilityProgram;
        const Float4 visibility = { m_lamps.VisibilityBlend(), 0.0f, 0.0f, 0.0f };
        SetConstants(kLampPassRegister, &visibility.x, 1);
        Bind(kLampVisibilityDepthStage, m_depth, false);
        Bind(kLampPreviousVisibilityStage, m_lampVisibility[previous], false);
        Bind(kLampVisibilityTableStage, m_lampTable, false);
        DrawFullscreen();
        m_lampVisibilityIndex = current;

        SetTarget(m_lampHalos);
        m_state.program = m_lampProgram;
        const Float4 pass[2] = {
            { static_cast<float>(table.slots), FarCoreAnglePerTexel(in, static_cast<int>(m_lowH)), 0.0f, 0.0f },
            { tileTexels[0], tileTexels[1], static_cast<float>(m_lampTiles.TilesY() * kLampTileBlockHeight), tilePixels },
        };
        SetConstants(kLampPassRegister, &pass[0].x, 2);
        Bind(kLampDepthStage, m_depth, false);
        Bind(kLampFogStage, fogTexture, false);
        Bind(kLampVisibilityStage, m_lampVisibility[current], false);
        Bind(kLampTableStage, m_lampTable, false);
        Bind(kLampTileStage, tiled ? m_lampTileTexture : nullptr, false);
        DrawFullscreen();
        m_lampsDrawn = true;
    }
    for (uint32_t stage = 0; stage < GxEffects::kMaxTextures; ++stage) {
        Bind(stage, nullptr, false);
    }
}

// --- the frame ------------------------------------------------------------------------------------------------------

bool LumenveilRenderer::Render(GxEffects::Device* device, GxEffects::Texture* depth, GxEffects::Texture* occluderDepth, uint32_t targetW,
                               uint32_t targetH, const FrameInputs& in, const Config& cfg, const char** skip) {
    if (device != m_device) {
        Release();
        m_device = device;
    }
    if (m_failed) {
        return Skip("shader creation failed", skip);
    }
    if (!EnsurePrograms()) {
        return Skip("shader creation failed", skip);
    }
    m_depth = depth;
    m_occluderSource = occluderDepth ? occluderDepth : depth;
    m_targetW = targetW;
    m_targetH = targetH;
    m_state = State();

    const D3DVIEWPORT9 vp = in.viewport;
    if (vp.Width < 16 || vp.Height < 16 || vp.X + vp.Width > targetW || vp.Y + vp.Height > targetH) {
        return Skip("world viewport outside the render target", skip);
    }

    const uint32_t scale = cfg.quality == 1 ? 4u : 2u;
    const uint32_t lowW = (vp.Width + scale - 1) / scale;
    const uint32_t lowH = (vp.Height + scale - 1) / scale;
    const uint32_t rayW = std::max(1u, static_cast<uint32_t>(vp.Width) / kRayScale);
    const uint32_t rayH = std::max(1u, static_cast<uint32_t>(vp.Height) / kRayScale);
    if (!EnsureTargets(lowW, lowH, rayW, rayH)) {
        return Skip("render target creation failed", skip);
    }

    float viewToWorld[16];
    float worldToView[16];
    if (!WorldViewFromCameraRelative(in, viewToWorld, worldToView)) {
        return Skip("view matrix not invertible", skip);
    }

    AuthoredFog authored = {};
    const bool hasAuthored = cfg.dataMode == 1 && GlobalFogData().Resolve(in.mapId, in.camPos, in.dayFraction, in.lightParams, authored);
    const FogParams fog = BuildFogParams(in, cfg, hasAuthored ? &authored : nullptr);
    AdvanceDrift(in, fog, cfg);
    const bool driftNoise = EnsureDriftNoise();
    AdvanceClouds(in, fog);
    const bool cloudNoise = cfg.cloudShadows > 0.0f && EnsureCloudNoise();
    const CloudLookup clouds = CloudConstants(in, fog, m_cloudOffset, cloudNoise);
    const bool cloudy = clouds.shape.w > 0.0f;
    if (!EnsureTerrainTexture()) {
        return Skip("terrain shadow texture creation failed", skip);
    }
    const TerrainShadowConstants terrain = UpdateTerrainShadows(in, cfg);
    const TerrainShadowConstants marchTerrain = cfg.terrainShadows ? terrain : kTerrainShadowsOff;
    float surfaceShade[3];
    SurfaceShadowShade(in, cfg, surfaceShade);
    const bool lowSunLift = std::max({ fog.lowSunLift[0], fog.lowSunLift[1], fog.lowSunLift[2] }) > 0.0f;
    const bool surfaceShadows = terrain.uvPerYard > 0.0f
        && ((cfg.surfaceShadows > 0.0f && std::min({ surfaceShade[0], surfaceShade[1], surfaceShade[2] }) < 1.0f) || lowSunLift || cloudy);
    const float* proj = in.glProjection;
    const WorldDepthMapping worldDepth = MapWorldDepth(proj, vp);

    float toLightInView[3];
    TransformDirection(in.toLight, in.cameraRelativeView, toLightInView);
    Normalize3(toLightInView);

    float common[9][4] = {
        { static_cast<float>(vp.X), static_cast<float>(vp.Y), static_cast<float>(vp.Width), static_cast<float>(vp.Height) },
        { static_cast<float>(scale), static_cast<float>(m_frame % 1024u), 1.0f / targetW, 1.0f / targetH },
        { proj[0], proj[5], proj[8], proj[9] },
        { worldDepth.atInfinity, worldDepth.perInverseViewDepth, fog.maxDistance, worldDepth.deepest },
        {},
        {},
        {},
        {},
        { static_cast<float>(lowW), static_cast<float>(lowH), 1.0f / lowW, 1.0f / lowH },
    };
    std::memcpy(common[4], viewToWorld, sizeof(viewToWorld));

    const float dx = in.camPos[0] - m_prevCam[0];
    const float dy = in.camPos[1] - m_prevCam[1];
    const float dz = in.camPos[2] - m_prevCam[2];
    const bool historyValid = m_historyValid && cfg.temporal > 0.0f && m_prevScale == scale && std::memcmp(&m_prevViewport, &vp, sizeof(vp)) == 0
        && in.seconds - m_prevSeconds < kHistoryMaxSeconds && dx * dx + dy * dy + dz * dz < kHistoryMaxMove * kHistoryMaxMove;
    float reproj[16];
    float prevViewProj[16];
    Mul4x4(m_prevWorldToView, m_prevProj, prevViewProj);
    Mul4x4(viewToWorld, prevViewProj, reproj);

    float sunPx[2] = {};
    const bool sunInFront = toLightInView[2] > 0.05f;
    float sunScreenFade = 0.0f;
    if (sunInFront) {
        const float ndcX = toLightInView[0] / toLightInView[2] * proj[0] + proj[8];
        const float ndcY = toLightInView[1] / toLightInView[2] * proj[5] + proj[9];
        sunPx[0] = vp.X + (ndcX * 0.5f + 0.5f) * vp.Width;
        sunPx[1] = vp.Y + (0.5f - ndcY * 0.5f) * vp.Height;
        sunScreenFade = std::clamp(1.6f - std::max(std::fabs(ndcX), std::fabs(ndcY)), 0.0f, 1.0f);
    }
    const float rayStrength = cfg.godRays * fog.lightVisibility * sunScreenFade;
    const bool rays = rayStrength > 0.005f;

    // Once a minute, and whenever the sun and the moon change places (the time of day jumped, or a night began).
    const bool summaryDue = in.seconds - m_summarySeconds > kSummarySeconds || m_summaryLight != (in.lightIsMoon ? 1 : 0);
    if (summaryDue) {
        m_summarySeconds = in.seconds;
        m_summaryLight = in.lightIsMoon ? 1 : 0;
        LogSummary(in, fog, viewToWorld, toLightInView, sunPx, rayStrength);
        LogWater(in, cfg);
    }

    m_state.blend = GxEffects::Blend::Opaque;
    m_state.writeMask = 0xF;
    SetConstants(0, &common[0][0], 9);

    const bool shadowed = cfg.lightShafts && (fog.authored ? fog.lightAboveHorizon : fog.lightVisibility) > 0.001f;
    const CarrierBody shaftBody = ShaftBodyInView(in, m_lantern.Lamp().pivotHeight);
    const bool memory = shadowed && cfg.shaftMemory && UpdateShaftMemory(in, shaftBody);
    const bool lightUp = (fog.authored ? fog.lightAboveHorizon : fog.lightVisibility) > 0.001f;
    // Off: a map filled from the screen leaves shadows where moving things stood, and its splat points leave holes (see LUMENVEIL_PORT.md).
    m_fineActive = false && lightUp && cfg.lightShadow > 0.0f && cfg.surfaceShadows > 0.0f && UpdateFineShadowMemory(in, shaftBody);
    if (summaryDue) {
        VF_LOG_INFO("  shafts: %s, strength %.3f focus %.2f, range %.0f depth %.1f length %.1f glare %.2f, %s", shadowed ? "on" : "off (light hidden)",
                    fog.shaftStrength, fog.shaftFocus, cfg.shaftRange, cfg.shaftDepth, cfg.shaftLength, cfg.shaftGlare,
                    memory ? "from the shaft memory" : (cfg.shaftMemory ? "from the edge occluders (no shaft memory)" : "from the edge occluders"));
    }
    if (shadowed && cfg.shaftRange > 0.0f && !memory) {
        RenderEdgeOccluders(vp, shaftBody);
    }
    SetTarget(m_marchTarget);
    const int quality = std::clamp(cfg.quality, 1, 3) - 1;
    m_state.program = memory ? m_marchMemory[quality] : m_march[quality];
    for (uint32_t stage = 0; stage < GxEffects::kMaxTextures; ++stage) {
        Bind(stage, nullptr, false);
    }
    const Float4 march[3] = {
        { toLightInView[0], toLightInView[1], toLightInView[2], fog.shadowedLayerLightScale },
        { kShadowMinStep, kShadowStepPerYard, shadowed ? 1.0f : 0.0f, kShadowThicknessSteps },
        { 1.0f, 1.0f / std::max(fog.maxDistance, 1.0f), fog.horizonStart, fog.farClip },
    };
    SetConstants(9, &march[0].x, 3);
    static_assert(sizeof(FogLayer) == 6 * sizeof(Float4), "FogLayer is six shader registers");
    FogLayer marchLayers[kFogLayers];
    std::memcpy(marchLayers, fog.layers, sizeof(marchLayers));
    if (!driftNoise) {
        for (FogLayer& l : marchLayers) {
            l.bankShare = 0.0f;
        }
    }
    SetConstants(12, &marchLayers[0].start, 6 * kFogLayers);
    const float shaftRange = shadowed ? cfg.shaftRange : 0.0f;
    const Float4 shafts[2] = {
        { fog.shaftStrength, fog.shaftFocus, shaftRange, cfg.shaftDepth },
        { fog.airShaftScatter[0], fog.airShaftScatter[1], fog.airShaftScatter[2], cfg.shaftLength },
    };
    static_assert(kShaftConstantsRegister == 12 + 6 * kFogLayers, "the shaft constants follow the fog layers");
    SetConstants(kShaftConstantsRegister, &shafts[0].x, 2);
    const DriftConstants drift = BuildDriftConstants(in, fog, cfg, m_coarseDrift, m_fineDrift, driftNoise);
    SetConstants(kDriftConstantsRegister, &drift.coarse.x, kDriftConstantCount);
    const Float4 terrainAndFade[2] = {
        { marchTerrain.uvAtCamera[0], marchTerrain.uvAtCamera[1], marchTerrain.uvPerYard, marchTerrain.sharpness },
        ShaftFadeConstants(shaftRange, cfg.shaftDepth, cfg.shaftGlare),
    };
    SetConstants(kTerrainShadowRegister, &terrainAndFade[0].x, 2);
    SetConstants(kCloudRegister, &clouds.map.x, kCloudConstants);
    const BodyConstants marchBody = BodyConstantsOf(shaftBody);
    SetConstants(kShaftBodyRegister, &marchBody.base.x, kShaftBodyConstants);
    const Float4 hazeFromSky = { fog.hazeFromSky[0], fog.hazeFromSky[1], fog.hazeFromSky[2], fog.hazeFromSkyShare };
    SetConstants(kHazeFromSkyRegister, &hazeFromSky.x, 1);
    const bool measureSky = fog.linear && fog.hazeFromSkyShare > 0.0f && cfg.debugView == 0;
    const bool skyReference = measureSky && SkyReferenceFresh(in);
    const Float4 skyReferenceUsed = { skyReference ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
    SetConstants(kSkyReferenceRegister, &skyReferenceUsed.x, 1);
    Bind(kSkyReferenceStage, skyReference ? m_skyReference[m_skyReferenceIndex] : nullptr, true);
    const bool banks = driftNoise && fog.bankStrength > 0.0f;
    const bool bankFloors = (banks || cfg.fogCeiling > 0.0f) && UploadBankFloors() && m_terrain.FloorsKnown();
    BankConstants bankConstants = BuildBankConstants(in, fog, terrain, bankFloors);
    if (!banks) {
        bankConstants.misc.y = 0.0f;
    }
    const FogCeilingConstants ceiling = UpdateFogCeiling(in, cfg, bankFloors);
    bankConstants.noise.y = ceiling.perYard;
    bankConstants.noise.z = ceiling.strength;
    bankConstants.noise.w = ceiling.groundAtCamera;
    SetConstants(kBankRegister, &bankConstants.floor.x, kBankConstants);
    Bind(kBankFloorStage, bankFloors ? m_bankFloorTexture : nullptr, true);
    Bind(kCloudStage, cloudy ? m_cloudNoise : nullptr, true, true, true);
    Bind(0, m_depth, false);
    Bind(kDriftNoiseStage, driftNoise ? m_driftNoise : nullptr, true, true, true);
    Bind(kTerrainShadowStage, m_terrainTexture, !m_terrainFallback);
    if (memory) {
        SetConstants(kShaftMemoryRegister, &m_memoryLookup[0][0], kShaftMemoryConstants);
        SetConstants(kShaftMemoryBeforeRegister, &m_memoryBeforeLookup[0][0], kShaftMemoryConstants);
        Bind(kEdgeOccluderStage, m_memory[m_memoryIndex], false, true);
        Bind(kShaftMemoryBeforeStage, m_memoryBefore, false, true);
    } else {
        Bind(kEdgeOccluderStage, m_edgeOccluders, true);
    }
    DrawFullscreen();

    const int write = m_historyIndex ^ 1;
    SetTarget(m_history[write]);
    m_state.program = m_temporal;
    const Float4 temporal = { cfg.temporal, historyValid ? 1.0f : 0.0f, 0.0f, 0.0f };
    SetConstants(9, reproj, 4);
    SetConstants(13, &temporal.x, 1);
    Bind(0, m_marchTarget, false);
    Bind(1, m_history[m_historyIndex], true);
    Bind(2, m_depth, false);
    DrawFullscreen();

    RenderLamps(m_history[write], in, cfg, fog);

    if (rays) {
        m_device->CopyColor(m_rays[0], static_cast<int32_t>(vp.X), static_cast<int32_t>(vp.Y), vp.Width, vp.Height, true);
        SetTarget(m_rays[1]);
        m_state.program = m_rayMask;
        const Float4 mask[2] = {
            { toLightInView[0], toLightInView[1], toLightInView[2], kRayFalloff },
            { static_cast<float>(vp.Width) / rayW, kRayThreshold, 1.0f / rayW, 1.0f / rayH },
        };
        SetConstants(9, &mask[0].x, 2);
        Bind(0, m_depth, false);
        Bind(1, m_rays[0], true);
        DrawFullscreen();

        float norm = 0.0f;
        for (int k = 0; k < kRayTaps; ++k) {
            norm += std::pow(kRayDecay, static_cast<float>(k));
        }
        const float sunUv[2] = { (sunPx[0] - vp.X) / vp.Width, (sunPx[1] - vp.Y) / vp.Height };
        m_state.program = m_rayBlur;
        float step = kRayStep;
        for (int pass = 0; pass < 2; ++pass) {
            SetTarget(m_rays[pass == 0 ? 0 : 1]);
            const Float4 blur[2] = {
                { sunUv[0], sunUv[1], step, 1.0f / norm },
                { static_cast<float>(rayW), static_cast<float>(rayH), 1.0f / rayW, 1.0f / rayH },
            };
            SetConstants(9, &blur[0].x, 2);
            Bind(0, m_rays[pass == 0 ? 1 : 0], true);
            DrawFullscreen();
            step /= kRayTaps;
        }
    }

    const bool lightGlow = fog.lightGlow > kMinLightGlow && RenderLightGlow(vp, cfg);
    // Objects and characters shade the ground from the sun or moon (the terrain map only knows the hills).
    const bool screenShadow = !m_stockObjectShadows && cfg.lightShadow > 0.0f && cfg.surfaceShadows > 0.0f && std::min({ surfaceShade[0], surfaceShade[1], surfaceShade[2] }) < 0.999f
        && RenderLightShadow(in, toLightInView, m_fineActive);

    FogBlend blend = FogBlend::GammaFixedFunction;
    if (fog.linear) {
        blend = CopyWorldViewport(vp) ? FogBlend::LinearOverSceneCopy : FogBlend::LinearFixedFunction;
    }
    const bool sceneBlend = blend == FogBlend::LinearOverSceneCopy;
    const float blendMode = static_cast<float>(blend);
    if (blendMode != m_loggedBlendMode) {
        m_loggedBlendMode = blendMode;
        VF_LOG_INFO("fog blend: %s", FogBlendName(blend));
    }

    m_wetDrawn = RenderWetSurfaces(m_history[write], in, cfg, fog, terrain, &clouds.map.x, &clouds.shape.x,
                                   fog.authored ? cfg.classicExposure : cfg.exposure, sceneBlend);
    SetWorldTarget(vp);
    m_state.blend = sceneBlend ? GxEffects::Blend::Opaque : GxEffects::Blend::Premultiplied;
    m_state.writeMask = kColourChannels;
    m_state.program = m_composite;
    const Float4 composite[4] = {
        { fog.authored ? cfg.classicExposure : cfg.exposure, rays ? rayStrength : 0.0f, static_cast<float>(cfg.debugView), blendMode },
        { fog.rayColor[0], fog.rayColor[1], fog.rayColor[2], 0.0f },
        { sunPx[0], sunPx[1], cfg.sunMarker && sunInFront ? 1.0f : 0.0f, cfg.glowCompensation && sceneBlend ? in.clientGlowAmount : 0.0f },
        { lightGlow ? fog.lightGlow * kLightGlowGain : 0.0f, kLightGlowCeiling, fog.hazeFromSkyShare, 0.0f },
    };
    SetConstants(9, &composite[0].x, 4);
    const Float4 surface[4] = {
        { terrain.uvAtCamera[0], terrain.uvAtCamera[1], terrain.uvPerYard, terrain.sharpness },
        { surfaceShade[0], surfaceShade[1], surfaceShade[2], surfaceShadows ? 1.0f : 0.0f },
        { static_cast<float>(TerrainShadowMap::kSide), TerrainShadowMap::kExtentYards / kSurfaceShadowMapFadeYards,
          -kSurfaceShadowMapMarginYards / kSurfaceShadowMapFadeYards, TerrainShadowMap::kUncoveredGain },
        { TerrainShadowMap::kOutwardLookupCells, terrain.sharpness > 0.0f ? -1.0f / (terrain.sharpness * TerrainShadowMap::kCellYards) : 0.0f,
          0.0f, 0.0f },
    };
    SetConstants(kCompositeSurfaceShadowRegister, &surface[0].x, 4);
    // No surface before the late draws and no wet surfaces yet (Renderer.h).
    const Float4 lamps = { m_lampsDrawn ? 1.0f : 0.0f, SelfLitShare(in), 0.0f, m_wetDrawn ? 1.0f : 0.0f };
    SetConstants(kCompositeLampRegister, &lamps.x, 1);
    float albedoPerLight[3];
    AlbedoPerSceneLight(in, albedoPerLight);
    float sceneLight[3];
    SceneLight(in, sceneLight);
    for (float& light : sceneLight) {
        light = std::sqrt(light);
    }
    const SunFacing lampFacing = DaylightFacing(in);
    const Float4 lampLight[3] = {
        { m_lampSurfacesDrawn ? 1.0f : 0.0f, kFixedFunctionAlbedo, kAlbedoCap, fog.nightDarkness },
        { albedoPerLight[0], albedoPerLight[1], albedoPerLight[2], m_lampSurfacesDrawn ? lampFacing.atZero : 1.0f },
        { sceneLight[0], sceneLight[1], sceneLight[2], m_lampSurfacesDrawn ? lampFacing.perFacing : 0.0f },
    };
    SetConstants(kCompositeLampSurfaceRegister, &lampLight[0].x, 3);
    // LightShadow scales how dark the screen-space shadow goes (0 off, 1 as dark as the surface shade).
    const Float4 screenShade = { 1.0f + (surfaceShade[0] - 1.0f) * cfg.lightShadow, 1.0f + (surfaceShade[1] - 1.0f) * cfg.lightShadow,
                                 1.0f + (surfaceShade[2] - 1.0f) * cfg.lightShadow, screenShadow ? 1.0f : 0.0f };
    SetConstants(kCompositeScreenShadeRegister, &screenShade.x, 1);
    const Float4 overcast = { fog.overcastSky[0], fog.overcastSky[1], fog.overcastSky[2], fog.overcastShare };
    SetConstants(kCompositeOvercastRegister, &overcast.x, 1);
    const Float4 lowSun[2] = {
        { fog.lowSunLift[0], fog.lowSunLift[1], fog.lowSunLift[2], 0.0f },
        { in.toLight[0], in.toLight[1], in.toLight[2], 0.0f },
    };
    SetConstants(kCompositeLowSunRegister, &lowSun[0].x, 2);
    float cloudShade[3];
    CloudShadowShade(in, cloudShade);
    const Float4 cloudComposite[kCompositeCloudConstants] = {
        clouds.map,
        clouds.shape,
        { cloudShade[0], cloudShade[1], cloudShade[2], 0.0f },
    };
    SetConstants(kCompositeCloudRegister, &cloudComposite[0].x, kCompositeCloudConstants);
    bool balanced = false;
    for (int i = 0; i < 3; ++i) {
        balanced = balanced || fog.whiteBalance[i] != 1.0f || fog.skyWhiteBalance[i] != 1.0f;
    }
    const bool graded = fog.saturation != 1.0f || fog.skySaturation != 1.0f || balanced;
    const Float4 grade = { fog.saturation, fog.skySaturation, graded ? 1.0f : 0.0f, fog.skyHazeGlowGrade };
    SetConstants(kCompositeGradeRegister, &grade.x, 1);
    const Float4 balance = { fog.lightBalance[0], fog.lightBalance[1], fog.lightBalance[2], fog.lateDrawsInFront };
    SetConstants(kCompositeBalanceRegister, &balance.x, 1);
    const Float4 skyGlow = { fog.skyGlow[0], fog.skyGlow[1], fog.skyGlow[2], kLateDrawYards };
    SetConstants(kCompositeSkyGlowRegister, &skyGlow.x, 1);
    const Float4 whiteBalance[kCompositeWhiteBalanceConstants] = {
        { fog.whiteBalance[0], fog.whiteBalance[1], fog.whiteBalance[2], 0.0f },
        { fog.skyWhiteBalance[0], fog.skyWhiteBalance[1], fog.skyWhiteBalance[2], fog.skyHazeZenith },
    };
    SetConstants(kCompositeWhiteBalanceRegister, &whiteBalance[0].x, kCompositeWhiteBalanceConstants);
    const Float4 sunAndMoonShown = { 0.0f, 0.0f, 0.0f, 0.0f };
    SetConstants(kCompositeSunAndMoonRegister, &sunAndMoonShown.x, 1);
    for (uint32_t stage = 0; stage < GxEffects::kMaxTextures; ++stage) {
        Bind(stage, nullptr, false);
    }
    Bind(0, m_depth, false);
    Bind(1, m_history[write], false);
    Bind(2, m_rays[1], true);
    Bind(3, sceneBlend ? m_sceneCopy : nullptr, false);
    Bind(kLightGlowStage, lightGlow ? m_glow[0] : nullptr, true);
    Bind(kCompositeWetStage, m_wetDrawn ? m_wetTarget : nullptr, false);
    Bind(kCompositeScreenShadowStage, screenShadow ? m_lightShadow[0] : nullptr, true);
    Bind(kCompositeLampHaloStage, m_lampsDrawn ? m_lampHalos : nullptr, false);
    Bind(kCompositeLampSurfaceStage, m_lampSurfacesDrawn ? m_lampSurfaces : nullptr, false);
    Bind(kCompositeTerrainShadowStage, surfaceShadows ? m_terrainTexture : nullptr, false);
    Bind(kCompositeCloudStage, cloudy && surfaceShadows ? m_cloudNoise : nullptr, true, true, true);
    if (!DrawFullscreen()) {
        return Skip("the composite could not be drawn", skip);
    }
    if (measureSky) {
        MeasureSky(in, cfg, fog);
    } else {
        m_skyReferenceValid = false;
    }

    std::memcpy(m_prevWorldToView, worldToView, sizeof(m_prevWorldToView));
    std::memcpy(m_prevProj, in.glProjection, sizeof(m_prevProj));
    std::memcpy(m_prevCam, in.camPos, sizeof(m_prevCam));
    m_prevViewport = vp;
    m_prevScale = scale;
    m_prevSeconds = in.seconds;
    m_historyIndex = write;
    m_historyValid = true;
    ++m_frame;
    return true;
}

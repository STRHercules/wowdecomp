#include "vf_lamp_table.hlsli"
#include "vf_lamp_tiles.hlsli"
#include "vf_surface_normal.hlsli"
#include "vf_clouds.hlsli"
#include "vf_material_tag.hlsli"

float4 cWet : register(c9);
float4 cLightInView : register(c10);
float4 cLightColour : register(c11);
float4 cLampTiles : register(c12);
float4 cTerrainMap : register(c13);
float4 cTerrainReach : register(c14);
float4 cCloudMap : register(c15);
float4 cCloudShape : register(c16);
float4 cPuddleMap : register(c17);
float4 cSkyHorizon : register(c18);
float4 cSkyAbove : register(c19);
float4 cOvercastSky : register(c20);
float4 cSceneLight : register(c21);
float4 cSelfLit : register(c22);
float4 cPuddleTurn : register(c23);
float4 cMaterials : register(c24);
float4 cGrass : register(c25);
float4 cSoil : register(c26);
float4 cStone : register(c27);
float4 cGlints : register(c28);

sampler2D sLampTable : register(s1);
sampler2D sLampTiles : register(s2);
sampler2D sSurface : register(s3);
sampler2D sLampLight : register(s4);
sampler2D sTerrain : register(s5);
sampler2D sNoise : register(s6);
sampler2D sDepthBeforeWater : register(s7);
sampler2D sFog : register(s8);
sampler2D sMaterial : register(s9);
sampler2D sMaterialTag : register(s10);
float4 cWetExclusionCount : register(c29);
float4 cWetExclusionBounds[16] : register(c30);

static const float4 kUnchanged = float4(0, 0, 0, 1);
static const float kPi = 3.14159265;
static const float3 kLuminanceWeights = float3(0.2126, 0.7152, 0.0722);

static const float kMinAlbedo = 1e-3;
static const float kStoneChroma = 0.12;
static const float kDirtChroma = 0.45;
static const float kGreenSlope = 4;
static const float kGlossFromAlbedo = 0.04;
static const float kGlossFullAlbedo = 0.3;
static const float kSheenPores = 0.25;

static const float kDryRoughness = 0.5;
static const float kWetRoughness = 0.22;
static const float kPuddleRoughness = 0.05;
static const float kSurfaceF0 = 0.04;
static const float kWaterF0 = 0.02;
static const float kSunAlpha = 0.01;
static const float kMinCosine = 0.02;
static const float kMaxSunSpecular = 16;
static const float kMaxLampSpecular = 256;
static const float kLampLobeWidening = 1.0 / 3.0;

static const float kSheenFadeFrom = 150;
static const float kSheenFadeTo = 400;
static const float kReflectFadeFrom = 300;
static const float kReflectFadeTo = 600;
static const float kWallWetShare = 0.5;
static const float kThinWetShare = 0.4;
static const float kWetTransmission = 0.5145;
static const float kWaterInternalReflectance = 0.475;
static const float kDropCover = 0.3;
static const float kSparkleCover = 0.3;
static const float kSparkleBelowDarkening = 0.3;
static const float kGlintRoughness = 1;
static const float kMinShine = 1e-3;

static const float kPuddleFlatFrom = 0.94;
static const float kPuddleFlatFull = 0.985;
static const float kPuddleEdge = 0.04;
static const float kDipPerYard = 0.15;
static const float kMaxDip = 0.2;
static const float kPuddleShiftWithoutWater = 1;
static const float kPuddleGroundTop = 0.75;
static const float kPuddleGroundOff = 1.5;
static const float kNoiseTexels = 256;

static const float kUnknownGround = 5e4;
static const float kCoverClearance = 3;
static const float kOutwardLookupCells = 1;
static const float kShelterDepth = 3;
static const float kShelterRamp = 2;

static const float kNearYards = 0.5;
static const float kFalloffPerYard = 0.7;
static const float kFalloffPerSquareYard = 0.03;
static const float kWrap = 0.15;
static const float kThinFacing = 0.6;
static const float kWindowWrap = 0.1;
static const float kMinUnshadowedLight = 1e-4;

static const int kReflectionSteps = 20;
static const int kRefineSteps = 4;
static const float kFirstStepYards = 0.3;
static const float kStepGrowth = 1.43;
static const float kMaxReflectionYards = 400;
static const float kNearViewZ = 0.5;
static const float kMinThickness = 0.5;
static const float kThicknessPerViewZ = 0.02;
static const float kThicknessPerStep = 1.5;
static const float kStartLift = 0.05;
static const float kEdgeFadePixels = 32;
static const float kReachFadeShare = 0.25;
static const float kConeYardsPerYard = 1;
static const float kMaxBlurPixels = 24;
static const float kSkyFromRise = -0.05;
static const float kSkyFullRise = 0.1;
static const float kSelfLitFrom = 0.9;
static const float kSelfLitSpan = 0.9;
static const float kMinReflectivity = 1e-3;

float LampSlots()
{
    return cWet.x;
}

float DrySheen()
{
    return cWet.y;
}

float Wetness()
{
    return cWet.z;
}

float PuddleThreshold()
{
    return cWet.w;
}

float Exposure()
{
    return cLightColour.w;
}

bool WaterMaskKnown()
{
    return cSkyHorizon.w > 0;
}

float3 GammaToLinear(float3 colour)
{
    return pow(colour, 2.2);
}

float Pow5(float x)
{
    float x2 = x * x;
    return x2 * x2 * x;
}

float3 UpInView()
{
    return float3(cViewToWorld[0].z, cViewToWorld[1].z, cViewToWorld[2].z);
}

float FocalPixels()
{
    return 0.5 * ViewToNdcScale().y * ViewportSize().y;
}

bool OnWater(float2 pixel, float depth)
{
    return WaterMaskKnown() && SampleDepth(sDepthBeforeWater, pixel) != depth;
}

float SpecularLobe(float3 n, float3 v, float3 l, float alpha, float f0, float cap)
{
    float NoL = saturate(dot(n, l));
    float NoV = max(dot(n, v), kMinCosine);
    float3 sum = v + l;
    float3 h = sum * rsqrt(max(dot(sum, sum), 1e-8));
    float NoH = saturate(dot(n, h));
    float VoH = saturate(dot(v, h));
    float a2 = alpha * alpha;
    float d = NoH * NoH * (a2 - 1) + 1;
    float distribution = a2 / (kPi * d * d);
    float visibility = 0.5 / (NoL * sqrt(NoV * NoV * (1 - a2) + a2) + NoV * sqrt(NoL * NoL * (1 - a2) + a2));
    float fresnel = f0 + (1 - f0) * Pow5(1 - VoH);
    return min(kPi * distribution * visibility * fresnel * NoL, cap);
}

float EnvironmentSpecular(float f0, float roughness, float NoV)
{
    const float4 c0 = float4(-1, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1, 0.0425, 1.04, -0.04);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    float2 scaleAndBias = float2(-1.04, 1.04) * a004 + r.zw;
    return f0 * scaleAndBias.x + scaleAndBias.y;
}

float4 SiteUnderTheSky(float3 fromCamera, float3 worldNormal)
{
    float4 site = float4(1, 1, 0, 0);
    [branch] if (cTerrainMap.z > 0)
    {
        float2 uv = cTerrainMap.xy + fromCamera.xy * cTerrainMap.z;
        float2 texel = uv * cTerrainReach.x - 0.5 + worldNormal.xy * kOutwardLookupCells;
        float2 firstCell = floor(texel);
        float2 fraction = texel - firstCell;
        float heightTerm = (CameraPositionWorld().z + fromCamera.z) * cTerrainMap.w;
        static const float2 kCorners[4] = { float2(0, 0), float2(1, 0), float2(0, 1), float2(1, 1) };
        float weightedLight = 0;
        float openWeight = 0;
        float rainOpen = 0;
        float4 ground = 0;
        [unroll] for (int k = 0; k < 4; k++)
        {
            float2 corner = kCorners[k];
            float2 cellUv = (firstCell + corner + 0.5) / cTerrainReach.x;
            float2 cell = tex2Dlod(sTerrain, float4(cellUv, 0, 0)).rg;
            ground[k] = cell.y;
            float2 lightAndOpen = saturate(heightTerm + cell);
            float2 bilinear = lerp(1 - fraction, fraction, corner);
            float weight = bilinear.x * bilinear.y * lightAndOpen.y;
            weightedLight += weight * lightAndOpen.x;
            openWeight += weight;
            float aboveTop = (heightTerm + cell.y - 1) / cTerrainMap.w - kCoverClearance;
            rainOpen += bilinear.x * bilinear.y * saturate((aboveTop + kShelterDepth) / kShelterRamp);
        }
        float light = openWeight > 1e-4 ? weightedLight / openWeight : 1;
        float2 edgeDistance = min(uv, 1 - uv);
        float edgeFade = saturate(min(edgeDistance.x, edgeDistance.y) * cTerrainReach.y + cTerrainReach.z);
        float groundHere =
            lerp(lerp(ground.x, ground.y, fraction.x), lerp(ground.z, ground.w, fraction.x), fraction.y);
        bool known = max(max(abs(ground.x), abs(ground.y)), max(abs(ground.z), abs(ground.w))) < kUnknownGround;
        float aboveGround = (heightTerm + groundHere - 1) / cTerrainMap.w - kCoverClearance;
        site = float4(lerp(1, light, edgeFade), lerp(1, saturate(rainOpen * cTerrainReach.w), edgeFade),
                      known ? aboveGround : 0, known ? 1 : 0);
    }
    return site;
}

float PuddleAt(float3 fromCamera, float up, float4 site, float yards, float allowance)
{
    float puddle = 0;
    [branch] if (PuddleThreshold() < 1 && up > kPuddleFlatFrom)
    {
        float2 uv = cPuddleMap.xy + fromCamera.xy * cPuddleMap.z;
        float2 turnedUv = cPuddleTurn.xy + float2(dot(fromCamera.xy, float2(cPuddleTurn.z, -cPuddleTurn.w)),
                                                  dot(fromCamera.xy, cPuddleTurn.wz));
        float texelsPerYard = yards * kNoiseTexels / cPuddleMap.w;
        float lod = log2(max(texelsPerYard * cPuddleMap.z, 1));
        float turnedLod = log2(max(texelsPerYard * length(cPuddleTurn.zw), 1));
        float dip = clamp(site.z * kDipPerYard, -kMaxDip, kMaxDip);
        float noise = tex2Dlod(sNoise, float4(uv, 0, lod)).r + tex2Dlod(sNoise, float4(turnedUv, 0, turnedLod)).r;
        float n = 0.5 * noise - dip - kPuddleShiftWithoutWater * (1 - saturate(allowance));
        float pooled = smoothstep(PuddleThreshold() - kPuddleEdge, PuddleThreshold() + kPuddleEdge, n);
        float flat = smoothstep(kPuddleFlatFrom, kPuddleFlatFull, up);
        float onGround = site.w > 0 ? 1 - smoothstep(kPuddleGroundTop, kPuddleGroundOff, abs(site.z)) : 1;
        puddle = pooled * flat * onGround * site.y;
    }
    return puddle;
}

float3 LampHighlights(float2 pixel, float2 viewportUv, float3 surface, float4 normal, float3 specNormal,
                      float3 toEye, float alpha, float f0)
{
    float shadowed = dot(tex2Dlod(sLampLight, float4(viewportUv, 0, 0)).rgb, kLuminanceWeights);
    float3 diffuse = 0;
    float3 specular = 0;
    float2 tile = LampTileOf(cLampTiles, pixel);
    float listed = shadowed > 0 ? LampTileListed(sLampTiles, cLampTiles, tile) : 0;
    float3 reflected = reflect(-toEye, specNormal);
    bool every = listed > kLampTileCapacity;
    float count = every ? LampSlots() : listed;
    float entry = 0;
    [loop] while (entry < count)
    {
        float slot = every ? entry : LampTileEntry(sLampTiles, cLampTiles, tile, entry + 1);
        float slotU = SlotU(slot);
        float4 lamp = LampTexel(sLampTable, slotU, kLampViewRow);
        float3 toLamp = lamp.xyz - surface;
        float distanceSquared = dot(toLamp, toLamp);
        float reachSquared = lamp.w * lamp.w;
        [branch] if (distanceSquared < reachSquared)
        {
            float4 light = LampTexel(sLampTable, slotU, kLampSurfaceRow);
            float4 emit = LampTexel(sLampTable, slotU, kLampEmitRow);
            float distance = sqrt(distanceSquared);
            float3 direction = toLamp / max(distance, 1e-4);
            float nearYards = emit.w != 0 ? abs(emit.w) : kNearYards;
            float spread = distanceSquared + nearYards * nearYards;
            float falloff = 1 / (kFalloffPerYard * sqrt(spread) + kFalloffPerSquareYard * spread);
            float outward = emit.w > 0 ? saturate((kWindowWrap - dot(emit.xyz, direction)) / (1 + kWindowWrap)) : 1;
            float window = saturate(1 - distanceSquared * distanceSquared / (reachSquared * reachSquared));
            float3 arriving = light.rgb * (outward * window * window * falloff);
            float lambert = saturate((dot(normal.xyz, direction) + kWrap) / (1 + kWrap));
            diffuse += arriving * (normal.w > 0 ? max(lambert, kThinFacing) : lambert);
            float3 centreToRay = dot(toLamp, reflected) * reflected - toLamp;
            float rayGap = rsqrt(max(dot(centreToRay, centreToRay), 1e-8));
            float3 closest = toLamp + centreToRay * saturate(nearYards * rayGap);
            float3 representative = closest * rsqrt(max(dot(closest, closest), 1e-8));
            float widened = saturate(alpha + kLampLobeWidening * nearYards / max(distance, 1e-3));
            float keptEnergy = alpha * alpha / (widened * widened);
            float lobe = SpecularLobe(specNormal, toEye, representative, alpha, f0, kMaxLampSpecular / keptEnergy);
            specular += arriving * arriving * (keptEnergy * lobe);
        }
        entry += 1;
    }
    float unshadowed = dot(diffuse, kLuminanceWeights);
    return specular * saturate(shadowed / max(unshadowed, kMinUnshadowedLight));
}

struct Material
{
    float4 wetting;
    float glint;
};

Material GroundMaterial(float2 pixel, float yards, float green, float stone, bool thin)
{
    float grass = thin ? 1 : green;
    Material m;
    m.wetting = lerp(lerp(cSoil, cStone, stone), cGrass, grass);
    m.glint = lerp(lerp(cGlints.y, cGlints.z, stone), cGlints.x, grass);
    [branch] if (cMaterials.x > 0)
    {
        float2 uv = pixel * DepthTexelSize();
        float4 tag = tex2Dlod(sMaterialTag, float4(uv, 0, 0));
        bool same = abs(DecodeMaterialTag(tag.xy) - yards) <= cMaterials.y * yards + cMaterials.z;
        float unknown = 1 - tag.z;
        float4 wetting = tex2Dlod(sMaterial, float4(uv, 0, 0)) + unknown * m.wetting;
        float glint = tag.w + unknown * m.glint;
        m.wetting = same ? wetting : m.wetting;
        m.glint = same ? glint : m.glint;
    }
    return m;
}

float Mod289(float x)
{
    return x - floor(x / 289) * 289;
}

float Permute(float x)
{
    return Mod289((34 * x + 1) * x);
}

float GlintMask(float3 world, float3 halfWorld, float yards)
{
    float footprint = yards / max(FocalPixels(), 1);
    float cell = cMaterials.w * exp2(ceil(log2(max(footprint / cMaterials.w, 1))));
    float ratio = cell / cMaterials.w;
    float share = saturate(cGlints.w * ratio * ratio);
    float3 index = floor(world / cell);
    float2 facing = floor(halfWorld.xy * 4 + 4);
    float h = Permute(Mod289(index.x) + Permute(Mod289(index.y) + Permute(Mod289(index.z + 9 * facing.x + facing.y))));
    return h / 289 < share ? 1 / max(share, 1e-3) : 0;
}

float Moonlit(float3 colour)
{
    float albedoLuminance = dot(colour * cSceneLight.rgb, kLuminanceWeights);
    return 1 - saturate((albedoLuminance * cSelfLit.x - kSelfLitFrom) / kSelfLitSpan);
}

float3 AsShown(float3 colour, float2 pixel, bool sky)
{
    float4 fog = tex2Dlod(sFog, float4(LowResTexelToUv(FullPixelToLowResTexel(pixel)), 0, 0));
    float3 kept = colour * (1 - cSceneLight.w * Moonlit(colour));
    float3 under = sky ? lerp(kept, cOvercastSky.rgb * Exposure(), cOvercastSky.w) : kept;
    return fog.rgb * Exposure() + (1 - fog.a) * under;
}

float3 SurfaceAt(float2 pixel, float radius)
{
    float2 uvPerPixel = 1 / ViewportSize();
    float2 uv = (pixel - ViewportOrigin()) * uvPerPixel;
    float2 offset = 0.5 * radius * uvPerPixel;
    float3 sum = tex2Dlod(sSurface, float4(uv + float2(offset.x, offset.y), 0, 0)).rgb +
                 tex2Dlod(sSurface, float4(uv + float2(-offset.x, offset.y), 0, 0)).rgb +
                 tex2Dlod(sSurface, float4(uv + float2(offset.x, -offset.y), 0, 0)).rgb +
                 tex2Dlod(sSurface, float4(uv + float2(-offset.x, -offset.y), 0, 0)).rgb;
    return GammaToLinear(0.25 * sum);
}

float3 ReflectedLight(float3 surface, float3 reflected, float alpha, float2 pixel)
{
    float jitter = InterleavedGradientNoise(pixel);
    float reach = kMaxReflectionYards;
    [flatten] if (reflected.z < 0)
        reach = min(reach, max(surface.z - kNearViewZ, 0) / -reflected.z);
    float travel = kFirstStepYards * pow(kStepGrowth, jitter);
    float previous = 0;
    float found = 0;
    float marching = 1;
    [loop] for (int i = 0; i < kReflectionSteps && marching > 0; i++)
    {
        float3 probe = surface + reflected * travel;
        float2 probePixel = ViewToPixel(probe);
        bool gone = travel > reach || probe.z < kNearViewZ || OutsideViewport(probePixel);
        float sceneZ = LinearDepth(SampleDepth(sDepth, probePixel));
        float behind = probe.z - sceneZ;
        float thickness = max(kMinThickness + kThicknessPerViewZ * sceneZ,
                              (travel - previous) * abs(reflected.z) * kThicknessPerStep);
        bool hit = !gone && behind > 0 && behind < thickness;
        found = hit ? 1 : 0;
        marching = gone || hit ? 0 : 1;
        previous = marching > 0 ? travel : previous;
        travel = marching > 0 ? travel * kStepGrowth : travel;
    }
    float3 reflectedWorld = ViewToWorldDirection(reflected);
    float skyShare = smoothstep(kSkyFromRise, kSkyFullRise, reflectedWorld.z);
    float3 gradient = lerp(cSkyHorizon.rgb, cSkyAbove.rgb, sqrt(saturate(reflectedWorld.z)));
    float3 fallback = lerp(gradient, cOvercastSky.rgb * Exposure(), cOvercastSky.w) * skyShare;
    float2 source = clamp(ViewToPixel(reflected), ViewportOrigin() + 0.5, ViewportLastPixelCentre());
    float weight = reflected.z > 0 && IsSky(SampleDepth(sDepth, source)) ? 1 : 0;
    bool fromSky = true;
    float radius = 0;
    [branch] if (found > 0)
    {
        float low = previous;
        float high = travel;
        [loop] for (int r = 0; r < kRefineSteps; r++)
        {
            float middle = 0.5 * (low + high);
            float3 probe = surface + reflected * middle;
            bool beyond = probe.z > LinearDepth(SampleDepth(sDepth, ViewToPixel(probe)));
            high = beyond ? middle : high;
            low = beyond ? low : middle;
        }
        float3 hitView = surface + reflected * high;
        source = ViewToPixel(hitView);
        fromSky = BeyondFarClip(SampleDepth(sDepth, source));
        radius = min(high * alpha * kConeYardsPerYard * FocalPixels() / max(hitView.z, kNearViewZ), kMaxBlurPixels);
        float2 edge = min(source - ViewportOrigin(), ViewportOrigin() + ViewportSize() - source);
        weight = saturate(min(edge.x, edge.y) / kEdgeFadePixels) *
                 saturate((1 - high / kMaxReflectionYards) / kReachFadeShare);
    }
    float3 sampled = 0;
    [branch] if (weight > 0)
        sampled = AsShown(SurfaceAt(source, radius), source, fromSky) * (found > 0 ? 1 : skyShare);
    return lerp(fallback, sampled, weight);
}

float4 main(float2 targetPixel : VPOS) : COLOR0
{
    float2 pixel = ViewportOrigin() + targetPixel + 0.5;
    float depth = SampleDepth(sDepth, pixel);
    [branch] if (BeyondFarClip(depth) || OnWater(pixel, depth))
        return kUnchanged;
    float3 surface = ViewRayAtUnitDepth(pixel) * LinearDepth(depth);
    float yards = length(surface);
    float3 toEye = -surface / yards;
    float4 normal = SurfaceNormal(pixel, surface);
    bool thin = normal.w > 0;
    float3 fromCamera = ViewToWorldDirection(surface);
    [unroll] for (int excluded=0;excluded<8;++excluded)
    {
        if (excluded<cWetExclusionCount.x && all(fromCamera>=cWetExclusionBounds[excluded*2].xyz) &&
            all(fromCamera<=cWetExclusionBounds[excluded*2+1].xyz)) return kUnchanged;
    }
    float3 worldNormal = ViewToWorldDirection(normal.xyz);
    float2 viewportUv = (pixel - ViewportOrigin()) / ViewportSize();

    float3 albedo = GammaToLinear(tex2Dlod(sSurface, float4(viewportUv, 0, 0)).rgb) * cSceneLight.rgb;
    float brightest = max(albedo.r, max(albedo.g, albedo.b));
    float chroma = (brightest - min(albedo.r, min(albedo.g, albedo.b))) / max(brightest, kMinAlbedo);
    float green = saturate((albedo.g - max(albedo.r, albedo.b)) / max(brightest, kMinAlbedo) * kGreenSlope);
    float stone = (1 - smoothstep(kStoneChroma, kDirtChroma, chroma)) * (1 - green);
    float luminance = dot(albedo, kLuminanceWeights);
    Material m = GroundMaterial(pixel, yards, green, stone, thin);
    float darkens = m.wetting.x;
    float pores = m.wetting.y;

    float4 site = SiteUnderTheSky(fromCamera, worldNormal);
    float up = saturate(worldNormal.z);
    float wet = Wetness() * site.y * lerp(kWallWetShare, 1, up) * (thin ? kThinWetShare : 1);
    float film = m.wetting.z * saturate((wet - pores) / max(1 - pores, 1e-3));
    float puddle = thin ? 0 : PuddleAt(fromCamera, up, site, yards, m.wetting.w);
    float glossy = m.wetting.z * saturate(1 - pores / kSheenPores) *
                   smoothstep(kGlossFromAlbedo, kGlossFullAlbedo, luminance);
    float dryShine = DrySheen() * glossy * (1 - smoothstep(kSheenFadeFrom, kSheenFadeTo, yards));
    float filmShine = max(max(dryShine, film), puddle);
    float glintShine = m.glint * (wet * kDropCover +
                                  DrySheen() * saturate(1 - darkens / kSparkleBelowDarkening) * kSparkleCover);
    float wetRatio = kWetTransmission / (1 - kWaterInternalReflectance * saturate(luminance));
    float darkening = lerp(1, wetRatio, saturate(darkens * wet + puddle));
    [branch] if (max(filmShine, glintShine) < kMinShine)
        return float4(0, 0, 0, darkening);

    bool glinting = glintShine > filmShine;
    float roughness = glinting ? kGlintRoughness
                               : lerp(lerp(kDryRoughness, kWetRoughness, saturate(film)), kPuddleRoughness, puddle);
    float alpha = roughness * roughness;
    float f0 = glinting ? kWaterF0 : lerp(kSurfaceF0, kWaterF0, puddle);
    float3 specNormal = normalize(lerp(normal.xyz, UpInView(), puddle));
    float shine = filmShine;
    [branch] if (glinting)
    {
        float3 halfView = normalize(toEye + cLightInView.xyz);
        shine = glintShine * GlintMask(CameraPositionWorld() + fromCamera, ViewToWorldDirection(halfView), yards);
    }
    [branch] if (shine < kMinShine)
        return float4(0, 0, 0, darkening);

    float clouded = 0;
    [branch] if (CloudStrength(cCloudShape) > 0)
        clouded = CloudStrength(cCloudShape) *
                  CloudCoverAt(sNoise, cCloudMap, cCloudShape, fromCamera, CameraPositionWorld().z, yards);
    float sunlit = cLightInView.w * site.x * site.y * (1 - clouded);
    float sunLobe = SpecularLobe(specNormal, toEye, cLightInView.xyz, max(alpha, kSunAlpha), f0, kMaxSunSpecular);
    float3 highlights = cLightColour.rgb * (sunlit * sunLobe);
    [branch] if (LampSlots() > 0)
        highlights += LampHighlights(pixel, viewportUv, surface, normal, specNormal, toEye, alpha, f0);

    float3 reflection = 0;
    float reflectivity = glinting ? 0 : max(film, puddle) * (1 - smoothstep(kReflectFadeFrom, kReflectFadeTo, yards));
    [branch] if (reflectivity > kMinReflectivity && !thin)
    {
        float3 reflected = reflect(-toEye, specNormal);
        float3 start = surface + specNormal * kStartLift;
        reflection = ReflectedLight(start, reflected, alpha, pixel) * reflectivity *
                     EnvironmentSpecular(f0, roughness, max(dot(specNormal, toEye), kMinCosine));
    }
    return float4(highlights * shine + reflection, darkening);
}

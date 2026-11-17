// Not in the original binary. The sky's panorama (SkyAtmosphere.cpp): the whole sky around the camera, worked out once
// and drawn from by the dome, the depth fog and (through the sky capture) the water. Two targets:
//   0: the clear sky, finished (gamma, 0..1): the atmosphere, tinted toward the zone's bands, the zone's night sky.
//   1: the clouds, finished and premultiplied (rgb * alpha, alpha the opacity): volumetric, ray marched through a layer
//      of noise, lit by the sun (or the moon) through their own depth, so they shade themselves, and faded into the sky
//      with distance.
// The panorama is a compute pass. The clouds are expensive, so it is refreshed a sixteenth at a time (cClouds.w, one texel
// of each 4x4 block in an ordered pattern), or whole when cClouds.w < 0. The CLOUD_SHADOW build is a pixel shader.
#include "sky_common.hlsli"

#ifndef CLOUD_SHADOW
#define CLOUD_SHADOW 0
#endif

sampler3D tCloudShape : register(s0, space0);
sampler3D tCloudDetail : register(s1, space0);


// The cloud layer (km above the sea, 1.6 to 3.6: a layer of cumulus and stratocumulus) and its noise: the shape
// repeats every 12 km, its detail every 1.6 km, the cover pattern every 30 km.
static const float kCloudBase = 1.6;
static const float kCloudTop = 3.6;
static const float kShapeScale = 1.0 / 12.0;
static const float kDetailScale = 1.0 / 1.6;
static const float kCoverScale = 1.0 / 30.0;
static const float kShapeTexel = 12.0 / 64.0;
static const float kDetailTexel = 1.6 / 32.0;

// The wind's heading (SkyAtmosphere.cpp moves cWind.xy along 0.95, 0.31). The shear: how far (km) the top of the layer
// is carried downwind of its base, so the clouds lean. The rise: how fast (km of noise per km of wind) the shape's noise
// slides down through the layer, so its puffs climb as they drift, as heated air does: they round out from the flat base,
// swell and dissolve at the top in a few minutes (about 6 m/s at the default wind). The detail rises as fast again on top
// of it, so the edges churn and billow rather than ride along fixed. 360 / 684 is a whole number of shape (12 km) and
// detail (1.6 km) periods per wrap of the wind (684 km in x). The erosion: how deep the fine detail cuts into the edges.
// The weather evolves too: its broad regions and its patches drift through their own noise's third axis (2 and 8 whole
// periods per wrap), so over tens of minutes clear stretches fill in and fields of cloud break up, instead of the same
// map sliding by.
// shortcut: with skyCloudScatter off its default the wind's 684 km wrap is no longer a whole number of shape periods, so the clouds jump once every ~17 hours of play; make the wrap a multiple of the scaled period if that shows.
static const float2 kWindDir = float2(0.9507, 0.3102);
static const float kShear = 0.5;
static const float kRise = 360.0 / 684.0;
static const float kDetailRise = 360.0 / 684.0;
static const float kBroadEvolve = 2.0 / 684.0;
static const float kPatchEvolve = 8.0 / 684.0;
static const float kErosion = 0.3;

// Not in the original binary. The fringe: where the cover thins, the cut is still normalised by at least this share of the
// layer's fullest cover, so the last noise peaks there thin out with it instead of standing as small dense puffs between
// the clouds. The wisp cut: density under this share is dropped, so what thins out goes away rather than lingering as
// specks. shortcut: both hand-picked; raise them for fewer small clouds, lower them if the big ones lose their edges.
static const float kFringeSpan = 0.7;
static const float kWispCut = 0.12;

// Extinction per km at density 1; how far clouds are marched; the distance over which they fade into the sky.
static const float kCloudExtinction = 45.0;
static const float kMaxCloudDistance = 120.0;
static const float kHazeDistance = 40.0;

// Distances (km) of the steps toward the light: short first, for the crisp edge, then longer.
static const float kLightSteps[6] = { 0.06, 0.12, 0.24, 0.4, 0.64, 1.0 };

// The 4x4 ordered pattern's index (y * 4 + x within the block) of each slice's texel, the inverse of { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 }.
static const uint kBayerSlot[16] = { 0, 10, 2, 8, 5, 15, 7, 13, 1, 11, 3, 9, 4, 14, 6, 12 };

float Remap(float v, float a, float b, float c, float d)
{
    return c + (v - a) / (b - a) * (d - c);
}

// The weather over a point: x how much of the layer is cloud there, 0..1; y how high its clouds may climb, 0..1 of the
// layer. Regions of tens of km: some stretches of sky stay clear, others gather the clouds into fields and banks, with
// patches in between, so the sky is never evenly spread; the zone's cover sets how much there is overall. Where the
// cover is heavier (moister air, stronger lift) the cells climb higher, varied region to region. Drifts with the wind.
float2 Weather(float2 xy)
{
    float2 w = (xy + cWind.xy) * kCoverScale;
    float broad = tex3Dlod(tCloudShape, float4(float3(w, 0.37 + cWind.x * kBroadEvolve), 2)).g;
    float patch = tex3Dlod(tCloudShape, float4(float3(w * 2.7, 0.71 + cWind.x * kPatchEvolve), 1)).b;
    float region = smoothstep(0.3, 0.75, broad * 0.75 + patch * 0.25);
    // Toward overcast the clear stretches close up too, so a rainy sky is one deck rather than banks with gaps.
    region = lerp(region, 1, smoothstep(0.6, 1.0, cClouds.x));
    float cover = saturate(cClouds.x * region * 1.4);
    float top = saturate(0.5 + 0.7 * cover + (patch - 0.5) * 0.3);
    return float2(cover, top);
}

// Cloud density at p (world x, y in km; z the altitude in km), h the height within the layer 0..1. The Nubis model
// (Schneider, "The real-time volumetric cloudscapes of Horizon Zero Dawn", SIGGRAPH 2015; GPU Pro 7; Nubis 2017):
//   1. the base shape: the Perlin-Worley dilated by the Worley fBm (a remap, not a product, which would hollow the cores);
//   2. a cumulus height gradient: a crisp, flat base (the condensation level), full low down, falling off toward the
//      column's top;
//   3. the cover: only the share of shape * gradient above 1 - cover is cloud, remapped so the inside stays dense and
//      the edges soft; the falling gradient turns that cut into domes, wide at the base and rounded on top;
//   4. the detail: finer Worley erodes the edges only (a remap again), wispy at the base and billowy above, its lookup
//      swirled near the base by the shape's own noise;
//   5. the water: a cumulus holds more water the higher above its base (adiabatic), so the bases are thinner and
//      greyer than the sunlit tops.
// Every noise channel is spread evenly over 0..1 (SkyAtmosphere.cpp), so the cut at 1 - cover tracks the cover.
float Density(float3 p, float h, float lodShape, float lodDetail, bool detail)
{
    if (h <= 0 || h >= 1) { return 0; }

    float2 weather = Weather(p.xy);
    float cover = weather.x;
    // Fade the cover out with distance from the player so the clouds gather overhead, not in a wall at the horizon.
    cover *= 1 - smoothstep(cCloudTint.w * 0.4, cCloudTint.w, length(p.xy - cCamera.xy));
    float top = weather.y;

    if (cover < 0.01 || h >= top) { return 0; }

    float3 q = float3(p.xy + cWind.xy + kWindDir * (h * kShear), p.z - cWind.x * kRise);
    float4 s = tex3Dlod(tCloudShape, float4(q * (kShapeScale * cCloudShape.x), lodShape));
    float fbm = s.g * 0.625 + s.b * 0.25 + s.a * 0.125;
    float shape = saturate(Remap(s.r, fbm - 1, 1, 0, 1));
    float gradient = smoothstep(0, 0.07, h) * (1 - smoothstep(top * 0.4, top, h));
    // The layer's fullest cover (Weather's at the heart of a region) sets how thin the fringe's peaks are cut.
    float span = max(cover, saturate(cClouds.x * 1.4) * kFringeSpan);
    float cloud = saturate((shape * gradient - (1 - cover)) / span);

    if (cloud <= 0) { return 0; }

    if (detail)
    {
        float2 swirl = (s.ba - 0.5) * 0.3 * (1 - h);
        float3 dq = float3(q.xy + cWind.xy * 0.6 + swirl, q.z - cWind.x * kDetailRise);
        float4 d = tex3Dlod(tCloudDetail, float4(dq * (kDetailScale * cCloudShape.x), lodDetail));
        float fine = d.r * 0.625 + d.g * 0.25 + d.b * 0.125;
        fine = lerp(fine, 1 - fine, saturate(h * 6));
        cloud = saturate(Remap(cloud, fine * kErosion, 1, 0, 1));
    }

    cloud = saturate(Remap(cloud, kWispCut, 1, 0, 1));

    return cloud * lerp(0.5, 1, saturate(h / top));
}

// Optical depth from p toward the light through the clouds (shape only): what shades a cloud with its own bulk.
float LightDepth(float3 p, float3 l)
{
    float depth = 0;
    float travelled = 0;

    [unroll] for (int i = 0; i < 6; i++)
    {
        float step = kLightSteps[i];
        float3 q = p + l * (travelled + step * 0.5);
        travelled += step;
        depth += Density(q, (q.z - kCloudBase) / (kCloudTop - kCloudBase), 1, 0, false) * step;
    }

    return depth * kCloudExtinction;
}

// The clouds along dir: scattered light (premultiplied, before exposure) and opacity, and the mean distance of what
// was seen (for the haze).
float4 MarchClouds(float3 dir, float3 ambient, float2 pixel, out float distance)
{
    distance = kMaxCloudDistance;

    float3 o = Origin();
    float2 outer = RaySphere(o, dir, kGround + kCloudTop);
    float2 inner = RaySphere(o, dir, kGround + kCloudBase);

    if (outer.y <= 0) { return 0; }

    // The stretch of the ray inside the shell between the two spheres: under the layer it starts where the ray leaves
    // the inner sphere, above it the ray stops where it enters the inner one.
    float start = max(outer.x, 0);
    float end = outer.y;

    if (inner.y > 0)
    {
        if (inner.x < 0) { start = max(start, inner.y); }
        else { end = min(end, inner.x); }
    }

    float2 ground = RaySphere(o, dir, kGround);

    if (ground.x > 0 && ground.x < start) { return 0; }

    // Past the range the cover is zero; stop there so the steps stay fine.
    end = min(end, min(kMaxCloudDistance, cCloudTint.w * 1.5));

    if (end <= start) { return 0; }

    int steps = (int)clamp((end - start) / 0.08, 32, 96);
    float ds = (end - start) / steps;
    // The start of the march is offset by interleaved gradient noise: well spread with no 4x4 period (an ordered offset
    // showed as a checker once the clouds were magnified), and the dome's blur (sky_dome_ps.hlsl CloudSample) averages it
    // into smooth cloud.
    float t = start + ds * frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
    float lodShape = clamp(log2(ds * cCloudShape.x / kShapeTexel), 0, 5);
    float lodDetail = clamp(log2(ds * cCloudShape.x / kDetailTexel), 0, 4);

    float3 l = cCloudLight.xyz;
    float mu = dot(dir, l);
    // The phase of each scattering octave (below) depends on the angle to the light alone, the same all along the ray.
    float phase[3];
    float g = 1;

    [unroll] for (int octave = 0; octave < 3; octave++)
    {
        // A narrow silver lining: at g 0.8 weighted 0.7 the forward lobe whitened every thin cloud within ~30 degrees of the sun.
        phase[octave] = lerp(HenyeyGreenstein(mu, 0.9 * g), HenyeyGreenstein(mu, -0.25 * g), 0.75);
        g *= 0.5;
    }

    // The light reaching the layer, reddened by the air it crossed: once, at the middle of the stretch.
    float3 sunlight = cCloudLight.w * cCloudTint.rgb * TransmittanceToLight(o + dir * (0.5 * (start + end)), l);

    float transmit = 1;
    float3 light = 0;
    float seen = 0;
    float seenWeight = 0;

    [loop] for (int i = 0; i < steps; i++)
    {
        float3 pc = o + dir * t;
        float altitude = length(pc) - kGround;
        float h = (altitude - kCloudBase) / (kCloudTop - kCloudBase);
        float3 p = float3(cCamera.xy + dir.xy * t, altitude);
        float density = Density(p, h, lodShape, lodDetail, true);

        [branch] if (density > 0.001)
        {
            float sigma = density * kCloudExtinction;
            float tau = LightDepth(p, l);

            // Multiple scattering, three octaves (Wrenninge): each fainter, less extinguished and less forward.
            float scatter = 0;
            float a = 1;
            float b = 1;

            [unroll] for (int k = 0; k < 3; k++)
            {
                scatter += a * exp(-tau * b) * phase[k];
                a *= 0.5;
                b *= 0.4;
            }

            // The sky lights the tops more than the undersides; by night the undersides get almost none (cView.w), so
            // the bases go dark while the moon still lights the tops.
            float3 source = sunlight * scatter + ambient * lerp(lerp(0.35, 0.05, cView.w), 1.0, h);
            float absorbed = 1 - exp(-sigma * ds);
            light += transmit * source * absorbed;
            seen += t * transmit * absorbed;
            seenWeight += transmit * absorbed;
            transmit *= 1 - absorbed;

            if (transmit < 0.01) { break; }
        }

        t += ds;
    }

    distance = seenWeight > 0 ? seen / seenWeight : end;
    return float4(light, 1 - transmit);
}

// The panorama's two targets, written by this compute pass (SkyAtmosphere.cpp DrawPanorama).

RWTexture2D<float4> uClouds : register(u17, space0);

// One thread per texel refreshed. With a slice (cClouds.w 0..15), one texel of every 4x4 block, packed so every lane of a
// wave marches: a pixel pass that discarded the other fifteen kept most lanes of each wave idle for the whole march.
// With cClouds.w < 0, every texel.
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint2 px = id.xy;

    if (cClouds.w >= 0)
    {
        uint slot = kBayerSlot[(uint)cClouds.w];
        px = id.xy * 4 + uint2(slot & 3, slot >> 2);
    }

    // The texel's centre, as SV_Position gave the pixel pass.
    float2 pos = px + 0.5;

    float3 dir = PanoDirection(pos / cWind.zw);
    // Below the horizon the sky is the horizon's haze, darkening quickly downward toward the land and sea it lies over:
    // a soft line between the bright sky and a darker haze, not a mirrored band of glare twice as wide.
    float3 level = float3(dir.xy, max(dir.z, 0));
    float3 view = normalize(float3(dir.xy, max(dir.z, 0.002)));
    float3 radiance = Atmosphere(view, cSun.xyz, cSun.w);
    // The low sky is darkened toward the horizon (to 0.8 at it, nothing by about 11 degrees up), and below it the haze
    // carries on darkening toward the land and sea (to 0.5): one continuous curve through the horizon, so no line.
    float below = dir.z >= 0 ? lerp(0.8, 1, smoothstep(0, 0.2, dir.z)) : lerp(0.8, 0.5, smoothstep(0, 0.12, -dir.z));

    float3 sky = FinishSky(level, radiance) * below;
    float4 clouds = 0;

    [branch] if (cClouds.y > 0.5 && cClouds.x > 0.005)
    {
        // The light from the sky around: its radiance this way, and by night a third of the zone's night sky, in the moonlight's
        // colour, brought to radiance, so the clouds stand darker than the sky behind them (shapes against the stars) and the moon rims them.
        float3 ambient = radiance + ToLinear(cBands[0].rgb) * cCloudTint.rgb * cView.w * 0.35 / max(cView.y, 1e-3);
        float distance;
        float4 cloud = MarchClouds(dir, ambient, pos, distance);

        if (cloud.a > 0.002)
        {
            float3 colour = Tonemap(cloud.rgb / cloud.a);
            float3 stockCloud = cCloudZone[0].rgb * 0.5 + cCloudZone[1].rgb + cCloudZone[2].rgb * saturate(Luma(colour)); // the stock clouds' mix of the zone's cloud colours
            colour = lerp(colour, ZoneTint(colour, stockCloud), cView.z * saturate(Luma(stockCloud) * 20)); // its hue at the physical cloud's brightness: the stock mix's own brightness varies too much from zone to zone; a near-black mix leaves the physical cloud
            // By night the clouds' shaded side goes no darker than the land in shade (the world's ambient on ground of
            // kCloudFloorAlbedo), lifted smoothly so their moonlit side keeps its shading: lit by the dim night sky
            // alone they went near black against the dark blue land and sea. shortcut: the albedo is hand-picked; raise
            // it if the clouds still read darker than the land at night, lower it if they lose their shape.
            static const float kCloudFloorAlbedo = 0.1;
            float3 floorColour = cAmbient.rgb * kCloudFloorAlbedo * cView.w;
            colour = floorColour + colour * (1 - floorColour);
            // Distant clouds dissolve into the haze: the aerial haze, plus a fade that reaches the full sky by the cloud
            // range (cCloudTint.w), so they thin out progressively instead of standing as crisp rows at the horizon.
            float haze = max(1 - exp(-distance / kHazeDistance), smoothstep(cCloudTint.w * 0.2, cCloudTint.w, distance));
            colour = lerp(colour, sky, haze);
            float alpha = cloud.a * (1 - haze);
            clouds = float4(colour * alpha, alpha);
        }
    }


    uClouds[px] = clouds;
}


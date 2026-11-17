// Not in the original binary. Shared by the sky's shaders (SkyAtmosphere.cpp): its constants, the panorama's mapping,
// and the atmosphere. Lengths are in km, the planet's centre at the origin, z up.
#ifndef SKY_COMMON_HLSLI
#define SKY_COMMON_HLSLI

// Bound natively at b9 by SkyAtmosphere; gx never binds b9, so it stays put through the dome's gx draws.
cbuffer SkyConstants : register(b16, space0)
{
    float4 cSun;        // toward the sun (world, unit), its illuminance
    float4 cCloudLight; // toward the light the clouds are lit by (the sun by day, the moon by night), its illuminance
    float4 cView;       // 0, exposure, zone tint 0..1, night 0..1
    float4 cCamera;     // world x, y (km), altitude above the sea (km), frame number (jitter)
    float4 cClouds;     // cover 0..1, clouds on (1) or off (0), time (s, wrapped), panorama slice (0..15; -1 all)
    float4 cWind;       // wind offset (km) xy, panorama width, height (texels)
    float4 cBands[6];   // the zone's sky bands, zenith (0) to horizon (5), gamma rgb
    float4 cFlash;      // lightning flash colour rgb, amount 0..1; applied where the panorama is read (Flash), never baked into it
    float4 cStars;      // star field rotation cos, sin, brightness, time (s, wrapped)
    float4 cCloudTint;  // the colour of the light on the clouds, rgb: white by day, the moonlight's (saturated) by night, w: cloud range (km), cover fades out toward it
    float4 cMoon;       // toward the moon (world, unit), the moon disc's angular radius (radians)
    // The dome draw's transforms, as the fixed-function path takes them (CGxDeviceD3d11::IFfpUpdateConstants): the gx
    // world and view stack tops and the native projection, row vectors.
    row_major float4x4 cDomeWorld;
    row_major float4x4 cDomeView;
    row_major float4x4 cDomeProj;
    float4 cAmbient;    // the world's ambient light (the land in shade), gamma rgb
    float4 cCloudShape; // x: the cloud noise's frequency (skyCloudScatter): 1 the default size, 2 clouds half as wide
    float4 cCloudZone[3]; // the zone's own cloud colours (Light.dbc), gamma rgb, mixed as the stock clouds do: 0 the body (by shade), 1 the base (flat), 2 the sunlight on them (by how lit)
};

static const float kPi = 3.14159265;

// ---- Panorama: the whole sky around the camera, azimuth across (u) and elevation down (v). The rows are packed toward
// the horizon, where the sky changes fastest and the clouds are squeezed flat: the zenith is the top row, the horizon
// row kPanoHorizon, and kPanoBelow radians of sky under it fill the rest.
static const float kPanoHorizon = 0.85;
static const float kPanoBelow = 0.5 * kPi;

float3 PanoDirection(float2 uv)
{
    float phi = uv.x * 2 * kPi;
    float e = uv.y < kPanoHorizon
        ? 0.5 * kPi * pow((kPanoHorizon - uv.y) / kPanoHorizon, 2)
        : -kPanoBelow * pow((uv.y - kPanoHorizon) / (1 - kPanoHorizon), 2);
    return float3(cos(e) * cos(phi), cos(e) * sin(phi), sin(e));
}

float2 PanoUv(float3 dir)
{
    float u = frac(atan2(dir.y, dir.x) / (2 * kPi) + 1);
    float e = asin(clamp(dir.z, -1, 1));
    float v = e >= 0
        ? kPanoHorizon - kPanoHorizon * sqrt(e / (0.5 * kPi))
        : kPanoHorizon + (1 - kPanoHorizon) * sqrt(min(-e / kPanoBelow, 1));
    return float2(u, v);
}

// Not in the original binary. The clear sky's floor, the sine of the elevation (about 2.9 degrees) below which the dome
// shows the clear sky of the floor itself in the same azimuth: the physical sky darkens steeply over its last degree and a
// half, a dark line along the horizon that no fog colour can meet. The depth fog (Engine/Source/DepthFog/shaders/fog_ps.hlsl)
// takes its colour at the same floor, so the sky runs flat into the fogged land and sea. Below the horizon too: the panorama's
// own rows there are a darker, other-hued haze that showed through thin or fading land as shades the fog could not meet.
static const float kSkyFloor = 0.05;

float3 SkyFloor(float3 dir)
{
    return dir.z >= kSkyFloor ? dir :float3(normalize(dir.xy + 1e-6) * sqrt(1 - kSkyFloor * kSkyFloor), kSkyFloor);
}

// Bilinear by Load, wrapping around the horizon and clamped at the zenith and the bottom: no sampler is shared with gx.
// ---- Atmosphere (Hillaire 2020's Earth with a real clear day's aerosol: his is almost free of it, a vertical optical depth
// of 0.005 against the 0.1 to 0.2 measured on average; at 0.03 the sun's half of the sky outshines the far half, which
// Rayleigh's symmetric phase alone left as bright): Rayleigh, Mie (Henyey-Greenstein) and ozone; single scattering marched
// along the view ray with the light's transmittance marched toward it at each sample (so the Earth's shadow and the
// sunset's reddening fall out), plus Hillaire's multiple-scattering LUT for the light scattered more than once.
// Now about 0.011 and g 0.85: at 0.03 and g 0.8 the forward glow saturated a third of the sky around the sun and hid the disc.
static const float kGround = 6360.0;
static const float kTop = 6460.0;
static const float3 kRayleigh = float3(5.802e-3, 13.558e-3, 33.1e-3);
static const float kRayleighHeight = 8.0;
static const float kMieScatter = 8.0e-3;
static const float kMieExtinct = 8.9e-3;
static const float kMieHeight = 1.2;
static const float kMieG = 0.85;
static const float3 kOzone = float3(0.650e-3, 1.881e-3, 0.085e-3);

float3 Origin()
{
    return float3(0, 0, kGround + cCamera.z);
}

// The two distances along the ray to a sphere about the planet's centre (near, far); both negative on a miss.
float2 RaySphere(float3 o, float3 d, float r)
{
    float b = dot(o, d);
    float c = dot(o, o) - r * r;
    float disc = b * b - c;

    if (disc < 0) { return float2(-1, -1); }

    float s = sqrt(disc);
    return float2(-b - s, -b + s);
}

bool HitsGround(float3 o, float3 d)
{
    float2 hit = RaySphere(o, d, kGround);
    return hit.y > 0 && hit.x > 0;
}

// Rayleigh, Mie and ozone densities at an altitude (km).
float3 Densities(float h)
{
    return float3(exp(-h / kRayleighHeight), exp(-h / kMieHeight), max(0, 1 - abs(h - 25) / 15));
}

float3 Extinction(float3 depth)
{
    return kRayleigh * depth.x + kMieExtinct * depth.y + kOzone * depth.z;
}

// From p to space toward the light l; none in the Earth's shadow.
float3 TransmittanceToLight(float3 p, float3 l)
{
    if (HitsGround(p, l)) { return 0; }

    float ds = RaySphere(p, l, kTop).y / 6;
    float3 depth = 0;

    [unroll] for (int i = 0; i < 6; i++)
    {
        depth += Densities(length(p + l * (ds * (i + 0.5))) - kGround) * ds;
    }

    return exp(-Extinction(depth));
}

// Hillaire's multiple-scattering LUT (sky_multiscatter_ps.hlsl, baked once): u the sun's zenith cosine, v the altitude.
// Bilinear by Load, like PanoSample.
sampler2D tMultiScatter : register(s2, space0);
static const float kMultiScatterSize = 32;

float3 MultiScatter(float h, float cosSun)
{
    float2 p = saturate(float2(cosSun * 0.5 + 0.5, h / (kTop - kGround))) * (kMultiScatterSize - 1);
    int2 i = min((int2)p, (int)kMultiScatterSize - 2);
    float2 f = p - i;
    float3 top = lerp(tex2Dlod(tMultiScatter, float4((float2(i) + 0.5) / kMultiScatterSize, 0, 0)).rgb, tex2Dlod(tMultiScatter, float4((float2(i.x + 1, i.y) + 0.5) / kMultiScatterSize, 0, 0)).rgb, f.x);
    float3 bottom = lerp(tex2Dlod(tMultiScatter, float4((float2(i.x, i.y + 1) + 0.5) / kMultiScatterSize, 0, 0)).rgb, tex2Dlod(tMultiScatter, float4((float2(i + 1) + 0.5) / kMultiScatterSize, 0, 0)).rgb, f.x);
    return lerp(top, bottom, f.y);
}

float RayleighPhase(float mu)
{
    return 3.0 / (16.0 * kPi) * (1 + mu * mu);
}

float HenyeyGreenstein(float mu, float g)
{
    float g2 = g * g;
    return (1 - g2) / (4 * kPi * pow(max(1 + g2 - 2 * g * mu, 1e-4), 1.5));
}

// Henyey-Greenstein at g 0.85: at 0.7 the aerosol's forward lobe lit the sun's whole half of the sky in a wide orange wash
// at sunset (about 0.6x the 0.7 lobe at 60 to 90 degrees from the sun). shortcut: g 0.85 is a hand-picked value, not
// measured; lower it toward 0.7 if the far side of the sky goes too dark at dusk.
float MiePhase(float mu)
{
    return HenyeyGreenstein(mu, kMieG);
}

// Radiance of the clear sky along dir (above the horizon) lit by illuminance e from l, before exposure.
float3 Atmosphere(float3 dir, float3 l, float e)
{
    float3 o = Origin();
    float len = RaySphere(o, dir, kTop).y;
    float3 depth = 0;
    float3 rayleigh = 0;
    float3 mie = 0;
    float3 multiple = 0;

    // Samples spaced quadratically: most of them in the dense low air the horizon rays cross first.
    [loop] for (int i = 0; i < 16; i++)
    {
        float x = (i + 0.5) / 16;
        float ds = len * 2 * x / 16;
        float3 p = o + dir * (len * x * x);
        float r = length(p);
        float3 d = Densities(r - kGround) * ds;
        float3 view = exp(-Extinction(depth + d * 0.5));
        float3 lit = view * TransmittanceToLight(p, l);
        rayleigh += lit * d.x;
        mie += lit * d.y;
        // Light scattered more than once reaches every sample, those in the Earth's shadow too: it keeps the twilight sky
        // blue on the side away from the sun after the direct light has gone.
        multiple += view * MultiScatter(r - kGround, dot(p, l) / r) * (kRayleigh * d.x + kMieScatter * d.y);
        depth += d;
    }

    float mu = dot(dir, l);
    return e * (kRayleigh * rayleigh * RayleighPhase(mu) + kMieScatter * mie * MiePhase(mu) + multiple);
}

// ---- Colour: exposure and a filmic shoulder into the 8-bit frame, which is gamma encoded like the rest of the art.
float Luma(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float3 Tonemap(float3 radiance)
{
    return pow(1 - exp(-max(radiance, 0) * cView.y), 1 / 2.2);
}

float3 ToLinear(float3 gamma)
{
    return pow(saturate(gamma), 2.2);
}

// s_skyThetaTable (DayNight.cpp): the polar angle of each ring of the stock dome, a fraction of pi; the dome is offset
// down by cos(pi/4). The zone's bands laid out as that dome lays them, per pixel.
static const float kRingTheta[6] = { 0.0, 0.17, 0.2, 0.23, 0.24, 0.25 };

float2 Ring(int i)
{
    float theta = kRingTheta[i] * kPi;
    return float2(sin(theta), cos(theta) - 0.70710678);
}

float3 Bands(float3 dir)
{
    float3 colour = cBands[5].rgb;

    if (dir.z <= 0) { return colour; }

    float2 d = float2(length(dir.xy), dir.z);

    [unroll] for (int i = 0; i < 5; i++)
    {
        float2 a = Ring(i);
        float2 e = Ring(i + 1) - a;
        float s = (d.x * a.y - d.y * a.x) / (e.x * d.y - e.y * d.x);

        if (s >= 0 && s <= 1) { colour = lerp(cBands[i].rgb, cBands[i + 1].rgb, s); }
    }

    return colour;
}

// A colour given the zone's hue at its own brightness, so lit and shaded parts keep their shape in the zone's palette.
float3 ZoneTint(float3 colour, float3 zone)
{
    return zone * min(Luma(colour) / max(Luma(zone), 0.02), 4);
}

// The finished sky colour (gamma) from its radiance: the physical sky, its colour pulled toward the zone's bands by the
// zone tint (keeping the physical brightness), and the zone's own night sky once the sun is down.
float3 FinishSky(float3 dir, float3 radiance)
{
    float3 physical = Tonemap(radiance);
    float3 zone = Bands(dir);
    float3 tinted = ZoneTint(physical, zone);
    return lerp(physical, tinted, cView.z) + zone * cView.w;
}

// The lightning flash over a colour read from the panorama (the sky or a cloud's own colour).
float3 Flash(float3 colour)
{
    return lerp(colour, cFlash.rgb, cFlash.w);
}

float Hash12(float2 p)
{
    float3 q = frac(float3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return frac((q.x + q.y) * q.z);
}

float Hash13(float3 p)
{
    p = frac(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return frac((p.x + p.y) * p.z);
}

#endif

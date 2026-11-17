#include "sky_common.hlsli"

// Hillaire 2020 ("A Scalable and Production Ready Sky and Atmosphere Rendering Technique", section 5.5): the light
// scattered two or more times, Psi_ms, for each sun zenith cosine (u, -1..1) and altitude (v, ground to top), with the
// sun's illuminance 1. The second order is gathered isotropically from 64 directions round the point, the ground's
// bounce included, and the higher orders follow as the geometric series 1 / (1 - f_ms). It depends on the air alone, so
// it is baked once.
static const float kGroundAlbedo = 0.3;

float4 main(float4 pos : SV_Position) : SV_Target
{
    float2 unit = (pos.xy - 0.5) / (kMultiScatterSize - 1);
    float cosSun = unit.x * 2 - 1;
    float3 l = float3(0, sqrt(saturate(1 - cosSun * cosSun)), cosSun);
    float3 o = float3(0, 0, kGround + max(unit.y * (kTop - kGround), 0.01));
    float3 second = 0;
    float3 transfer = 0;

    [loop] for (int n = 0; n < 64; n++)
    {
        float theta = 2 * kPi * ((n / 8) + 0.5) / 8;
        float cosPhi = 1 - 2 * ((n % 8) + 0.5) / 8;
        float sinPhi = sqrt(1 - cosPhi * cosPhi);
        float3 dir = float3(cos(theta) * sinPhi, sin(theta) * sinPhi, cosPhi);
        float2 ground = RaySphere(o, dir, kGround);
        bool hitsGround = ground.x > 0;
        float len = hitsGround ? ground.x : RaySphere(o, dir, kTop).y;
        float3 throughput = 1;
        float t = 0;

        // Hillaire's 20 steps, each sampled 0.3 of the way in.
        [loop] for (int i = 0; i < 20; i++)
        {
            float next = len * (i + 0.3) / 20;
            float dt = next - t;
            t = next;
            float3 p = o + dir * t;
            float3 density = Densities(length(p) - kGround);
            float3 scatter = kRayleigh * density.x + kMieScatter * density.y;
            float3 extinct = Extinction(density);
            float3 step = exp(-extinct * dt);
            // The step's transmittance integrated over its length, so thick steps keep their energy.
            float3 share = throughput * (1 - step) / extinct;
            second += share * scatter * TransmittanceToLight(p, l) / (4 * kPi);
            transfer += share * scatter;
            throughput *= step;
        }

        if (hitsGround)
        {
            float3 p = o + dir * len;
            float3 up = normalize(p);
            second += throughput * TransmittanceToLight(p + up * 0.01, l) * saturate(dot(up, l)) * kGroundAlbedo / kPi;
        }
    }

    // Each direction's share of the sphere (4 pi / 64) times the isotropic phase (1 / 4 pi).
    second /= 64;
    transfer /= 64;
    return float4(second / (1 - transfer), 1);
}

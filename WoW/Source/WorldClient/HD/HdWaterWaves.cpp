// SPDX-License-Identifier: GPL-3.0-or-later
// Azeroth Remastered's HdWaterWaves.cpp on Vulkan: the donor's spectrum and CPU FFT unchanged, the textures GxEffects ones.
#include "WoW/Source/WorldClient/HD/HdWaterWaves.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>
#include <windows.h>
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include "Engine/Source/Gx/GxEffects.h"
#include "Engine/Source/Os/TimeManager.h"

namespace {
    const int32_t kN = 64;                 // FFT size (texels per tile side)
    const int32_t kLevels = 7;             // mips 64 .. 1
    const double kPi = 3.14159265358979;
    const double kG = 9.81;                // gravity (the tiles' sizes are taken as metres, 1 yard ~ 1 metre)
    const int32_t kMaxTiles = 4;

    struct Complex {
        float re;
        float im;
    };

    // One FFTTile row (forever_data\hd_forever_fft.txt, "F id size seed wind gamma dir1 fetch1 spread1 dir2 fetch2
    // spread2"); a group with fetch <= 0 is unused.
    struct Tile {
        float size;
        float seed;
        float wind;
        float gamma;
        float group[2][3];
    };

    struct Cascade {
        float size;
        std::vector<Complex> h0;           // h0(k), kN x kN, index (m + kN/2) * kN + (n + kN/2)
        std::vector<float> omega;          // dispersion w(k)
        GxEffects::Texture* texture;
    };

    struct WaveType {
        Cascade cascades[kMaxTiles];
        int32_t count;
        uint32_t lastFrameMs;
        float heading;                     // the wind heading the spectrum was built for (radians)
    };

    std::map<int32_t, Tile> s_tiles;                    // FFTTile id -> numbers
    std::map<int32_t, std::vector<int32_t> > s_typeTiles; // Forever liquid type -> its tiles
    std::map<int32_t, WaveType*> s_waves;               // Forever liquid type -> spectra and textures
    bool s_loaded = false;
    WaveType* s_current = nullptr;
    GxEffects::Texture* s_calm = nullptr;   // 1 x 1: slopes 0, height 0, Jacobian 1 (for unused cascades)
    GxEffects::Device* s_device = nullptr;  // the device the textures belong to

    CVar* s_enabled = nullptr;
    CVar* s_scale = nullptr;

    void RegisterCVars() {
        static bool registered = false;
        if (registered) return;
        registered = true;
        s_enabled = CVar::Register("hdWaterWaves", "HD: 1 = FFT waves on the Forever water (hdWater 1), 0 = the 3.3.5 normal maps", 1, "1", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
        s_scale = CVar::Register("hdWaterWaveScale", "HD: FFT wave height multiplier, 1 = the tiles' own (0 to 4)", 1, "1", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    }

    // Built-in tiles when forever_data\hd_forever_fft.txt is missing: hand-made, NOT Forever's numbers.
    void BuiltinTiles() {
        const Tile lake[3] = {
            { 64.0f, 11.0f, 5.0f, 3.3f, { { 0.0f, 10.0f, 3.0f }, { 0.0f, 0.0f, 1.0f } } },
            { 32.0f, 12.0f, 4.0f, 3.3f, { { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } } },
            { 16.0f, 13.0f, 4.0f, 1.8f, { { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } } },
        };
        const Tile ocean[3] = {
            { 512.0f, 21.0f, 7.0f, 1.8f, { { 0.0f, 200.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } } },
            { 128.0f, 22.0f, 6.0f, 1.8f, { { 0.0f, 50.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } } },
            { 32.0f, 23.0f, 6.0f, 3.3f, { { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } } },
        };
        for (int32_t i = 0; i < 3; i++) {
            s_tiles[-1 - i] = lake[i];
            s_tiles[-11 - i] = ocean[i];
            s_typeTiles[-1].push_back(-1 - i);
            s_typeTiles[-2].push_back(-11 - i);
        }
    }

    FILE* OpenData(const char* name) {
        char path[260];
        snprintf(path, sizeof(path), "forever_data\\%s", name);
        FILE* file = fopen(path, "rb");
        if (file) return file;
        snprintf(path, sizeof(path), "..\\..\\forever_data\\%s", name);
        return fopen(path, "rb");
    }

    void Load() {
        if (s_loaded) return;
        s_loaded = true;
        BuiltinTiles();
        FILE* file = OpenData("hd_forever_fft.txt");
        if (!file) return;
        char line[512];
        while (fgets(line, sizeof(line), file)) {
            if (line[0] == 'F' && line[1] == ' ') {
                int id = 0;
                Tile t;
                if (sscanf(line + 2, "%d %f %f %f %f %f %f %f %f %f %f", &id, &t.size, &t.seed, &t.wind, &t.gamma,
                           &t.group[0][0], &t.group[0][1], &t.group[0][2], &t.group[1][0], &t.group[1][1], &t.group[1][2]) == 11) {
                    s_tiles[id] = t;
                }
            } else if (line[0] == 'L' && line[1] == ' ') {
                char* p = line + 2;
                char* end = nullptr;
                long type = strtol(p, &end, 10);
                if (end == p) continue;
                std::vector<int32_t> tiles;
                for (p = end;;) {
                    long id = strtol(p, &end, 10);
                    if (end == p) break;
                    tiles.push_back(static_cast<int32_t>(id));
                    p = end;
                }
                if (!tiles.empty()) s_typeTiles[static_cast<int32_t>(type)] = tiles;
            }
        }
        fclose(file);
    }

    // deterministic normal random numbers per tile (the tile's seed)
    struct Random {
        uint32_t state;
        float Uniform() {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return (static_cast<float>(state >> 8) + 0.5f) * (1.0f / 16777216.0f);
        }
        float Normal() {
            float u = Uniform();
            float v = Uniform();
            return static_cast<float>(std::sqrt(-2.0 * std::log(u)) * std::cos(2.0 * kPi * v));
        }
    };

    // Mitsuyasu spreading cos^2s(theta / 2), normalised over -pi..pi
    double Spreading(double theta, double s) {
        if (s < 0.1) s = 0.1;
        double q = std::pow(2.0, 2.0 * s - 1.0) / kPi * std::exp(2.0 * std::lgamma(s + 1.0) - std::lgamma(2.0 * s + 1.0));
        return q * std::pow(std::fabs(std::cos(theta * 0.5)), 2.0 * s);
    }

    // JONSWAP (fetch in km > 0) or Pierson-Moskowitz (fetch 0: fully developed), energy per unit angular frequency
    double Spectrum(double w, double wind, double fetchKm, double gamma) {
        if (w <= 1e-4) return 0.0;
        double alpha;
        double wp;
        double peak = 1.0;
        if (fetchKm > 0.0) {
            double fetch = fetchKm * 1000.0;
            alpha = 0.076 * std::pow(wind * wind / (fetch * kG), 0.22);
            wp = 22.0 * std::pow(kG * kG / (wind * fetch), 1.0 / 3.0);
            double sigma = w <= wp ? 0.07 : 0.09;
            double r = std::exp(-(w - wp) * (w - wp) / (2.0 * sigma * sigma * wp * wp));
            peak = std::pow(gamma > 1.0 ? gamma : 1.0, r);
        } else {
            alpha = 0.0081;
            wp = 0.855 * kG / wind;
        }
        double x = wp / w;
        return alpha * kG * kG / std::pow(w, 5.0) * std::exp(-1.25 * x * x * x * x) * peak;
    }

    void BuildCascade(Cascade& c, const Tile& t, double kLow, double kHigh, double heading) {
        c.size = t.size;
        c.h0.assign(kN * kN, Complex());
        c.omega.assign(kN * kN, 0.0f);
        Random rnd;
        rnd.state = static_cast<uint32_t>(t.seed) * 2654435761u + 0x9E3779B9u;
        if (!rnd.state) rnd.state = 1;
        double dk = 2.0 * kPi / t.size;
        double wind = t.wind > 0.5 ? t.wind : 0.5;
        for (int32_t m = 0; m < kN; m++) {
            for (int32_t n = 0; n < kN; n++) {
                // the random numbers are drawn for every k (the same field whatever the band)
                float xr = rnd.Normal();
                float xi = rnd.Normal();
                double kx = (n - kN / 2) * dk;
                double ky = (m - kN / 2) * dk;
                double k = std::sqrt(kx * kx + ky * ky);
                int32_t i = m * kN + n;
                c.omega[i] = static_cast<float>(std::sqrt(kG * k));
                if (k < 1e-6 || k < kLow || k >= kHigh) continue;
                double w = std::sqrt(kG * k);
                double dwdk = kG / (2.0 * w);
                double theta = std::atan2(ky, kx);
                double s = 0.0;
                int32_t used = 0;
                for (int32_t g = 0; g < 2; g++) {
                    if (t.group[g][1] <= 0.0f) continue;
                    s += Spectrum(w, wind, t.group[g][1], t.gamma) * Spreading(theta - heading - t.group[g][0], t.group[g][2]);
                    used++;
                }
                if (!used) s = Spectrum(w, wind, 0.0, 1.0) * Spreading(theta - heading, t.group[0][2] > 0.0f ? t.group[0][2] : 1.0f);
                // S(kx, ky) = S(w) dw/dk D(theta) / k. h(k, t) gets h0(k) and conj(h0(-k)), so E|h0|^2 = S dk^2 / 2
                // keeps the height variance = the sum of S dk^2 over the grid (checked numerically);
                // split over the two random numbers
                double p = 0.5 * s * dwdk / k * dk * dk;
                double a = std::sqrt(p * 0.5);
                c.h0[i].re = static_cast<float>(xr * a);
                c.h0[i].im = static_cast<float>(xi * a);
            }
        }
    }

    // in-place radix-2 inverse FFT (no scaling) of kN complex values with stride
    void Fft1D(Complex* data, int32_t stride) {
        for (int32_t i = 1, j = 0; i < kN; i++) {
            int32_t bit = kN >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(data[i * stride], data[j * stride]);
        }
        for (int32_t len = 2; len <= kN; len <<= 1) {
            double a = 2.0 * kPi / len;
            float wr = static_cast<float>(std::cos(a));
            float wi = static_cast<float>(std::sin(a));
            for (int32_t i = 0; i < kN; i += len) {
                float cr = 1.0f;
                float ci = 0.0f;
                for (int32_t j = 0; j < len / 2; j++) {
                    Complex& u = data[(i + j) * stride];
                    Complex& v = data[(i + j + len / 2) * stride];
                    float tr = v.re * cr - v.im * ci;
                    float ti = v.re * ci + v.im * cr;
                    v.re = u.re - tr;
                    v.im = u.im - ti;
                    u.re += tr;
                    u.im += ti;
                    float nr = cr * wr - ci * wi;
                    ci = cr * wi + ci * wr;
                    cr = nr;
                }
            }
        }
    }

    void Fft2D(std::vector<Complex>& f) {
        for (int32_t m = 0; m < kN; m++) Fft1D(&f[m * kN], 1);
        for (int32_t n = 0; n < kN; n++) Fft1D(&f[n], kN);
    }

    uint16_t Half(float v) {
        uint32_t x;
        memcpy(&x, &v, 4);
        uint32_t sign = (x >> 16) & 0x8000u;
        int32_t e = static_cast<int32_t>((x >> 23) & 0xFF) - 127 + 15;
        uint32_t mant = x & 0x7FFFFFu;
        if (e <= 0) return static_cast<uint16_t>(sign);
        if (e >= 31) return static_cast<uint16_t>(sign | 0x7BFFu);
        return static_cast<uint16_t>(sign | (e << 10) | (mant >> 13));
    }

    // The waves of one cascade at time t: r = dh/dx, g = dh/dy, b = height, a = Jacobian, kN x kN floats x 4
    void Evaluate(const Cascade& c, double t, float scale, std::vector<float>& out) {
        std::vector<Complex> a(kN * kN), b(kN * kN), d(kN * kN);
        double dk = 2.0 * kPi / c.size;
        for (int32_t m = 0; m < kN; m++) {
            for (int32_t n = 0; n < kN; n++) {
                int32_t i = m * kN + n;
                // h(k, t) = h0(k) e^(iwt) + conj(h0(-k)) e^(-iwt)
                int32_t mm = (kN - m) % kN;
                int32_t nn = (kN - n) % kN;
                const Complex& p = c.h0[i];
                const Complex& q = c.h0[mm * kN + nn];
                double ph = std::fmod(c.omega[i] * t, 2.0 * kPi);
                float cw = static_cast<float>(std::cos(ph));
                float sw = static_cast<float>(std::sin(ph));
                float hr = (p.re * cw - p.im * sw) + (q.re * cw - q.im * sw);
                float hi = (p.re * sw + p.im * cw) - (q.re * sw + q.im * cw);
                hr *= scale;
                hi *= scale;
                float kx = static_cast<float>((n - kN / 2) * dk);
                float ky = static_cast<float>((m - kN / 2) * dk);
                float k = std::sqrt(kx * kx + ky * ky);
                float ik = k > 1e-6f ? 1.0f / k : 0.0f;
                // the sign (-1)^(n + m) moves k = 0 from the middle of the array to its corner (a centred spectrum)
                float sg = ((n + m) & 1) ? -1.0f : 1.0f;
                // a = h + i * dh/dx, b = dh/dy + i * dDx/dx, d = dDy/dy + i * dDx/dy (packed real pairs)
                float sxr = -kx * hi, sxi = kx * hr;     // i kx h
                float syr = -ky * hi, syi = ky * hr;     // i ky h
                float xxr = kx * kx * ik * hr, xxi = kx * kx * ik * hi;
                float yyr = ky * ky * ik * hr, yyi = ky * ky * ik * hi;
                float xyr = kx * ky * ik * hr, xyi = kx * ky * ik * hi;
                a[i].re = sg * (hr - sxi);  a[i].im = sg * (hi + sxr);
                b[i].re = sg * (syr - xxi); b[i].im = sg * (syi + xxr);
                d[i].re = sg * (yyr - xyi); d[i].im = sg * (yyi + xyr);
            }
        }
        Fft2D(a);
        Fft2D(b);
        Fft2D(d);
        out.resize(kN * kN * 4);
        for (int32_t i = 0; i < kN * kN; i++) {
            // undo the centring shift on the output side as well
            int32_t m = i / kN;
            int32_t n = i % kN;
            float sg = ((n + m) & 1) ? -1.0f : 1.0f;
            float h = sg * a[i].re;
            float sx = sg * a[i].im;
            float sy = sg * b[i].re;
            float dxx = sg * b[i].im;
            float dyy = sg * d[i].re;
            float dxy = sg * d[i].im;
            // horizontal displacement x' = x - D (choppiness 1): Jacobian of that map
            float jac = (1.0f - dxx) * (1.0f - dyy) - dxy * dxy;
            out[i * 4 + 0] = sx;
            out[i * 4 + 1] = sy;
            out[i * 4 + 2] = h;
            out[i * 4 + 3] = jac;
        }
    }

    // The cascade's texture for this update: every mip level (box filter of the wrapping tile) as half floats, made new each
    // time (the old one goes when the GPU is done with it).
    bool Upload(GxEffects::Device* device, Cascade& c, const std::vector<float>& level0) {
        std::vector<std::vector<uint16_t> > levels;
        std::vector<float> cur = level0;
        int32_t size = kN;
        for (int32_t level = 0; level < kLevels; level++) {
            std::vector<uint16_t> texels(static_cast<size_t>(size) * size * 4);
            for (size_t i = 0; i < texels.size(); i++) texels[i] = Half(cur[i]);  // RGBA16F memory order: r, g, b, a
            levels.push_back(texels);
            if (size == 1) break;
            int32_t half = size / 2;
            std::vector<float> next(static_cast<size_t>(half) * half * 4);
            for (int32_t y = 0; y < half; y++) {
                for (int32_t x = 0; x < half; x++) {
                    for (int32_t ch = 0; ch < 4; ch++) {
                        next[(y * half + x) * 4 + ch] = 0.25f * (cur[((2 * y) * size + 2 * x) * 4 + ch] + cur[((2 * y) * size + 2 * x + 1) * 4 + ch] +
                                                                 cur[((2 * y + 1) * size + 2 * x) * 4 + ch] + cur[((2 * y + 1) * size + 2 * x + 1) * 4 + ch]);
                    }
                }
            }
            cur.swap(next);
            size = half;
        }
        const void* pointers[kLevels] = {};
        for (size_t i = 0; i < levels.size(); i++) pointers[i] = levels[i].data();
        GxEffects::Texture* made = device->CreateFilled(kN, kN, static_cast<uint32_t>(levels.size()), GxEffects::Format::RGBA16F, pointers);
        if (!made) return false;
        if (c.texture) device->Destroy(c.texture);
        c.texture = made;
        return true;
    }

    float HeadingRadians() {
        CVar* grass = CVar::Lookup("hdGrassWindDirection");
        float deg = grass ? grass->m_floatValue : 45.0f;
        return static_cast<float>(deg * kPi / 180.0);
    }

    WaveType* Build(int32_t pbrType, float heading) {
        std::map<int32_t, std::vector<int32_t> >::const_iterator it = s_typeTiles.find(pbrType);
        if (it == s_typeTiles.end()) it = s_typeTiles.find((pbrType == 1250 || pbrType == 1296) ? -2 : -1);
        std::vector<Tile> tiles;
        for (size_t i = 0; i < it->second.size(); i++) {
            std::map<int32_t, Tile>::const_iterator t = s_tiles.find(it->second[i]);
            if (t != s_tiles.end() && t->second.size > 1.0f) tiles.push_back(t->second);
        }
        // largest tile first; each tile takes the wavenumbers from 4 x its smaller neighbour's fundamental down to
        // the larger neighbour's cut, so the cascades add up without counting the same waves twice
        std::sort(tiles.begin(), tiles.end(), [](const Tile& a, const Tile& b) { return a.size > b.size; });
        if (tiles.size() > static_cast<size_t>(kMaxTiles)) tiles.resize(kMaxTiles);
        WaveType* w = new WaveType();
        w->count = static_cast<int32_t>(tiles.size());
        w->lastFrameMs = 0;
        w->heading = heading;
        double kLow = 0.0;
        for (int32_t i = 0; i < w->count; i++) {
            double kHigh = i + 1 < w->count ? 4.0 * 2.0 * kPi / tiles[i + 1].size : 1e9;
            w->cascades[i].texture = nullptr;
            BuildCascade(w->cascades[i], tiles[i], kLow, kHigh, heading);
            kLow = kHigh;
        }
        for (int32_t i = w->count; i < kMaxTiles; i++) w->cascades[i].texture = nullptr;
        return w;
    }

    void Free(WaveType* w) {
        for (int32_t i = 0; i < kMaxTiles; i++) {
            if (w->cascades[i].texture && s_device) s_device->Destroy(w->cascades[i].texture);
        }
        delete w;
    }
}

bool HD::WaterWavesUpdate(GxEffects::Device* device, int32_t pbrType) {
    RegisterCVars();
    s_current = nullptr;
    if (!device || !WaterWavesEnabled()) return false;
    if (device != s_device) {
        WaterWavesRelease();  // a new device: what the old one made is gone with it
        s_device = device;
    }
    Load();
    float heading = HeadingRadians();
    WaveType*& w = s_waves[pbrType];
    if (w && std::fabs(w->heading - heading) > 1e-4f) {
        Free(w); // the wind turned: new spectrum
        w = nullptr;
    }
    if (!w) w = Build(pbrType, heading);
    if (w->count == 0) return false;
    if (!s_calm) {
        const uint16_t texel[4] = { 0, 0, 0, Half(1.0f) };
        const void* level = texel;
        s_calm = device->CreateFilled(1, 1, 1, GxEffects::Format::RGBA16F, &level);
        if (!s_calm) return false;
    }
    uint32_t now = OsGetAsyncTimeMs();
    // at most every 8 ms (several water draws in one frame share one update)
    if (now - w->lastFrameMs >= 8u || !w->cascades[0].texture) {
        w->lastFrameMs = now;
        double t = (now % 3600000u) * 0.001; // wraps once an hour
        float scale = WaterWavesScale();
        std::vector<float> data;
        for (int32_t i = 0; i < w->count; i++) {
            Evaluate(w->cascades[i], t, scale, data);
            if (!Upload(device, w->cascades[i], data)) return false;
        }
    }
    s_current = w;
    return true;
}

GxEffects::Texture* HD::WaterWavesTexture(int32_t cascade) {
    if (!s_current || cascade < 0 || cascade >= s_current->count) return s_calm;
    return s_current->cascades[cascade].texture;
}

int32_t HD::WaterWavesTiling(float* out) {
    for (int32_t i = 0; i < kMaxTiles; i++) {
        out[i] = s_current && i < s_current->count ? 1.0f / s_current->cascades[i].size : 0.0f;
    }
    return s_current ? s_current->count : 0;
}

float HD::WaterWavesScale() {
    RegisterCVars();
    float v = s_scale ? s_scale->m_floatValue : 1.0f;
    return v < 0.0f ? 0.0f : (v > 4.0f ? 4.0f : v);
}

bool HD::WaterWavesEnabled() {
    RegisterCVars();
    return s_enabled ? s_enabled->m_intValue != 0 : true;
}

void HD::WaterWavesRelease() {
    for (std::map<int32_t, WaveType*>::iterator it = s_waves.begin(); it != s_waves.end(); ++it) {
        if (it->second) Free(it->second);
    }
    s_waves.clear();
    s_current = nullptr;
    if (s_calm && s_device) s_device->Destroy(s_calm);
    s_calm = nullptr;
}

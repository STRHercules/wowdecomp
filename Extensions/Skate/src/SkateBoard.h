// Phase 5: the board as a real M2. Pure and engine-independent (no engine headers), unit tested.
//  - Build(): an M2 (MD20 v264) + skin + BLPs from the helper's op 7 mesh and the board's rest shape, in memory.
//    The layout is the one Tools/FortniteCosmetics/convert_backpacks.py writes (live-proven), one rigid bone,
//    one submesh and one batch per texture. Nothing from Skate 3 is stored: it is generated from what the user's
//    own helper streams and served through VirtualAssets, so no converted game data enters git or an MPQ.
//  - Place(): the board's world matrix for a frame, such that the M2 lands exactly where the donor drew its
//    vertices (boardplacement::WorldPoint), from a rigid fit of the streamed vertices to the rest shape.
#pragma once
#include "SkateLogic/board-placement.h"
#include "SkateLogic/character-alignment.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace skateboard {
using skatevisual::Point;
constexpr double Yard = boardplacement::MetresPerYard; // metres per yard
// Trucks tilt and wheels spin in the streamed vertices; the rigid M2 may differ from them by this much (metres).
constexpr float MaxResidual = .2f;

// ---- 3x3 maths (row-major, double) ----
struct M3 { double a[9]{}; };
inline M3 Mul(const M3& x, const M3& y) {
    M3 r;
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
        r.a[i*3+j] = x.a[i*3]*y.a[j] + x.a[i*3+1]*y.a[3+j] + x.a[i*3+2]*y.a[6+j];
    return r;
}
inline M3 Transpose(const M3& x) { M3 r; for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) r.a[i*3+j] = x.a[j*3+i]; return r; }
inline double Det(const M3& x) {
    const double* a = x.a;
    return a[0]*(a[4]*a[8]-a[5]*a[7]) - a[1]*(a[3]*a[8]-a[5]*a[6]) + a[2]*(a[3]*a[7]-a[4]*a[6]);
}
inline bool Inverse(const M3& x, M3& out) {
    const double d = Det(x), *a = x.a;
    if (!(fabs(d) > 1e-18) || !std::isfinite(d)) return false;
    const double c[9] {
        a[4]*a[8]-a[5]*a[7], a[2]*a[7]-a[1]*a[8], a[1]*a[5]-a[2]*a[4],
        a[5]*a[6]-a[3]*a[8], a[0]*a[8]-a[2]*a[6], a[2]*a[3]-a[0]*a[5],
        a[3]*a[7]-a[4]*a[6], a[1]*a[6]-a[0]*a[7], a[0]*a[4]-a[1]*a[3]};
    for (int i = 0; i < 9; ++i) out.a[i] = c[i] / d;
    return true;
}
// The rotation closest to k (polar factor), by Newton iteration. False if k is singular or not a rotation-like map.
inline bool Polar(const M3& k, M3& out) {
    M3 q = k;
    for (int n = 0; n < 60; ++n) {
        M3 inv;
        if (!Inverse(q, inv)) return false;
        const M3 it = Transpose(inv);
        double change = 0;
        M3 next;
        for (int i = 0; i < 9; ++i) { next.a[i] = .5 * (q.a[i] + it.a[i]); change = (std::max)(change, fabs(next.a[i] - q.a[i])); }
        q = next;
        if (change < 1e-12) break;
    }
    if (Det(q) <= 0) return false;
    out = q;
    return true;
}

// ---- rest shape and rigid fit ----
struct Rest {
    std::vector<Point> points; // source metres, the board's streamed vertices at the first frame
    Point centre{};
    bool Make(const std::vector<Point>& vertices) {
        points = vertices; centre = {};
        if (points.size() < 4) { points.clear(); return false; }
        double sum[3]{};
        for (const Point& p : points) { if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) { points.clear(); return false; }
            sum[0] += p.x; sum[1] += p.y; sum[2] += p.z; }
        const double n = static_cast<double>(points.size());
        centre = {float(sum[0]/n), float(sum[1]/n), float(sum[2]/n)};
        return true;
    }
};
struct Rigid { M3 r; Point centre{}; float error = 0; }; // p ~ centre + r * (rest - rest.centre)
inline bool FitRigid(const Rest& rest, const std::vector<Point>& now, Rigid& out) {
    if (now.size() != rest.points.size() || now.empty()) return false;
    double c[3]{};
    for (const Point& p : now) { if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false; c[0] += p.x; c[1] += p.y; c[2] += p.z; }
    const double n = static_cast<double>(now.size());
    for (double& v : c) v /= n;
    M3 k;
    for (size_t i = 0; i < now.size(); ++i) {
        const double p[3]{now[i].x - c[0], now[i].y - c[1], now[i].z - c[2]};
        const double r[3]{double(rest.points[i].x) - rest.centre.x, double(rest.points[i].y) - rest.centre.y, double(rest.points[i].z) - rest.centre.z};
        for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b) k.a[a*3+b] += p[a] * r[b];
    }
    if (!Polar(k, out.r)) return false;
    out.centre = {float(c[0]), float(c[1]), float(c[2])};
    double worst = 0;
    for (size_t i = 0; i < now.size(); ++i) {
        const double r[3]{double(rest.points[i].x) - rest.centre.x, double(rest.points[i].y) - rest.centre.y, double(rest.points[i].z) - rest.centre.z};
        for (int a = 0; a < 3; ++a) {
            const double predicted = c[a] + out.r.a[a*3]*r[0] + out.r.a[a*3+1]*r[1] + out.r.a[a*3+2]*r[2];
            const double p = a == 0 ? now[i].x : a == 1 ? now[i].y : now[i].z;
            worst = (std::max)(worst, fabs(p - predicted));
        }
    }
    out.error = static_cast<float>(worst);
    return std::isfinite(worst);
}

// ---- placement ----
struct Placed {
    float matrix[16]; // C44Matrix layout: rows are the images of the local basis, row 3 the translation (world yards)
    float rotation[9]; // the same rotation without the uniform scale (rows as above), for the peer message
    float scale = 1;
    float error = 0;
};
// The donor drew each vertex at boardplacement::WorldPoint(p, frame, player, bottom, fit), an affine map of p.
// With p = centre + R * (rest - rest.centre) and the M2's vertices at local = L(rest - rest.centre), where L takes
// source (x, y up, z) metres to WoW yards (x, -z, y) / 0.9144, the M2 at this matrix lands on the same points.
inline bool Place(const Rest& rest, const skatevisual::Frame& frame, Point player, const boardplacement::Fit& fit, Placed& out) {
    Rigid rigid;
    if (!FitRigid(rest, frame.vertices, rigid) || rigid.error > MaxResidual) return false;
    float bottom = frame.vertices[0].y;
    for (const Point& p : frame.vertices) bottom = (std::min)(bottom, p.y);
    const auto at = [&](Point p) { return boardplacement::WorldPoint(p, frame, player, bottom, fit); };
    const Point w0 = at(rigid.centre);
    // The linear part does not depend on where the player is; measure it near the origin, where floats are precise
    // (world coordinates reach 17,000 yd, which would cost 1e-3 of accuracy in the axes).
    boardplacement::Fit local = fit;
    local.sourceOrigin = {0, 0, 0};
    const auto step = [&](Point p) { return boardplacement::WorldPoint(p, frame, {0, 0, 0}, bottom, local); };
    const Point o = step(rigid.centre);
    const Point wx = step({rigid.centre.x + 1, rigid.centre.y, rigid.centre.z});
    const Point wy = step({rigid.centre.x, rigid.centre.y + 1, rigid.centre.z});
    const Point wz = step({rigid.centre.x, rigid.centre.y, rigid.centre.z + 1});
    M3 lw; // source metres -> world yards (columns)
    const double cols[3][3]{{double(wx.x)-o.x, double(wx.y)-o.y, double(wx.z)-o.z},
        {double(wy.x)-o.x, double(wy.y)-o.y, double(wy.z)-o.z}, {double(wz.x)-o.x, double(wz.y)-o.y, double(wz.z)-o.z}};
    for (int c = 0; c < 3; ++c) for (int r = 0; r < 3; ++r) lw.a[r*3+c] = cols[c][r];
    M3 back; // local yards (a, b, c) -> source metres (a, c, -b) * 0.9144
    back.a[0] = Yard; back.a[5] = Yard; back.a[7] = -Yard;
    const M3 t = Mul(Mul(lw, rigid.r), back);
    for (int row = 0; row < 3; ++row) for (int col = 0; col < 3; ++col) out.matrix[row*4+col] = static_cast<float>(t.a[col*3+row]);
    out.matrix[3] = out.matrix[7] = out.matrix[11] = 0;
    out.matrix[12] = w0.x; out.matrix[13] = w0.y; out.matrix[14] = w0.z; out.matrix[15] = 1;
    double scale = 0;
    for (int row = 0; row < 3; ++row) scale += sqrt(double(out.matrix[row*4])*out.matrix[row*4] + double(out.matrix[row*4+1])*out.matrix[row*4+1] + double(out.matrix[row*4+2])*out.matrix[row*4+2]);
    scale /= 3;
    if (!(scale > .01) || !std::isfinite(scale)) return false;
    out.scale = static_cast<float>(scale);
    for (int row = 0; row < 3; ++row) for (int col = 0; col < 3; ++col) out.rotation[row*3+col] = static_cast<float>(out.matrix[row*4+col] / scale);
    out.error = rigid.error;
    for (float v : out.matrix) if (!std::isfinite(v)) return false;
    return true;
}

// Which donor fit to place the board with: the grounded stance fit while riding, the root-relative one in the air,
// the board's own free trajectory in a bail, else the plain board under the feet.
inline boardplacement::Fit ChooseFit(bool bail, const skatevisual::Frame& frame, const characteralignment::AirAnchor& anchor,
                                     float modelScale, Point origin, Point centre) {
    boardplacement::Fit plain; plain.valid = true; plain.scale = 1;
    boardplacement::Fit fit = plain;
    if (bail) {
        fit.physical = true; fit.sourceOrigin = origin; fit.sourceCentre = centre;
    } else if (!frame.pose.ground || frame.pose.airborne) {
        const auto air = anchor.Solve(frame);
        if (air.valid) fit = air.board;
        else if (anchor.ground.valid) { fit = anchor.ground.board; fit.rootRelative = true; fit.sourceBottomFromRoot = anchor.bottomFromRoot; }
    } else if (anchor.ground.valid) {
        fit = anchor.ground.board; fit.rootRelative = false;
    }
    fit.modelScale = modelScale;
    return boardplacement::Valid(fit) ? fit : [&] { plain.modelScale = modelScale; return boardplacement::Valid(plain) ? plain : boardplacement::Fit{}; }();
}

// Rotation (rows as Placed::rotation) to a unit quaternion x, y, z, w.
inline void Quaternion(const float r[9], float q[4]) {
    // Row-vector rows: column j of the column-vector matrix is row j here, so transpose first.
    const double m00 = r[0], m01 = r[3], m02 = r[6], m10 = r[1], m11 = r[4], m12 = r[7], m20 = r[2], m21 = r[5], m22 = r[8];
    double x, y, z, w;
    const double trace = m00 + m11 + m22;
    if (trace > 0) { const double s = sqrt(trace + 1) * 2; w = s / 4; x = (m21 - m12) / s; y = (m02 - m20) / s; z = (m10 - m01) / s; }
    else if (m00 > m11 && m00 > m22) { const double s = sqrt(1 + m00 - m11 - m22) * 2; w = (m21 - m12) / s; x = s / 4; y = (m01 + m10) / s; z = (m02 + m20) / s; }
    else if (m11 > m22) { const double s = sqrt(1 + m11 - m00 - m22) * 2; w = (m02 - m20) / s; x = (m01 + m10) / s; y = s / 4; z = (m12 + m21) / s; }
    else { const double s = sqrt(1 + m22 - m00 - m11) * 2; w = (m10 - m01) / s; x = (m02 + m20) / s; y = (m12 + m21) / s; z = s / 4; }
    const double n = sqrt(x*x + y*y + z*z + w*w);
    q[0] = float(x/n); q[1] = float(y/n); q[2] = float(z/n); q[3] = float(w/n);
}

// ---- the M2 ----
struct Model {
    std::vector<uint8_t> m2, skin;
    std::vector<std::vector<uint8_t>> blp;
    std::vector<std::string> blpName;
    std::string m2Name, skinName;
    float low[3]{}, high[3]{};
};
namespace detail {
inline void Put(std::vector<uint8_t>& b, size_t at, const void* data, size_t n) { memcpy(b.data() + at, data, n); }
inline void PutU32(std::vector<uint8_t>& b, size_t at, uint32_t v) { Put(b, at, &v, 4); }
inline uint32_t GetU32(const std::vector<uint8_t>& b, size_t at) { uint32_t v; memcpy(&v, b.data() + at, 4); return v; }
// Appends payload at the next 16-byte boundary and points the {count, offset} header at it.
inline void Append(std::vector<uint8_t>& b, size_t header, uint32_t count, const void* data, size_t n) {
    b.resize((b.size() + 15) & ~size_t(15), 0);
    PutU32(b, header, count); PutU32(b, header + 4, static_cast<uint32_t>(b.size()));
    b.insert(b.end(), static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + n);
}
template <typename T> inline void Raw(std::vector<uint8_t>& out, const T& v) { const auto* p = reinterpret_cast<const uint8_t*>(&v); out.insert(out.end(), p, p + sizeof(T)); }
inline void U16(std::vector<uint8_t>& out, uint16_t v) { Raw(out, v); }
inline void F32(std::vector<uint8_t>& out, float v) { Raw(out, v); }
// BLP2, ARGB8888 uncompressed with a full mip chain; alpha forced opaque (Skate keeps gloss data in alpha).
inline std::vector<uint8_t> Blp(unsigned w, unsigned h, const std::vector<unsigned char>& rgba) {
    std::vector<uint8_t> blob(1172, 0);
    memcpy(blob.data(), "BLP2", 4);
    const uint32_t head[2]{1, 0}; memcpy(blob.data() + 4, head, 4);
    blob[8] = 3; blob[9] = 8; blob[10] = 8; blob[11] = 1; // ARGB8888, 8 alpha bits, alpha encoding 8, mips
    PutU32(blob, 12, w); PutU32(blob, 16, h);
    std::vector<uint8_t> level(rgba.begin(), rgba.end());
    unsigned lw = w, lh = h;
    for (unsigned mip = 0; mip < 16; ++mip) {
        const size_t offset = blob.size();
        PutU32(blob, 20 + 4*mip, static_cast<uint32_t>(offset));
        PutU32(blob, 84 + 4*mip, lw * lh * 4);
        blob.resize(offset + size_t(lw) * lh * 4);
        for (size_t i = 0; i < size_t(lw) * lh; ++i) { // BGRA
            blob[offset + i*4] = level[i*4+2]; blob[offset + i*4+1] = level[i*4+1]; blob[offset + i*4+2] = level[i*4]; blob[offset + i*4+3] = 255;
        }
        if (lw == 1 && lh == 1) break;
        const unsigned nw = (std::max)(1u, lw / 2), nh = (std::max)(1u, lh / 2);
        std::vector<uint8_t> next(size_t(nw) * nh * 4);
        for (unsigned y = 0; y < nh; ++y) for (unsigned x = 0; x < nw; ++x) for (unsigned c = 0; c < 4; ++c) {
            unsigned sum = 0, count = 0;
            for (unsigned dy = 0; dy < 2; ++dy) for (unsigned dx = 0; dx < 2; ++dx) {
                const unsigned sx = (std::min)(lw - 1, x*2 + dx), sy = (std::min)(lh - 1, y*2 + dy);
                sum += level[(size_t(sy) * lw + sx) * 4 + c]; ++count;
            }
            next[(size_t(y) * nw + x) * 4 + c] = static_cast<uint8_t>((sum + count/2) / count);
        }
        level.swap(next); lw = nw; lh = nh;
    }
    return blob;
}
}

// mesh: op 7's colours/indices/uv/materials/textures. rest: the board's rest shape. generation makes the names unique,
// because the model cache would keep a model of the same name. False if the input is not a usable textured mesh.
inline bool Build(const skatevisual::Mesh& mesh, const Rest& rest, uint32_t generation, Model& out) {
    using namespace detail;
    const size_t nv = rest.points.size(), ni = mesh.indices.size();
    if (!nv || nv > 4096 || nv != mesh.colors.size() || !ni || ni % 3 || ni > 12000 || mesh.uv.size() != nv
        || !mesh.textures || mesh.textures->empty() || mesh.textures->size() > 16 || mesh.materials.size() != ni / 3) return false;
    const unsigned textures = static_cast<unsigned>(mesh.textures->size());
    for (uint32_t m : mesh.materials) if (m >= textures) return false;
    for (uint32_t i : mesh.indices) if (i >= nv) return false;
    out = {};
    // Local vertices: source metres around the centre, mapped (dx, dy, dz) -> (dx, -dz, dy) / 0.9144.
    std::vector<float> local(nv * 3);
    for (size_t i = 0; i < nv; ++i) {
        const double dx = double(rest.points[i].x) - rest.centre.x, dy = double(rest.points[i].y) - rest.centre.y, dz = double(rest.points[i].z) - rest.centre.z;
        local[i*3] = float(dx / Yard); local[i*3+1] = float(-dz / Yard); local[i*3+2] = float(dy / Yard);
    }
    // Triangles grouped by material; outward winding from the signed volume (counter-clockwise seen from outside).
    std::vector<uint32_t> tris;
    std::vector<uint32_t> start(textures + 1, 0);
    for (unsigned m = 0; m < textures; ++m) {
        start[m] = static_cast<uint32_t>(tris.size());
        for (size_t t = 0; t < ni / 3; ++t) if (mesh.materials[t] == m) tris.insert(tris.end(), {mesh.indices[t*3], mesh.indices[t*3+1], mesh.indices[t*3+2]});
    }
    start[textures] = static_cast<uint32_t>(tris.size());
    double volume = 0;
    for (size_t t = 0; t < tris.size(); t += 3) {
        const float* a = &local[tris[t]*3]; const float* b = &local[tris[t+1]*3]; const float* c = &local[tris[t+2]*3];
        volume += a[0]*(double(b[1])*c[2] - double(b[2])*c[1]) - a[1]*(double(b[0])*c[2] - double(b[2])*c[0]) + a[2]*(double(b[0])*c[1] - double(b[1])*c[0]);
    }
    if (volume < 0) for (size_t t = 0; t < tris.size(); t += 3) std::swap(tris[t+1], tris[t+2]);
    std::vector<double> normal(nv * 3, 0.0);
    for (size_t t = 0; t < tris.size(); t += 3) {
        const float* a = &local[tris[t]*3]; const float* b = &local[tris[t+1]*3]; const float* c = &local[tris[t+2]*3];
        const double u[3]{double(b[0])-a[0], double(b[1])-a[1], double(b[2])-a[2]}, v[3]{double(c[0])-a[0], double(c[1])-a[1], double(c[2])-a[2]};
        const double n[3]{u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]};
        for (int k = 0; k < 3; ++k) for (int j = 0; j < 3; ++j) normal[tris[t+k]*3+j] += n[j];
    }
    float low[3]{1e30f, 1e30f, 1e30f}, high[3]{-1e30f, -1e30f, -1e30f}, radius = 0;
    for (size_t i = 0; i < nv; ++i) {
        for (int k = 0; k < 3; ++k) { low[k] = (std::min)(low[k], local[i*3+k]); high[k] = (std::max)(high[k], local[i*3+k]); }
        radius = (std::max)(radius, sqrtf(local[i*3]*local[i*3] + local[i*3+1]*local[i*3+1] + local[i*3+2]*local[i*3+2]));
    }
    const std::string base = "Skate\\Board\\board" + std::to_string(generation);
    out.m2Name = base + ".m2"; out.skinName = base + "00.skin";
    for (unsigned i = 0; i < textures; ++i) out.blpName.push_back("Skate\\Board\\tex" + std::to_string(generation) + "_" + std::to_string(i) + ".blp");
    for (int k = 0; k < 3; ++k) { out.low[k] = low[k]; out.high[k] = high[k]; }

    std::vector<uint8_t>& m = out.m2; m.assign(304, 0);
    std::vector<uint8_t>& s = out.skin; s.assign(48, 0);
    memcpy(m.data(), "MD20", 4); PutU32(m, 4, 264);
    const char label[] = "SkateBoard";
    Append(m, 8, sizeof(label), label, sizeof(label));
    std::vector<uint8_t> bounds;
    for (float v : low) F32(bounds, v);
    for (float v : high) F32(bounds, v);
    F32(bounds, radius);
    Put(m, 160, bounds.data(), 28); Put(m, 188, bounds.data(), 28);
    std::vector<uint8_t> sequence(64, 0);
    { const uint16_t h[2]{0, 0}; Put(sequence, 0, h, 4); const uint32_t d = 1000; Put(sequence, 4, &d, 4); const float f = 0; Put(sequence, 8, &f, 4);
      const uint32_t fl = 0x20; Put(sequence, 12, &fl, 4); const uint32_t w = 32767; Put(sequence, 16, &w, 4);
      Put(sequence, 32, bounds.data(), 28); const uint16_t t[2]{65535, 0}; Put(sequence, 60, t, 4); }
    Append(m, 28, 1, sequence.data(), sequence.size());
    { const uint16_t zero = 0; Append(m, 36, 1, &zero, 2); }
    std::vector<uint8_t> bone(88, 0);
    { const int32_t parent = -1; Put(bone, 0, &parent, 4); const uint32_t flags = 0; Put(bone, 4, &flags, 4);
      const uint16_t pair[2]{65535, 0}; Put(bone, 8, pair, 4);
      for (size_t o : {16u, 36u, 56u}) { const uint16_t p[2]{0, 65535}; Put(bone, o, p, 4); } }
    Append(m, 44, 1, bone.data(), bone.size());
    std::vector<uint8_t> records;
    for (size_t i = 0; i < nv; ++i) {
        for (int k = 0; k < 3; ++k) F32(records, local[i*3+k]);
        const uint8_t weights[8]{255, 0, 0, 0, 0, 0, 0, 0}; records.insert(records.end(), weights, weights + 8);
        double n[3]{normal[i*3], normal[i*3+1], normal[i*3+2]};
        const double len = sqrt(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
        if (len > 1e-12) for (double& v : n) v /= len; else { n[0] = 0; n[1] = 0; n[2] = 1; }
        for (double v : n) F32(records, float(v));
        for (int k = 0; k < 2; ++k) F32(records, mesh.uv[i][k]);
        for (int k = 0; k < 2; ++k) F32(records, mesh.uv[i][k]);
    }
    Append(m, 60, static_cast<uint32_t>(nv), records.data(), records.size());
    PutU32(m, 68, 1);
    Append(m, 80, textures, std::vector<uint8_t>(16 * textures).data(), 16 * textures);
    const uint32_t table = GetU32(m, 84);
    for (unsigned i = 0; i < textures; ++i) Append(m, table + 16*i + 8, static_cast<uint32_t>(out.blpName[i].size() + 1), out.blpName[i].c_str(), out.blpName[i].size() + 1);
    { std::vector<uint8_t> track; U16(track, 0); U16(track, 65535); for (int i = 0; i < 4; ++i) Raw(track, uint32_t(0));
      Append(m, 88, 1, track.data(), track.size()); }
    const uint32_t track = GetU32(m, 92);
    { std::vector<uint8_t> eight(8, 0); Append(m, track + 4, 1, eight.data(), 8); const uint32_t outer = GetU32(m, track + 8);
      const uint32_t zero = 0; Append(m, outer, 1, &zero, 4); }
    { std::vector<uint8_t> eight(8, 0); Append(m, track + 12, 1, eight.data(), 8); const uint32_t outer = GetU32(m, track + 16);
      const int16_t value = 32767; Append(m, outer, 1, &value, 2); }
    { std::vector<uint8_t> flags; for (unsigned i = 0; i < textures; ++i) { U16(flags, 0x04); U16(flags, 0); } // two-sided, opaque, lit
      Append(m, 112, textures, flags.data(), flags.size()); }
    const auto lookup = [&](size_t header, uint16_t first, uint16_t step, unsigned count) {
        std::vector<uint8_t> v; for (unsigned i = 0; i < count; ++i) U16(v, uint16_t(first + step * i)); Append(m, header, count, v.data(), v.size()); };
    lookup(120, 0, 0, 1); lookup(128, 0, 1, textures); lookup(136, 0, 0, textures); lookup(144, 0, 0, textures); lookup(152, 65535, 0, textures);

    memcpy(s.data(), "SKIN", 4);
    { std::vector<uint8_t> ids; for (size_t i = 0; i < nv; ++i) U16(ids, uint16_t(i)); Append(s, 4, static_cast<uint32_t>(nv), ids.data(), ids.size()); }
    { std::vector<uint8_t> idx; for (uint32_t i : tris) U16(idx, uint16_t(i)); Append(s, 12, static_cast<uint32_t>(tris.size()), idx.data(), idx.size()); }
    { std::vector<uint8_t> props(4 * nv, 0); Append(s, 20, static_cast<uint32_t>(nv), props.data(), props.size()); }
    std::vector<uint8_t> sections, batches;
    uint16_t used = 0;
    float centre[3]; for (int k = 0; k < 3; ++k) centre[k] = (low[k] + high[k]) / 2;
    for (unsigned mat = 0; mat < textures; ++mat) {
        const uint32_t first = start[mat], count = start[mat + 1] - first;
        if (!count) continue;
        Raw(sections, uint32_t(1502)); U16(sections, 0); U16(sections, uint16_t(nv)); U16(sections, uint16_t(first)); U16(sections, uint16_t(count));
        U16(sections, 1); U16(sections, 0); U16(sections, 1); U16(sections, 0);
        for (int k = 0; k < 3; ++k) F32(sections, centre[k]);
        for (int k = 0; k < 3; ++k) F32(sections, centre[k]);
        F32(sections, radius);
        batches.push_back(0); batches.push_back(0);
        const uint16_t fields[11]{0, used, used, 65535, uint16_t(mat), 0, 1, uint16_t(mat), uint16_t(mat), uint16_t(mat), uint16_t(mat)};
        for (uint16_t f : fields) U16(batches, f);
        ++used;
    }
    if (!used) return false;
    Append(s, 28, used, sections.data(), sections.size());
    Append(s, 36, used, batches.data(), batches.size());
    PutU32(s, 44, 1);
    for (unsigned i = 0; i < textures; ++i) {
        const auto& tex = (*mesh.textures)[i];
        out.blp.push_back(Blp(tex.width, tex.height, tex.rgba));
    }
    return true;
}
}

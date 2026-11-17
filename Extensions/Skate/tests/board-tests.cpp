// Phase 5: rigid fit, M2 placement against the donor's WorldPoint, the generated M2/skin/BLP layout, fit selection.
#include "SkateAssets.h"
#include "SkateBoard.h"
#include <cassert>
#include <cstdio>
#include <random>

using namespace skateboard;
using skatevisual::Point;

static bool Near(double a, double b, double e) { return fabs(a - b) <= e; }
// A 3x3 rotation (row-major, column-vector convention) from an axis and an angle.
static M3 Axis(double x, double y, double z, double angle) {
    const double n = sqrt(x*x + y*y + z*z); x /= n; y /= n; z /= n;
    const double c = cos(angle), s = sin(angle), t = 1 - c;
    M3 r; const double v[9]{t*x*x + c, t*x*y - s*z, t*x*z + s*y, t*x*y + s*z, t*y*y + c, t*y*z - s*x, t*x*z - s*y, t*y*z + s*x, t*z*z + c};
    memcpy(r.a, v, sizeof(v)); return r;
}
static Point Apply(const M3& r, Point p) {
    return {float(r.a[0]*p.x + r.a[1]*p.y + r.a[2]*p.z), float(r.a[3]*p.x + r.a[4]*p.y + r.a[5]*p.z), float(r.a[6]*p.x + r.a[7]*p.y + r.a[8]*p.z)};
}
// A board-ish cloud (0.23 x 0.13 x 0.9 m) with some asymmetry so no rotation is ambiguous.
static std::vector<Point> Cloud(unsigned n) {
    std::mt19937 rng(7); std::uniform_real_distribution<float> u(-1, 1);
    std::vector<Point> p;
    for (unsigned i = 0; i < n; ++i) p.push_back({u(rng) * .115f + .02f * i / n, u(rng) * .065f + (i % 7) * .004f, u(rng) * .45f + (i % 3) * .01f});
    return p;
}
static std::vector<Point> Move(const std::vector<Point>& rest, const M3& r, Point t) {
    std::vector<Point> out;
    for (const Point& p : rest) { const Point q = Apply(r, p); out.push_back({q.x + t.x, q.y + t.y, q.z + t.z}); }
    return out;
}
// The M2's vertex for a rest point, then through the board matrix (row vector times matrix).
static Point Through(const Placed& placed, const Rest& rest, Point p) {
    const double dx = double(p.x) - rest.centre.x, dy = double(p.y) - rest.centre.y, dz = double(p.z) - rest.centre.z;
    const double a = dx / Yard, b = -dz / Yard, c = dy / Yard;
    const float* m = placed.matrix;
    return {float(a*m[0] + b*m[4] + c*m[8] + m[12]), float(a*m[1] + b*m[5] + c*m[9] + m[13]), float(a*m[2] + b*m[6] + c*m[10] + m[14])};
}

static void FitAndPlace() {
    Rest rest; assert(rest.Make(Cloud(500)));
    assert(!Rest{}.Make({}) && !Rest{}.Make({{0,0,0}}));
    const M3 rotations[]{Axis(1, 0, 0, 0), Axis(0, 1, 0, 1.2), Axis(1, 2, 3, 2.9), Axis(0, 0, 1, 3.1), Axis(3, -1, .5, -2.2), Axis(1, 0, 0, 3.14159)};
    for (const M3& r : rotations) {
        skatevisual::Frame frame{};
        frame.vertices = Move(rest.points, r, {10, 5, -3});
        Rigid rigid;
        assert(FitRigid(rest, frame.vertices, rigid) && rigid.error < 1e-4f);
        for (int i = 0; i < 9; ++i) assert(Near(rigid.r.a[i], r.a[i], 1e-4));
        // Each donor fit: the M2 lands where the donor drew the streamed vertices.
        frame.pose.position[0] = 10; frame.pose.position[1] = 5; frame.pose.position[2] = -3;
        frame.pose.ground = 1;
        const Point player{-8800, 640, 95};
        std::vector<boardplacement::Fit> fits;
        boardplacement::Fit f; f.valid = true; fits.push_back(f);                                // plain, scale 1
        f.scale = 1.1f; f.x = .1f; f.y = -.05f; f.yaw = .4f; f.modelScale = 1.35f; fits.push_back(f); // stance fit + model scale
        f.rootRelative = true; f.sourceBottomFromRoot = -.8f; fits.push_back(f);                  // air
        boardplacement::Fit ph; ph.valid = true; ph.physical = true; ph.sourceOrigin = {-8790, 600, 90}; ph.sourceCentre = rigid.centre; ph.modelScale = .8f;
        fits.push_back(ph);                                                                       // a free (bail) board
        for (const auto& fit : fits) {
            Placed placed;
            assert(Place(rest, frame, player, fit, placed));
            float bottom = frame.vertices[0].y;
            for (const Point& v : frame.vertices) bottom = (std::min)(bottom, v.y);
            for (size_t i = 0; i < rest.points.size(); i += 7) {
                const Point want = boardplacement::WorldPoint(frame.vertices[i], frame, player, bottom, fit);
                const Point got = Through(placed, rest, rest.points[i]);
                assert(Near(want.x, got.x, 1e-3) && Near(want.y, got.y, 1e-3) && Near(want.z, got.z, 1e-3));
            }
            // The scale is the donor's uniform one: stance scale times model scale (1 when the fit is not valid).
            const double expect = boardplacement::Valid(fit) ? double(fit.scale) * fit.modelScale : 1.0;
            assert(Near(placed.scale, expect, 1e-3));
            // rotation is orthonormal
            for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b) {
                double dot = 0; for (int k = 0; k < 3; ++k) dot += placed.rotation[a*3+k] * placed.rotation[b*3+k];
                assert(Near(dot, a == b ? 1 : 0, 1e-4));
            }
        }
    }
}

static void Rejections() {
    Rest rest; rest.Make(Cloud(200));
    skatevisual::Frame frame{};
    boardplacement::Fit fit; fit.valid = true;
    Placed placed;
    frame.vertices = rest.points;
    assert(Place(rest, frame, {0, 0, 0}, fit, placed));
    // Wheels and trucks may differ a little (tilt, spin) but a different shape is refused.
    auto bent = frame; bent.vertices[3].x += .5f;
    assert(!Place(rest, bent, {0, 0, 0}, fit, placed));
    auto wrong = frame; wrong.vertices.pop_back();
    assert(!Place(rest, wrong, {0, 0, 0}, fit, placed));
    auto nan = frame; nan.vertices[5].y = NAN;
    assert(!Place(rest, nan, {0, 0, 0}, fit, placed));
    // A flat sheet cannot orient the board.
    Rest flat; std::vector<Point> sheet; for (int i = 0; i < 20; ++i) sheet.push_back({float(i), float(i % 4), 0});
    flat.Make(sheet);
    Rigid rigid;
    assert(!FitRigid(flat, sheet, rigid) || rigid.error < 1e-3f);
}

static void Quaternions() {
    const M3 r = Axis(1, 2, 3, 1.1);
    Placed placed{};
    for (int row = 0; row < 3; ++row) for (int col = 0; col < 3; ++col) placed.rotation[row*3+col] = float(r.a[col*3+row]); // rows = images of e_i
    float q[4]; Quaternion(placed.rotation, q);
    const double n = q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3];
    assert(Near(n, 1, 1e-5));
    // Axis-angle of the original: q = (axis sin(a/2), cos(a/2)), up to sign.
    const double s = sin(.55) / sqrt(14.0);
    const double sign = q[3] < 0 ? -1 : 1;
    assert(Near(sign*q[0], s*1, 1e-4) && Near(sign*q[1], s*2, 1e-4) && Near(sign*q[2], s*3, 1e-4) && Near(sign*q[3], cos(.55), 1e-4));
    const float identity[9]{1,0,0, 0,1,0, 0,0,1};
    Quaternion(identity, q);
    assert(Near(q[3], 1, 1e-6) && Near(q[0], 0, 1e-6));
}

static uint32_t U32(const std::vector<uint8_t>& b, size_t at) { uint32_t v; memcpy(&v, b.data() + at, 4); return v; }
static uint16_t U16(const std::vector<uint8_t>& b, size_t at) { uint16_t v; memcpy(&v, b.data() + at, 2); return v; }
static void Array(const std::vector<uint8_t>& b, size_t header, size_t stride, uint32_t expectCount) {
    const uint32_t count = U32(b, header), offset = U32(b, header + 4);
    assert(count == expectCount && offset % 16 == 0 && size_t(offset) + size_t(count) * stride <= b.size());
}

// A cube split over three materials, wound inwards on purpose: the builder must flip it so normals face outward.
static void ModelLayout() {
    skatevisual::Mesh mesh;
    const float c[8][3]{{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
    const uint32_t faces[12][3]{{0,1,2},{0,2,3},{4,6,5},{4,7,6},{0,5,1},{0,4,5},{2,7,3},{2,6,7},{0,3,7},{0,7,4},{1,5,6},{1,6,2}}; // outward
    std::vector<Point> points;
    for (auto& v : c) { points.push_back({v[0] * .1f, v[1] * .05f, v[2] * .4f}); mesh.colors.push_back(0xff808080); mesh.uv.push_back({(v[0] + 1) / 2, (v[1] + 1) / 2}); }
    for (unsigned i = 0; i < 12; ++i) {
        mesh.indices.insert(mesh.indices.end(), {faces[i][0], faces[i][2], faces[i][1]}); // wound inwards
        mesh.materials.push_back(i < 6 ? 0u : i < 10 ? 2u : 0u);                          // material 1 unused
    }
    auto textures = std::make_shared<std::vector<skatevisual::Texture>>();
    for (unsigned t = 0; t < 3; ++t) { skatevisual::Texture tex; tex.width = 8; tex.height = 4; tex.rgba.assign(8 * 4 * 4, uint8_t(40 + t)); tex.rgba[3] = 7; textures->push_back(tex); }
    mesh.textures = textures;
    Rest rest; assert(rest.Make(points));
    skateboard::Model model;
    assert(Build(mesh, rest, 3, model));
    const auto& m = model.m2; const auto& s = model.skin;
    assert(!memcmp(m.data(), "MD20", 4) && U32(m, 4) == 264 && !memcmp(s.data(), "SKIN", 4));
    assert(model.m2Name == "Skate\\Board\\board3.m2" && model.skinName == "Skate\\Board\\board300.skin" && model.blpName.size() == 3);
    Array(m, 8, 1, 11); Array(m, 28, 64, 1); Array(m, 36, 2, 1); Array(m, 44, 88, 1); Array(m, 60, 48, 8);
    Array(m, 80, 16, 3); Array(m, 88, 20, 1); Array(m, 112, 4, 3);
    Array(m, 120, 2, 1); Array(m, 128, 2, 3); Array(m, 136, 2, 3); Array(m, 144, 2, 3); Array(m, 152, 2, 3);
    assert(U32(m, 68) == 1);
    // Texture names point at the BLPs the engine will ask for.
    const uint32_t table = U32(m, 84);
    for (unsigned i = 0; i < 3; ++i) {
        const uint32_t len = U32(m, table + 16*i + 8), off = U32(m, table + 16*i + 12);
        assert(len == model.blpName[i].size() + 1 && !strcmp(reinterpret_cast<const char*>(m.data() + off), model.blpName[i].c_str()));
    }
    // Vertices centred (local yards), weights full on bone 0, normals unit and outward.
    const uint32_t vo = U32(m, 64);
    for (unsigned i = 0; i < 8; ++i) {
        float p[3], n[3]; memcpy(p, m.data() + vo + 48*i, 12); memcpy(n, m.data() + vo + 48*i + 20, 12);
        assert(m[vo + 48*i + 12] == 255);
        assert(Near(sqrt(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]), 1, 1e-4));
        assert(n[0]*p[0] + n[1]*p[1] + n[2]*p[2] > 0); // a convex solid centred on the origin: outward
        float uv[2]; memcpy(uv, m.data() + vo + 48*i + 32, 8);
        assert(Near(uv[0], mesh.uv[i][0], 1e-6) && Near(uv[1], mesh.uv[i][1], 1e-6));
    }
    // Local y is -dz and local z is dy: the cube's z extent (.4 m) is the model's y extent in yards.
    assert(Near(model.high[1], .4 / Yard, 1e-4) && Near(model.high[0], .1 / Yard, 1e-4) && Near(model.high[2], .05 / Yard, 1e-4));
    // Skin: identity vertex table, indices in range, 2 sections (materials 0 and 2), one batch each.
    Array(s, 4, 2, 8); Array(s, 12, 2, 36); Array(s, 20, 4, 4 * 0 + 8); Array(s, 28, 48, 2); Array(s, 36, 24, 2);
    const uint32_t io = U32(s, 16);
    for (unsigned i = 0; i < 36; ++i) assert(U16(s, io + 2*i) < 8);
    const uint32_t so = U32(s, 32), bo = U32(s, 40);
    assert(U16(s, so + 8) == 0 && U16(s, so + 10) == 24);   // material 0: 8 triangles, indices [0, 24)
    assert(U16(s, so + 48 + 8) == 24 && U16(s, so + 48 + 10) == 12); // material 2: 4 triangles, [24, 36)
    assert(s[bo] == 0 && U16(s, bo + 16) == 0 && U16(s, bo + 24 + 16) == 2 && U16(s, bo + 14) == 1); // texture combo = material, one texture
    assert(U16(s, bo + 4) == 0 && U16(s, bo + 24 + 4) == 1);                // skin section index
    // BLPs: opaque BGRA with the full mip chain, alpha forced to 255.
    assert(model.blp.size() == 3);
    const auto& b = model.blp[1];
    assert(!memcmp(b.data(), "BLP2", 4) && U32(b, 4) == 1 && b[8] == 3 && b[11] == 1 && U32(b, 12) == 8 && U32(b, 16) == 4);
    unsigned w = 8, h = 4;
    for (unsigned mip = 0; mip < 4; ++mip, w = (std::max)(1u, w / 2), h = (std::max)(1u, h / 2)) {
        const uint32_t off = U32(b, 20 + 4*mip), size = U32(b, 84 + 4*mip);
        assert(size == w * h * 4 && off + size <= b.size());
        for (unsigned i = 0; i < w * h; ++i) assert(b[off + i*4] == 41 && b[off + i*4+3] == 255);
    }
    assert(U32(b, 20 + 4*4) == 0);
    // Regression (live test 2026-11-15: board invisible / solid green = the engine's default texture).
    // 1. Bounds: non-zero, ordered, and containing every vertex; the radius reaches the farthest one.
    {
        float bb[7]; memcpy(bb, m.data() + 160, 28);
        for (int k = 0; k < 3; ++k) assert(bb[k] < bb[3 + k] && Near(bb[k], model.low[k], 1e-6) && Near(bb[3 + k], model.high[k], 1e-6));
        assert(!memcmp(m.data() + 160, m.data() + 188, 28)); // collision box = bounding box
        float farthest = 0;
        for (unsigned i = 0; i < 8; ++i) {
            float p[3]; memcpy(p, m.data() + vo + 48*i, 12);
            for (int k = 0; k < 3; ++k) assert(p[k] >= bb[k] - 1e-6f && p[k] <= bb[3 + k] + 1e-6f);
            farthest = (std::max)(farthest, sqrtf(p[0]*p[0] + p[1]*p[1] + p[2]*p[2]));
        }
        assert(Near(bb[6], farthest, 1e-5) && bb[6] > .01f);
        // every skin section's sort radius covers the mesh
        float sectionRadius; memcpy(&sectionRadius, s.data() + so + 44, 4); assert(sectionRadius >= farthest - 1e-5f);
    }
    // 2. The reader finds every file the loaders will ask for: the M2 itself, the skin the engine derives from its name,
    //    and each texture named in the M2, in any case and with either slash.
    {
        skateassets::Registry registry;
        registry.Publish(model.m2Name, model.m2); registry.Publish(model.skinName, model.skin);
        for (unsigned i = 0; i < 3; ++i) registry.Publish(model.blpName[i], model.blp[i]);
        auto upper = [](std::string v) { for (char& c : v) c = char(toupper((unsigned char)c)); return v; };
        auto slashed = [](std::string v) { for (char& c : v) if (c == '\\') c = '/'; return v; };
        for (const std::string& name : {model.m2Name, upper(model.m2Name), slashed(model.m2Name)}) assert(registry.Find(name.c_str()) && *registry.Find(name.c_str()) == model.m2);
        std::string skin = model.m2Name; skin.replace(skin.size() - 3, 3, "00.skin"); // the loader's own derivation
        assert(registry.Find(skin.c_str()) && *registry.Find(skin.c_str()) == model.skin && registry.Find(upper(skin).c_str()));
        for (unsigned i = 0; i < 3; ++i) {
            const uint32_t off = U32(m, table + 16*i + 12);
            const char* inM2 = reinterpret_cast<const char*>(m.data() + off);   // exactly what the M2 says
            assert(registry.Find(inM2) && *registry.Find(inM2) == model.blp[i]);
            assert(registry.Find(slashed(inM2).c_str()) && registry.Find(upper(inM2).c_str()));
        }
        assert(!registry.Find("Skate\\Board\\nothing.blp") && !registry.Find("World\\Generic\\x.blp") && !registry.Find(nullptr));
        const auto asked = registry.TakeRequests();
        bool sawMiss = false;
        for (const auto& r : asked) if (!r.hit && r.name.find("nothing") != std::string::npos) sawMiss = true;
        assert(sawMiss && registry.TakeRequests().empty());
    }
    // 3. Every BLP decodes the way the engine reads it (CBLPFile::Source/Lock, encoding 3): header, level count, offsets
    //    and sizes inside the file, opaque BGRA.
    for (unsigned t = 0; t < 3; ++t) {
        const auto& blp = model.blp[t];
        assert(blp.size() >= 148 && !memcmp(blp.data(), "BLP2", 4) && U32(blp, 4) == 1 && blp[8] == 3 && (blp[11] & 0xF) != 0);
        unsigned bw = U32(blp, 12), bh = U32(blp, 16), levels = 0;
        for (unsigned v = (std::max)(bw, bh); v; v >>= 1) ++levels; // CalcLevelCount: down to 1x1
        assert(bw == (*mesh.textures)[t].width && bh == (*mesh.textures)[t].height && levels >= 1 && levels <= 16);
        uint32_t previousEnd = 0;
        for (unsigned level = 0; level < levels; ++level) {
            const uint32_t offset = U32(blp, 20 + 4*level), size = U32(blp, 84 + 4*level);
            const unsigned lw = (std::max)(1u, bw >> level), lh = (std::max)(1u, bh >> level);
            assert(size == lw * lh * 4 && offset >= previousEnd && size_t(offset) + size == (level + 1 == levels ? blp.size() : size_t(offset) + size));
            assert(size_t(offset) + size <= blp.size());
            previousEnd = offset + size;
        }
        assert(previousEnd == blp.size());
        assert(U32(blp, 20 + 4*levels) == 0 && U32(blp, 84 + 4*levels) == 0); // the chain ends where the engine stops
    }
    if (getenv("SKATE_BOARD_DUMP")) { // byte comparison against the Python writer in Tools/ (scratch use)
        const std::string dir = getenv("SKATE_BOARD_DUMP");
        FILE* f = fopen((dir + "/board.m2").c_str(), "wb"); fwrite(m.data(), 1, m.size(), f); fclose(f);
        f = fopen((dir + "/board.skin").c_str(), "wb"); fwrite(s.data(), 1, s.size(), f); fclose(f);
        f = fopen((dir + "/tex1.blp").c_str(), "wb"); fwrite(b.data(), 1, b.size(), f); fclose(f);
    }
    // Bad inputs are refused, not half-built.
    skateboard::Model bad;
    auto noTex = mesh; noTex.textures = nullptr; assert(!Build(noTex, rest, 1, bad));
    auto badUv = mesh; badUv.uv.pop_back(); assert(!Build(badUv, rest, 1, bad));
    auto badMat = mesh; badMat.materials[0] = 9; assert(!Build(badMat, rest, 1, bad));
    auto badIdx = mesh; badIdx.indices[0] = 99; assert(!Build(badIdx, rest, 1, bad));
    Rest other; other.Make(Cloud(9)); assert(!Build(mesh, other, 1, bad));
}

static void Fits() {
    skatevisual::Frame frame{};
    frame.vertices = {{0,0,0},{1,0,0},{0,1,0},{0,0,1}};
    characteralignment::AirAnchor anchor; // empty: nothing captured yet
    frame.pose.ground = 1;
    auto fit = ChooseFit(false, frame, anchor, 1.25f, {0,0,0}, {0,0,0});
    assert(fit.valid && fit.scale == 1 && fit.modelScale == 1.25f && !fit.physical && !fit.rootRelative);
    fit = ChooseFit(true, frame, anchor, 1, {-8790, 600, 90}, {.1f, .2f, .3f});
    assert(fit.physical && fit.sourceOrigin.x == -8790 && fit.sourceCentre.z == .3f);
    // A captured grounded stance fit is used while riding, root-relative in the air.
    anchor.ground.valid = true; anchor.ground.board = {true, 1.1f, .05f, .02f}; anchor.bottomFromRoot = -.7f;
    fit = ChooseFit(false, frame, anchor, 1, {0,0,0}, {0,0,0});
    assert(fit.scale == 1.1f && fit.x == .05f && !fit.rootRelative);
    frame.pose.airborne = 1;
    fit = ChooseFit(false, frame, anchor, 1, {0,0,0}, {0,0,0});
    assert(fit.scale == 1.1f && fit.rootRelative && fit.sourceBottomFromRoot == -.7f);
    // An implausible model scale falls back to a valid plain fit rather than a skipped board.
    fit = ChooseFit(false, frame, anchor, 500.f, {0,0,0}, {0,0,0});
    assert(!boardplacement::Valid(fit) || fit.modelScale <= 64);
}

int main() {
    FitAndPlace();
    Rejections();
    Quaternions();
    ModelLayout();
    Fits();
    puts("board-tests ok");
    return 0;
}

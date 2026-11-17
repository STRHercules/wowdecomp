// Phase 5 against the real helper's stream (game-derived, so never committed): SKATE_BOARD_CAPTURE=<dir> holding mesh.bin
// (op 7 reply) and frames.bin (u32 length + op 22 reply, repeated). Passes without doing anything when it is unset.
#include "SkateBoard.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>

static std::vector<unsigned char> File(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(f), {});
}

int main() {
    const char* dir = getenv("SKATE_BOARD_CAPTURE");
    if (!dir) { puts("board-real-tests skipped (SKATE_BOARD_CAPTURE not set)"); return 0; }
    const std::string base = dir;
    const auto mesh = skatevisual::Schema(File(base + "/mesh.bin"));
    assert(mesh && mesh->textures && mesh->textures->size() == 3);
    const auto data = File(base + "/frames.bin");
    std::vector<std::shared_ptr<const skatevisual::Frame>> frames;
    for (size_t at = 0; at + 4 <= data.size();) {
        uint32_t n; memcpy(&n, &data[at], 4);
        std::vector<unsigned char> reply(data.begin() + at + 4, data.begin() + at + 4 + n);
        at += 4 + n;
        auto frame = skatevisual::Decode(reply, *mesh, true, nullptr, true);
        assert(frame && frame->vertices.size() == mesh->colors.size());
        frames.push_back(frame);
    }
    assert(frames.size() > 100);
    skateboard::Rest rest;
    assert(rest.Make(frames[0]->vertices));
    skateboard::Model model;
    assert(skateboard::Build(*mesh, rest, 1, model));
    printf("model: %zu bytes m2, %zu skin, %zu textures, extent %.3f x %.3f x %.3f yd\n", model.m2.size(), model.skin.size(),
        model.blp.size(), model.high[0]-model.low[0], model.high[1]-model.low[1], model.high[2]-model.low[2]);
    FILE* f = fopen((base + "/board.m2").c_str(), "wb"); fwrite(model.m2.data(), 1, model.m2.size(), f); fclose(f);
    f = fopen((base + "/board.skin").c_str(), "wb"); fwrite(model.skin.data(), 1, model.skin.size(), f); fclose(f);
    float worst = 0, worstGap = 0;
    for (const auto& frame : frames) {
        skateboard::Placed placed;
        boardplacement::Fit fit; fit.valid = true; fit.scale = 1.1f; fit.x = .05f; fit.y = -.03f; fit.modelScale = 1.2f;
        const skatevisual::Point player{-8800, 640, 95};
        assert(skateboard::Place(rest, *frame, player, fit, placed));
        worst = (std::max)(worst, placed.error);
        float bottom = frame->vertices[0].y;
        for (const auto& v : frame->vertices) bottom = (std::min)(bottom, v.y);
        for (size_t i = 0; i < rest.points.size(); i += 5) {
            const auto want = boardplacement::WorldPoint(frame->vertices[i], *frame, player, bottom, fit);
            const double dx = (double(rest.points[i].x) - rest.centre.x) / skateboard::Yard, dy = (double(rest.points[i].y) - rest.centre.y) / skateboard::Yard,
                dz = (double(rest.points[i].z) - rest.centre.z) / skateboard::Yard;
            const double a = dx, b = -dz, c = dy;
            const float* m = placed.matrix;
            const double got[3]{a*m[0] + b*m[4] + c*m[8] + m[12], a*m[1] + b*m[5] + c*m[9] + m[13], a*m[2] + b*m[6] + c*m[10] + m[14]};
            worstGap = (std::max)(worstGap, float(fabs(got[0]-want.x) + fabs(got[1]-want.y) + fabs(got[2]-want.z)));
        }
    }
    printf("frames %zu, worst rigid residual %.4f m, worst world gap %.4f yd\n", frames.size(), worst, worstGap);
    assert(worst < skateboard::MaxResidual && worstGap < .25f);
    return 0;
}

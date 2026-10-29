// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOW_SOURCE_WORLDCLIENT_HD_HDCAMERA_H
#define WOW_SOURCE_WORLDCLIENT_HD_HDCAMERA_H

// Not in the original binary. The world's camera as the opaque models leave it, for the Azeroth Remastered passes
// that run after the world (the donor took it from its own backend's frame): D3D-style projection (row-major,
// _34 = 1, z 0..1), the engine's view matrix, and the viewport in target pixels.
namespace HD {

struct Camera {
    bool valid = false;
    float projection[16] = {};
    float view[16] = {};
    float x = 0, y = 0, width = 0, height = 0, minZ = 0, maxZ = 1;
};

// After the opaque models (beside Lumenveil::OpaqueDone); only with visuals 2.
void CaptureCamera();
// The last capture. A frame whose world draws no models keeps the one before (the passes are skipped then).
const Camera& CurrentCamera();

}  // namespace HD

#endif

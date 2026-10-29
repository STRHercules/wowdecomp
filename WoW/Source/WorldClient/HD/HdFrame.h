// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOW_SOURCE_WORLDCLIENT_HD_HDFRAME_H
#define WOW_SOURCE_WORLDCLIENT_HD_HDFRAME_H

#include <cstdint>

namespace GxEffects {
struct Texture;
}

// Not in the original binary. The world's colour as the Azeroth Remastered passes write it: one RGBA16F storage texture
// of the target's size (GLSL has no BGRA image format) shared by AO, fog, bloom and grading. Open copies the world into
// it, or returns it as it is when it is already open, so passes run back to back (bloom, then grading) share one copy;
// Close draws it back onto the world.
namespace HD::Frame {

// The open frame, copying the world into it first if it is not open. Null (nothing copied) when it cannot be made.
GxEffects::Texture* Open(uint32_t width, uint32_t height);
// Draws the open frame back onto the world and closes it; nothing when none is open. False when the draw failed. A pass
// that fails leaves the frame as it was copied (each writes it in its last dispatch only), so closing is always safe.
bool Close();

}  // namespace HD::Frame

#endif

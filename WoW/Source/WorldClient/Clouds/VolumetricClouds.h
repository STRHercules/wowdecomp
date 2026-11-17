#pragma once

class CRect;

namespace VolumetricClouds {
void RegisterCVars();
// Replaces the stock cloud draw only when the Vulkan pass succeeds, clipped to this sky portal.
bool Render(const CRect& rect);
}

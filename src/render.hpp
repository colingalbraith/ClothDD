/// @file render.hpp
/// @brief Scene rendering — cloth surface, wireframe, HDRI sky dome, debug overlays.

#pragma once

#include "app.hpp"

namespace clothdd {

void renderFrame(const AppState& app, int framebufferWidth, int framebufferHeight);

}  // namespace clothdd

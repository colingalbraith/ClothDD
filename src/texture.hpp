/// @file texture.hpp
/// @brief Utility for loading image files as OpenGL textures via stb_image.

#pragma once
#include <string>

namespace clothdd {

unsigned int loadTexture(const std::string &path);

} // namespace clothdd

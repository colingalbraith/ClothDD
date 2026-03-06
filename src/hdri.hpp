/// @file hdri.hpp
/// @brief OpenEXR HDRI image loading with Reinhard tone mapping.

#pragma once

#include <string>
#include <vector>

namespace clothdd {

struct HdriImage {
  int width = 0;
  int height = 0;
  std::vector<unsigned char> rgb8;
};

bool loadExrHdriAsRgb8(const std::string& filePath, HdriImage& outImage, std::string& errorOut);

}  // namespace clothdd

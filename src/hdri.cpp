/// @file hdri.cpp
/// @brief OpenEXR HDRI loader — reads half-float RGBA, tone-maps to sRGB LDR.

#include "hdri.hpp"

#include <algorithm>
#include <cmath>
#include <exception>

#if defined(CLOTHDD_HAVE_OPENEXR)
#include <Imath/ImathBox.h>
#include <ImfArray.h>
#include <ImfRgba.h>
#include <ImfRgbaFile.h>
#endif

namespace clothdd {

namespace {

float toneMapChannel(float value) {
  const float clamped = std::max(0.0f, value);
  const float mapped = clamped / (1.0f + clamped);
  return std::pow(mapped, 1.0f / 2.2f);
}

} // namespace

bool loadExrHdriAsRgb8(const std::string &filePath, HdriImage &outImage,
                       std::string &errorOut) {
#if defined(CLOTHDD_HAVE_OPENEXR)
  try {
    Imf::RgbaInputFile exrFile(filePath.c_str());
    const Imath::Box2i dataWindow = exrFile.dataWindow();

    const int width = dataWindow.max.x - dataWindow.min.x + 1;
    const int height = dataWindow.max.y - dataWindow.min.y + 1;
    if (width <= 0 || height <= 0) {
      errorOut = "Invalid EXR data window size.";
      return false;
    }

    Imf::Array2D<Imf::Rgba> pixels;
    pixels.resizeErase(height, width);

    exrFile.setFrameBuffer(
        &pixels[0][0] - dataWindow.min.x - dataWindow.min.y * width, 1, width);
    exrFile.readPixels(dataWindow.min.y, dataWindow.max.y);

    HdriImage image;
    image.width = width;
    image.height = height;
    image.rgb8.resize(static_cast<std::size_t>(width) *
                      static_cast<std::size_t>(height) * 3U);

    for (int y = 0; y < height; ++y) {
      const int dstY = height - 1 - y;
      for (int x = 0; x < width; ++x) {
        const Imf::Rgba &px = pixels[y][x];
        const float r = toneMapChannel(static_cast<float>(px.r));
        const float g = toneMapChannel(static_cast<float>(px.g));
        const float b = toneMapChannel(static_cast<float>(px.b));

        const std::size_t base =
            (static_cast<std::size_t>(dstY) * static_cast<std::size_t>(width) +
             static_cast<std::size_t>(x)) *
            3U;
        image.rgb8[base + 0U] =
            static_cast<unsigned char>(std::clamp(r, 0.0f, 1.0f) * 255.0f);
        image.rgb8[base + 1U] =
            static_cast<unsigned char>(std::clamp(g, 0.0f, 1.0f) * 255.0f);
        image.rgb8[base + 2U] =
            static_cast<unsigned char>(std::clamp(b, 0.0f, 1.0f) * 255.0f);
      }
    }

    outImage = std::move(image);
    errorOut.clear();
    return true;
  } catch (const std::exception &exception) {
    errorOut = exception.what();
    return false;
  }
#else
  (void)filePath;
  (void)outImage;
  errorOut = "OpenEXR support is not enabled in this build.";
  return false;
#endif
}

} // namespace clothdd

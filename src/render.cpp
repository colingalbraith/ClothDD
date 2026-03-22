/// @file render.cpp
/// @brief OpenGL 2.1 fixed-function rendering pipeline: HDRI sky, textured cloth,
///        domain colour overlay, wireframe, ground plane, and sphere collider.

#include "render.hpp"
#include "hdri.hpp"
#include "texture.hpp"

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_GENERATE_MIPMAP
#define GL_GENERATE_MIPMAP 0x8191
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace clothdd {
namespace {

constexpr float kPi = 3.14159265358979323846f;

void setPerspective(float fovYDegrees, float aspect, float zNear, float zFar) {
  const float halfHeight = zNear * std::tan(fovYDegrees * kPi / 360.0f);
  const float halfWidth = halfHeight * aspect;
  glFrustum(-halfWidth, halfWidth, -halfHeight, halfHeight, zNear, zFar);
}

void setColor(const std::array<float, 3> &color) {
  glColor3f(color[0], color[1], color[2]);
}

struct HdriTextureCache {
  GLuint textureId = 0;
  bool valid = false;
  bool attempted = false;
  std::string sourcePath;
};

HdriTextureCache &hdriTextureCache() {
  static HdriTextureCache cache;
  return cache;
}

void releaseHdriTexture(HdriTextureCache &cache) {
  if (cache.textureId != 0U) {
    glDeleteTextures(1, &cache.textureId);
  }
  cache.textureId = 0U;
  cache.valid = false;
}

struct DiffuseTextureCache {
  GLuint textureId = 0;
  bool valid = false;
  bool attempted = false;
  std::string sourcePath;
};

DiffuseTextureCache &diffuseTextureCache() {
  static DiffuseTextureCache cache;
  return cache;
}

bool ensureDiffuseTextureReady() {
  DiffuseTextureCache &cache = diffuseTextureCache();
  if (cache.valid)
    return true;
  if (cache.attempted)
    return false;

  cache.attempted = true;
  cache.sourcePath =
      "../cotton_jersey_4k.blend/textures/cotton_jersey_diff_4k.jpg";
  cache.textureId = clothdd::loadTexture(cache.sourcePath);
  if (cache.textureId != 0U) {
    cache.valid = true;
    return true;
  }
  return false;
}

bool ensureHdriTextureReady(const AppState &app) {
  if (!app.hdriBackgroundEnabled || app.hdriPath.empty()) {
    return false;
  }

  HdriTextureCache &cache = hdriTextureCache();
  if (cache.valid && cache.sourcePath == app.hdriPath) {
    return true;
  }
  if (cache.attempted && cache.sourcePath == app.hdriPath) {
    return false;
  }

  releaseHdriTexture(cache);
  cache.sourcePath = app.hdriPath;
  cache.attempted = true;

  HdriImage image;
  std::string error;
  if (!loadExrHdriAsRgb8(app.hdriPath, image, error)) {
    std::fprintf(stderr, "Failed to load HDRI '%s': %s\n", app.hdriPath.c_str(),
                 error.c_str());
    return false;
  }

  GLuint texture = 0U;
  glGenTextures(1, &texture);
  if (texture == 0U) {
    std::fprintf(stderr, "Failed to allocate OpenGL texture for HDRI '%s'.\n",
                 app.hdriPath.c_str());
    return false;
  }

  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, image.width, image.height, 0, GL_RGB,
               GL_UNSIGNED_BYTE, image.rgb8.data());

  float maxAnisotropy = 1.0f;
  glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropy);
  if (maxAnisotropy > 1.0f) {
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                    maxAnisotropy);
  }

  glBindTexture(GL_TEXTURE_2D, 0U);

  cache.textureId = texture;
  cache.valid = true;
  return true;
}

void drawHdriSkyDome(const AppState &app) {
  if (!ensureHdriTextureReady(app)) {
    return;
  }

  const HdriTextureCache &cache = hdriTextureCache();
  if (!cache.valid || cache.textureId == 0U) {
    return;
  }

  const GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
  const GLboolean lightingWasEnabled = glIsEnabled(GL_LIGHTING);
  GLboolean depthWriteMask = GL_TRUE;
  glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWriteMask);

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_LIGHTING);
  glDepthMask(GL_FALSE);
  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, cache.textureId);
  glColor3f(1.0f, 1.0f, 1.0f);

  constexpr int kLatSegments = 48;
  constexpr int kLonSegments = 96;
  constexpr float kRadius = 32.0f;
  for (int lat = 0; lat < kLatSegments; ++lat) {
    const float v0 = static_cast<float>(lat) / static_cast<float>(kLatSegments);
    const float v1 =
        static_cast<float>(lat + 1) / static_cast<float>(kLatSegments);
    const float theta0 = v0 * kPi;
    const float theta1 = v1 * kPi;

    glBegin(GL_QUAD_STRIP);
    for (int lon = 0; lon <= kLonSegments; ++lon) {
      const float u =
          static_cast<float>(lon) / static_cast<float>(kLonSegments);
      const float phi = u * 2.0f * kPi;

      const float sinTheta0 = std::sin(theta0);
      const float cosTheta0 = std::cos(theta0);
      const float sinTheta1 = std::sin(theta1);
      const float cosTheta1 = std::cos(theta1);
      const float cosPhi = std::cos(phi);
      const float sinPhi = std::sin(phi);

      const Vec3 p0{sinTheta0 * cosPhi * kRadius, cosTheta0 * kRadius,
                    sinTheta0 * sinPhi * kRadius};
      const Vec3 p1{sinTheta1 * cosPhi * kRadius, cosTheta1 * kRadius,
                    sinTheta1 * sinPhi * kRadius};

      glTexCoord2f(u, 1.0f - v0);
      glVertex3f(p0.x, p0.y, p0.z);
      glTexCoord2f(u, 1.0f - v1);
      glVertex3f(p1.x, p1.y, p1.z);
    }
    glEnd();
  }

  glBindTexture(GL_TEXTURE_2D, 0U);
  glDisable(GL_TEXTURE_2D);
  glDepthMask(depthWriteMask);
  if (depthWasEnabled == GL_TRUE) {
    glEnable(GL_DEPTH_TEST);
  }
  if (lightingWasEnabled == GL_TRUE) {
    glEnable(GL_LIGHTING);
  }
}

void drawGroundPlane(float y, const std::array<float, 3> &color) {
  glEnable(GL_LIGHTING);
  glNormal3f(0.0f, 1.0f, 0.0f);

  constexpr float kExtent = 6.0f;
  constexpr float kTileSize = 0.5f;
  constexpr int kTiles =
      static_cast<int>(2.0f * kExtent / kTileSize);

  const float darkScale = 0.55f;
  const std::array<float, 3> colorA = color;
  const std::array<float, 3> colorB = {
      color[0] * darkScale, color[1] * darkScale, color[2] * darkScale};

  glBegin(GL_QUADS);
  for (int iz = 0; iz < kTiles; ++iz) {
    for (int ix = 0; ix < kTiles; ++ix) {
      const bool dark = ((ix + iz) & 1) != 0;
      const auto &c = dark ? colorB : colorA;
      glColor3f(c[0], c[1], c[2]);

      const float x0 = -kExtent + static_cast<float>(ix) * kTileSize;
      const float z0 = -kExtent + static_cast<float>(iz) * kTileSize;
      const float x1 = x0 + kTileSize;
      const float z1 = z0 + kTileSize;

      glVertex3f(x0, y, z0);
      glVertex3f(x1, y, z0);
      glVertex3f(x1, y, z1);
      glVertex3f(x0, y, z1);
    }
  }
  glEnd();
}

void drawSphere(const Vec3 &center, float radius,
                const std::array<float, 3> &color) {
  glEnable(GL_LIGHTING);
  setColor(color);

  glPushMatrix();
  glTranslatef(center.x, center.y, center.z);

  constexpr int kLatSegments = 32;
  constexpr int kLonSegments = 64;
  for (int lat = 0; lat < kLatSegments; ++lat) {
    const float v0 = static_cast<float>(lat) / static_cast<float>(kLatSegments);
    const float v1 =
        static_cast<float>(lat + 1) / static_cast<float>(kLatSegments);
    const float theta0 = v0 * kPi;
    const float theta1 = v1 * kPi;

    glBegin(GL_QUAD_STRIP);
    for (int lon = 0; lon <= kLonSegments; ++lon) {
      const float u =
          static_cast<float>(lon) / static_cast<float>(kLonSegments);
      const float phi = u * 2.0f * kPi;

      const float sinTheta0 = std::sin(theta0);
      const float cosTheta0 = std::cos(theta0);
      const float sinTheta1 = std::sin(theta1);
      const float cosTheta1 = std::cos(theta1);
      const float cosPhi = std::cos(phi);
      const float sinPhi = std::sin(phi);

      const float x0 = sinTheta0 * cosPhi;
      const float y0 = cosTheta0;
      const float z0 = sinTheta0 * sinPhi;

      const float x1 = sinTheta1 * cosPhi;
      const float y1 = cosTheta1;
      const float z1 = sinTheta1 * sinPhi;

      glNormal3f(x0, y0, z0);
      glVertex3f(x0 * radius, y0 * radius, z0 * radius);

      glNormal3f(x1, y1, z1);
      glVertex3f(x1 * radius, y1 * radius, z1 * radius);
    }
    glEnd();
  }

  glPopMatrix();
}

void drawCloth(const Cloth &cloth, const AppState &app) {
  const std::vector<Vec3> &positions = cloth.positions();
  const std::vector<Vec2> &uvs = cloth.uvs();
  const std::vector<Vec3> &normals = cloth.normals();
  const std::vector<unsigned int> &triangles = cloth.triangleIndices();
  const std::vector<unsigned int> &lines = cloth.lineIndices();
  if (positions.empty()) {
    return;
  }

  if (app.drawSurface && !triangles.empty()) {
    glEnable(GL_LIGHTING);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);
    setColor(app.clothColor);

    std::vector<Vec3> vertexColors;
    bool usingColors = false;

    if (app.showDomains && cloth.domainCount() > 1) {
      vertexColors.assign(
          positions.size(),
          Vec3{app.clothColor[0], app.clothColor[1], app.clothColor[2]});
      const std::vector<Spring> &springs = cloth.springs();
      const std::vector<int> &springDomains = cloth.springDomains();

      constexpr std::array<std::array<float, 3>, 8> domainPalette{{
          {0.96f, 0.28f, 0.22f},
          {0.17f, 0.72f, 0.95f},
          {0.10f, 0.86f, 0.39f},
          {0.98f, 0.74f, 0.18f},
          {0.84f, 0.44f, 0.96f},
          {0.30f, 0.94f, 0.86f},
          {0.98f, 0.56f, 0.30f},
          {0.58f, 0.64f, 0.98f},
      }};

      if (springs.size() == springDomains.size()) {
        for (std::size_t i = 0; i < springs.size(); ++i) {
          const int domain = springDomains[i];
          if (domain >= 0) {
            const auto &c = domainPalette[static_cast<std::size_t>(domain) %
                                          domainPalette.size()];
            vertexColors[static_cast<std::size_t>(springs[i].a)] =
                Vec3{c[0], c[1], c[2]};
            vertexColors[static_cast<std::size_t>(springs[i].b)] =
                Vec3{c[0], c[1], c[2]};
          }
        }
        usingColors = true;
      }
    }

    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(Vec3), positions.data());

    if (!normals.empty()) {
      glEnableClientState(GL_NORMAL_ARRAY);
      glNormalPointer(GL_FLOAT, sizeof(Vec3), normals.data());
    }

    bool textured = false;
    if (!usingColors && ensureDiffuseTextureReady()) {
      glEnable(GL_TEXTURE_2D);
      glBindTexture(GL_TEXTURE_2D, diffuseTextureCache().textureId);
      if (!uvs.empty()) {
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, sizeof(Vec2), uvs.data());
        textured = true;
      }
      glColor3f(1.0f, 1.0f, 1.0f);
    } else if (usingColors) {
      glEnable(GL_COLOR_MATERIAL);
      glEnableClientState(GL_COLOR_ARRAY);
      glColorPointer(3, GL_FLOAT, sizeof(Vec3), vertexColors.data());
    }

    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(triangles.size()),
                   GL_UNSIGNED_INT, triangles.data());

    if (textured) {
      glDisableClientState(GL_TEXTURE_COORD_ARRAY);
      glBindTexture(GL_TEXTURE_2D, 0U);
      glDisable(GL_TEXTURE_2D);
    }
    if (usingColors) {
      glDisableClientState(GL_COLOR_ARRAY);
    }
    if (!normals.empty()) {
      glDisableClientState(GL_NORMAL_ARRAY);
    }
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisable(GL_POLYGON_OFFSET_FILL);
  }

  if (app.drawWireframe && !lines.empty()) {
    glDisable(GL_LIGHTING);
    setColor(app.wireColor);
    glLineWidth(1.0f);

    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(Vec3), positions.data());
    glDrawElements(GL_LINES, static_cast<GLsizei>(lines.size()),
                   GL_UNSIGNED_INT, lines.data());
    glDisableClientState(GL_VERTEX_ARRAY);
  }
}

void drawPinnedPoints(const Cloth &cloth, const AppState &app) {
  if (!app.showPinnedPoints) {
    return;
  }

  const std::vector<Vec3> &positions = cloth.positions();
  const std::vector<bool> &pinned = cloth.pinnedMask();
  if (positions.size() != pinned.size()) {
    return;
  }

  glDisable(GL_LIGHTING);
  glPointSize(app.debugPointSize);
  setColor(app.pinnedColor);
  glBegin(GL_POINTS);
  for (std::size_t i = 0; i < positions.size(); ++i) {
    if (!pinned[i]) {
      continue;
    }
    const Vec3 &point = positions[i];
    glVertex3f(point.x, point.y, point.z);
  }
  glEnd();
}

// ---------------------------------------------------------------------------
// Wind particles — lightweight streaks that visualise the current wind field.
// ---------------------------------------------------------------------------

struct WindParticle {
  Vec3 pos;
  Vec3 vel;
  float life;    // remaining lifetime (seconds)
  float maxLife; // total lifetime (for alpha fade)
};

struct WindParticleState {
  std::vector<WindParticle> particles;
  bool initialised = false;
};

WindParticleState &windParticles() {
  static WindParticleState state;
  return state;
}

void updateAndDrawWindParticles(const AppState &app, float dt) {
  if (app.windStrength < 0.01f) {
    windParticles().particles.clear();
    return;
  }

  WindParticleState &state = windParticles();

  // Spawn region centred around the cloth.
  constexpr float kSpawnExtent = 3.5f;
  constexpr float kSpawnYLow = -2.5f;
  constexpr float kSpawnYHigh = 2.5f;
  constexpr int kMaxParticles = 900;
  constexpr float kParticleLifetime = 2.4f;

  const Vec3 &wind = app.lastWindDirection;
  const float windLen = length(wind);
  const Vec3 windDir =
      windLen > 1e-6f ? wind / windLen : Vec3{1.0f, 0.0f, 0.0f};
  const float speed = app.windStrength * 0.35f;

  // --- spawn new particles ---
  const int spawnCount =
      std::min(14, kMaxParticles - static_cast<int>(state.particles.size()));
  for (int i = 0; i < spawnCount; ++i) {
    WindParticle p;
    const float rx =
        (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX)) *
            2.0f -
        1.0f;
    const float ry =
        (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX));
    const float rz =
        (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX)) *
            2.0f -
        1.0f;
    // Spawn particles upwind so they blow through the scene.
    p.pos = Vec3{rx * kSpawnExtent - windDir.x * kSpawnExtent,
                 kSpawnYLow + ry * (kSpawnYHigh - kSpawnYLow),
                 rz * kSpawnExtent - windDir.z * kSpawnExtent};
    // Small random jitter on top of the main wind velocity.
    const float jx =
        (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) -
         0.5f) *
        0.4f;
    const float jy =
        (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) -
         0.5f) *
        0.15f;
    const float jz =
        (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) -
         0.5f) *
        0.4f;
    p.vel = Vec3{windDir.x * speed + jx, windDir.y * speed + jy,
                 windDir.z * speed + jz};
    p.life = kParticleLifetime *
             (0.6f + 0.4f * static_cast<float>(std::rand()) /
                         static_cast<float>(RAND_MAX));
    p.maxLife = p.life;
    state.particles.push_back(p);
  }

  // --- update & cull ---
  for (auto it = state.particles.begin(); it != state.particles.end();) {
    it->pos.x += it->vel.x * dt;
    it->pos.y += it->vel.y * dt;
    it->pos.z += it->vel.z * dt;
    it->life -= dt;
    if (it->life <= 0.0f) {
      it = state.particles.erase(it);
    } else {
      ++it;
    }
  }

  if (state.particles.empty()) {
    return;
  }

  // --- draw as short streaks ---
  const GLboolean lightingWas = glIsEnabled(GL_LIGHTING);
  glDisable(GL_LIGHTING);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glLineWidth(2.5f);

  glBegin(GL_LINES);
  for (const WindParticle &p : state.particles) {
    const float t = p.life / p.maxLife; // 1 at spawn → 0 at death
    const float alpha = t * 0.55f;
    glColor4f(0.85f, 0.92f, 1.0f, alpha);
    glVertex3f(p.pos.x, p.pos.y, p.pos.z);
    // Trail end: a short distance behind.
    const float trailLen = 0.25f + (1.0f - t) * 0.15f;
    glColor4f(0.85f, 0.92f, 1.0f, 0.0f);
    glVertex3f(p.pos.x - p.vel.x * trailLen,
               p.pos.y - p.vel.y * trailLen,
               p.pos.z - p.vel.z * trailLen);
  }
  glEnd();

  glDisable(GL_BLEND);
  if (lightingWas == GL_TRUE) {
    glEnable(GL_LIGHTING);
  }
}

void drawDebugVisualizations(const Cloth &cloth, const AppState &app) {
  drawPinnedPoints(cloth, app);
}

} // namespace

void renderFrame(const AppState &app, int framebufferWidth,
                 int framebufferHeight) {
  glViewport(0, 0, framebufferWidth, framebufferHeight);

  glClearColor(app.clearColor[0], app.clearColor[1], app.clearColor[2], 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  const float aspect = static_cast<float>(framebufferWidth) /
                       static_cast<float>(std::max(1, framebufferHeight));
  setPerspective(48.0f, aspect, 0.1f, 50.0f);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glTranslatef(0.0f, -0.1f, -app.distance);
  glRotatef(app.pitch, 1.0f, 0.0f, 0.0f);
  glRotatef(app.yaw, 0.0f, 1.0f, 0.0f);

  drawHdriSkyDome(app);

  // Apply target offset after sky dome (panning / WASD movement)
  glTranslatef(-app.targetX, -app.targetY, -app.targetZ);

  const GLfloat lightPosition[] = {2.5f, 4.0f, 3.0f, 1.0f};
  glLightfv(GL_LIGHT0, GL_POSITION, lightPosition);

  if (app.drawGround) {
    drawGroundPlane(app.cloth.floorY(), app.groundColor);
  }
  if (app.cloth.sphereEnabled()) {
    drawSphere(app.cloth.sphereCenter(), app.cloth.sphereRadius(),
               app.groundColor);
  }

  drawCloth(app.cloth, app);
  drawDebugVisualizations(app.cloth, app);

  // Wind particles (use a rough dt from the smoothed FPS).
  const float particleDt =
      app.smoothedFps > 1.0f ? 1.0f / app.smoothedFps : 1.0f / 60.0f;
  updateAndDrawWindParticles(app, particleDt);
}

} // namespace clothdd

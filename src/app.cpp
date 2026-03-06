/// @file app.cpp
/// @brief Preset configurations, OpenGL setup, simulation loop, and camera controls.

#include "app.hpp"

#include "imgui.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace clothdd {
void applyPreset(AppState &app, ScenePreset preset) {
  app.scenePreset = preset;
  app.paused = false;
  app.requestStep = false;

  switch (preset) {
  case ScenePreset::Baseline:
    app.cloth = Cloth(56, 40, 0.06f);
    app.solverIterations = 8;
    app.substeps = 5;
    app.gravity = -9.81f;
    app.windStrength = 2.5f;
    app.windFrequencyX = 0.35f;
    app.windFrequencyZ = 0.25f;
    app.windVerticalBias = 0.10f;
    app.domainLocalSolveEnabled = true;
    app.squareDomainDecomposition = false;
    app.domainCount = 12;
    app.springStiffness = 1.0f;
    app.clothDamping = 0.995f;
    app.drawSurface = true;
    app.drawWireframe = true;
    app.drawGround = false;
    app.showDomains = true;
    app.showPinnedPoints = true;
    app.hdriBackgroundEnabled = true;
    app.clearColor = {0.08f, 0.10f, 0.12f};
    app.clothColor = {0.84f, 0.33f, 0.24f};
    app.wireColor = {0.08f, 0.08f, 0.10f};
    app.groundColor = {0.30f, 0.27f, 0.24f};
    app.pinnedColor = {0.16f, 0.68f, 0.96f};
    app.debugPointSize = 4.0f;
    app.debugLineWidth = 1.2f;
    app.yaw = 30.0f;
    app.pitch = -25.0f;
    app.distance = 5.2f;
    app.cloth.setFloorEnabled(true);
    app.cloth.setFloorY(-2.0f);
    break;
  case ScenePreset::DenseShowcase:
    app.cloth = Cloth(120, 88, 0.035f);
    app.solverIterations = 10;
    app.substeps = 5;
    app.gravity = -9.81f;
    app.windStrength = 8.5f;
    app.windFrequencyX = 0.60f;
    app.windFrequencyZ = 0.42f;
    app.windVerticalBias = 0.20f;
    app.domainLocalSolveEnabled = true;
    app.squareDomainDecomposition = false;
    app.domainCount = 28;
    app.springStiffness = 1.0f;
    app.clothDamping = 0.995f;
    app.drawSurface = true;
    app.drawWireframe = true;
    app.drawGround = false;
    app.showDomains = true;
    app.showPinnedPoints = false;
    app.hdriBackgroundEnabled = true;
    app.clearColor = {0.04f, 0.06f, 0.09f};
    app.clothColor = {0.74f, 0.46f, 0.18f};
    app.wireColor = {0.02f, 0.02f, 0.03f};
    app.groundColor = {0.20f, 0.20f, 0.24f};
    app.pinnedColor = {0.95f, 0.70f, 0.18f};
    app.debugPointSize = 3.0f;
    app.debugLineWidth = 1.35f;
    app.yaw = 22.0f;
    app.pitch = -32.0f;
    app.distance = 9.0f;
    app.cloth.setFloorEnabled(true);
    app.cloth.setFloorY(-2.6f);
    break;
  case ScenePreset::BallDrop:
    app.cloth = Cloth(80, 80, 0.045f);
    app.solverIterations = 16;
    app.substeps = 5;
    app.gravity = -9.81f;
    app.windStrength = 0.0f;
    app.windFrequencyX = 0.0f;
    app.windFrequencyZ = 0.0f;
    app.windVerticalBias = 0.0f;
    app.domainLocalSolveEnabled = true;
    app.squareDomainDecomposition = false;
    app.domainCount = 16;
    app.springStiffness = 1.0f;
    app.clothDamping = 0.995f;
    app.drawSurface = true;
    app.drawWireframe = true;
    app.drawGround = false;
    app.showDomains = true;
    app.showPinnedPoints = false;
    app.hdriBackgroundEnabled = true;
    app.clearColor = {0.03f, 0.05f, 0.08f};
    app.clothColor = {0.88f, 0.80f, 0.26f};
    app.wireColor = {0.02f, 0.02f, 0.02f};
    app.groundColor = {0.16f, 0.19f, 0.22f};
    app.pinnedColor = {0.20f, 0.92f, 0.95f};
    app.debugPointSize = 2.0f;
    app.debugLineWidth = 1.15f;
    app.yaw = 18.0f;
    app.pitch = -21.0f;
    app.distance = 7.6f;
    app.cloth.setFloorEnabled(true);
    app.cloth.setFloorY(-2.4f);
    app.cloth.setSphere(Vec3{0.0f, 0.3f, 2.5f}, 1.25f);
    app.cloth.setSphereEnabled(true);
    break;
  case ScenePreset::UltraDense:
    app.cloth = Cloth(240, 176, 0.0175f);
    app.solverIterations = 14;
    app.substeps = 6;
    app.gravity = -9.81f;
    app.windStrength = 10.0f;
    app.windFrequencyX = 0.50f;
    app.windFrequencyZ = 0.38f;
    app.windVerticalBias = 0.15f;
    app.domainLocalSolveEnabled = true;
    app.squareDomainDecomposition = true;
    app.domainCount = 48;
    app.springStiffness = 1.0f;
    app.clothDamping = 0.995f;
    app.drawSurface = true;
    app.drawWireframe = false;
    app.drawGround = false;
    app.showDomains = false;
    app.showPinnedPoints = false;
    app.hdriBackgroundEnabled = true;
    app.clearColor = {0.02f, 0.03f, 0.06f};
    app.clothColor = {0.60f, 0.20f, 0.55f};
    app.wireColor = {0.01f, 0.01f, 0.02f};
    app.groundColor = {0.14f, 0.14f, 0.18f};
    app.pinnedColor = {0.90f, 0.50f, 0.90f};
    app.debugPointSize = 2.0f;
    app.debugLineWidth = 1.0f;
    app.yaw = 15.0f;
    app.pitch = -28.0f;
    app.distance = 9.5f;
    app.cloth.setFloorEnabled(true);
    app.cloth.setFloorY(-2.8f);
    break;
  }

  app.lastWindDirection = Vec3{1.0f, 0.0f, 0.0f};
  app.targetX = 0.0f;
  app.targetY = 0.0f;
  app.targetZ = 0.0f;
  app.simulationTime = 0.0;
  app.cloth.setDomainLocalSolveEnabled(app.domainLocalSolveEnabled);
  app.cloth.setSquareDomainDecompositionEnabled(app.squareDomainDecomposition);
  app.cloth.setDomainCount(app.domainCount);
  app.cloth.setSpringStiffness(app.springStiffness);
  app.cloth.setDamping(app.clothDamping);
  app.cloth.reset();
}

void applyBaseline(AppState &app) { applyPreset(app, ScenePreset::Baseline); }

void applyDenseShowcase(AppState &app) {
  applyPreset(app, ScenePreset::DenseShowcase);
}

void configureOpenGLState() {
  glEnable(GL_NORMALIZE);
  glEnable(GL_COLOR_MATERIAL);
  glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
  glShadeModel(GL_SMOOTH);

  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);

  glEnable(GL_LIGHT0);
  const GLfloat ambient[] = {0.24f, 0.24f, 0.24f, 1.0f};
  const GLfloat diffuse[] = {0.90f, 0.90f, 0.90f, 1.0f};
  const GLfloat specular[] = {0.30f, 0.30f, 0.30f, 1.0f};
  glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);
  glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
  glLightfv(GL_LIGHT0, GL_SPECULAR, specular);
}

void printControls() {
  std::printf("\n  ClothDD — Controls\n");
  std::printf("  ─────────────────────────────────\n");
  std::printf("  TAB             Toggle UI panel\n");
  std::printf("  LMB drag        Orbit camera\n");
  std::printf("  Ctrl+LMB / MMB  Pan camera\n");
  std::printf("  WASD            Move camera target\n");
  std::printf("  Q / E           Camera up / down\n");
  std::printf("  Scroll          Zoom\n");
  std::printf("  Space           Pause / resume\n");
  std::printf("  .               Single-step\n");
  std::printf("  1               Baseline preset\n");
  std::printf("  2               Dense Showcase preset\n");
  std::printf("  3               Ball Drop preset\n");
  std::printf("  4               Ultra Dense preset\n");
  std::printf("  Esc             Quit\n\n");
}

void updateSmoothedFps(AppState &app, float dt) {
  const float instantFps = 1.0f / std::max(dt, 1e-6f);
  if (app.smoothedFps <= 0.0f) {
    app.smoothedFps = instantFps;
  } else {
    app.smoothedFps = app.smoothedFps * 0.90f + instantFps * 0.10f;
  }
  app.fpsHistory[app.fpsHistoryOffset] = app.smoothedFps;
  app.fpsHistoryOffset = (app.fpsHistoryOffset + 1) % AppState::kFpsHistorySize;
}

void stepSimulation(AppState &app, double now, float dt) {
  if (app.paused && !app.requestStep) {
    return;
  }

  app.simulationTime += static_cast<double>(dt);

  // Animate sphere for BallDrop preset (pendulum swing through cloth)
  if (app.scenePreset == ScenePreset::BallDrop && app.cloth.sphereEnabled()) {
    const float t = static_cast<float>(app.simulationTime);
    const float z = 2.5f * std::cos(t * 0.8f);
    app.cloth.setSphere(Vec3{0.0f, 0.3f, z}, app.cloth.sphereRadius());
  }

  const int substeps = std::max(1, app.substeps);
  const float stepDt = dt / static_cast<float>(substeps);
  for (int i = 0; i < substeps; ++i) {
    const float t = static_cast<float>(now + static_cast<double>(i) * stepDt);
    const Vec3 windDirection{std::sin(t * app.windFrequencyX),
                             app.windVerticalBias,
                             std::cos(t * app.windFrequencyZ)};
    app.lastWindDirection = windDirection;

    app.cloth.simulate(stepDt, std::max(1, app.solverIterations),
                       Vec3{0.0f, app.gravity, 0.0f}, windDirection,
                       app.windStrength);
  }

  app.requestStep = false;
}

void pollCameraKeys(GLFWwindow *window, AppState &app, float dt) {
  if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureKeyboard) {
    return;
  }

  constexpr float kPi = 3.14159265358979323846f;
  const float moveSpeed = 3.0f * dt;
  const float yawRad = app.yaw * kPi / 180.0f;
  const float forwardX = -std::sin(yawRad);
  const float forwardZ = -std::cos(yawRad);
  const float rightX = std::cos(yawRad);
  const float rightZ = -std::sin(yawRad);

  if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
    app.targetX += forwardX * moveSpeed;
    app.targetZ += forwardZ * moveSpeed;
  }
  if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
    app.targetX -= forwardX * moveSpeed;
    app.targetZ -= forwardZ * moveSpeed;
  }
  if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
    app.targetX -= rightX * moveSpeed;
    app.targetZ -= rightZ * moveSpeed;
  }
  if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
    app.targetX += rightX * moveSpeed;
    app.targetZ += rightZ * moveSpeed;
  }
  if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
    app.targetY -= moveSpeed;
  }
  if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
    app.targetY += moveSpeed;
  }
}

} // namespace clothdd

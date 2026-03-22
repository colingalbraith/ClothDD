/// @file app.hpp
/// @brief Application state, scene presets, and simulation stepping.

#pragma once

#include "cloth.hpp"
#include "gpu_solver.hpp"

#ifdef CLOTHDD_HAVE_GLAD
#include <glad/glad.h>
#endif
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#if !defined(CLOTHDD_HAVE_GLAD)
#if defined(_WIN32)
#include <windows.h>
#endif
#include <GL/gl.h>
#endif

#include <array>
#include <string>

namespace clothdd {

enum class ScenePreset {
  Baseline = 0,
  DenseShowcase = 1,
  BallDrop = 2,
  UltraDense = 3,
  ExtremeDense = 4,
};

struct AppState {
  Cloth cloth{56, 40, 0.06f};
  ScenePreset scenePreset = ScenePreset::Baseline;

  bool paused = false;
  bool requestStep = false;

  int solverIterations = 8;
  int substeps = 2;
  float gravity = -9.81f;
  float windStrength = 2.5f;
  float windFrequencyX = 0.35f;
  float windFrequencyZ = 0.25f;
  float windVerticalBias = 0.10f;
  bool domainLocalSolveEnabled = true;
  bool squareDomainDecomposition = false;
  int domainCount = 12;
  float structuralCompliance = 1e-7f;
  float shearCompliance = 1e-5f;
  float bendCompliance = 1e-3f;
  float clothDamping = 0.995f;
  Vec3 lastWindDirection{1.0f, 0.0f, 0.0f};

  bool drawSurface = true;
  bool drawWireframe = true;
  bool drawGround = true;
  bool showDomains = true;
  bool showPinnedPoints = true;
  bool hdriBackgroundEnabled = true;
  std::string hdriPath = "../kloofendal_48d_partly_cloudy_puresky_4k.exr";

  std::array<float, 3> clearColor{0.08f, 0.10f, 0.12f};
  std::array<float, 3> clothColor{0.84f, 0.33f, 0.24f};
  std::array<float, 3> wireColor{0.08f, 0.08f, 0.10f};
  std::array<float, 3> groundColor{0.30f, 0.27f, 0.24f};
  std::array<float, 3> pinnedColor{0.16f, 0.68f, 0.96f};

  float debugPointSize = 4.0f;
  float debugLineWidth = 1.2f;

  float yaw = 30.0f;
  float pitch = -25.0f;
  float distance = 5.2f;
  bool rotatingCamera = false;
  bool panningCamera = false;
  double lastCursorX = 0.0;
  double lastCursorY = 0.0;

  float targetX = 0.0f;
  float targetY = 0.0f;
  float targetZ = 0.0f;
  double simulationTime = 0.0;

  bool showUi = true;
  bool showImGuiDemo = false;
  bool showDebugInfo = false;
  bool msaaEnabled = true;
  bool gpuSolverEnabled = false;
  GpuSolver gpuSolver;

  // Timing (milliseconds).
  float simTimeMs = 0.0f;
  float renderTimeMs = 0.0f;
  float frameTimeMs = 0.0f;
  float smoothedFps = 0.0f;

  static constexpr int kFpsHistorySize = 128;
  float fpsHistory[kFpsHistorySize] = {};
  int fpsHistoryOffset = 0;
};

void applyPreset(AppState &app, ScenePreset preset);
void applyBaseline(AppState &app);
void applyDenseShowcase(AppState &app);
void configureOpenGLState();
void printControls();
void updateSmoothedFps(AppState &app, float dt);
void stepSimulation(AppState &app, double now, float dt);
void pollCameraKeys(GLFWwindow *window, AppState &app, float dt);

} // namespace clothdd

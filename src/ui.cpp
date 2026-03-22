/// @file ui.cpp
/// @brief ImGui panel, keyboard/mouse callbacks, and camera input handling.

#include "ui.hpp"

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl2.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE 0x809D
#endif

namespace clothdd {
namespace {

bool imguiWantsMouse() {
  if (ImGui::GetCurrentContext() == nullptr) {
    return false;
  }
  return ImGui::GetIO().WantCaptureMouse;
}

bool imguiWantsKeyboard() {
  if (ImGui::GetCurrentContext() == nullptr) {
    return false;
  }
  return ImGui::GetIO().WantCaptureKeyboard;
}

AppState *appFromWindow(GLFWwindow *window) {
  return static_cast<AppState *>(glfwGetWindowUserPointer(window));
}

} // namespace

void initImGui(GLFWwindow *window) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  ImGui_ImplGlfw_InitForOpenGL(window, false);
  ImGui_ImplOpenGL2_Init();
}

void beginImGuiFrame() {
  ImGui_ImplOpenGL2_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
}

void drawImGuiPanel(AppState &app, float fps) {
  if (!app.showUi) {
    return;
  }

  ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(360.0f, 460.0f), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("ClothDD", &app.showUi)) {
    ImGui::End();
    return;
  }

  // Auto-scaled FPS graph: compute min/max from history with padding
  float fpsMin = 1e9f;
  float fpsMax = -1e9f;
  for (int i = 0; i < AppState::kFpsHistorySize; ++i) {
    const float v = app.fpsHistory[i];
    if (v > 0.0f) {
      if (v < fpsMin) fpsMin = v;
      if (v > fpsMax) fpsMax = v;
    }
  }
  if (fpsMin >= fpsMax) {
    fpsMin = 0.0f;
    fpsMax = 120.0f;
  }
  // Always show from 0 so the graph reflects overall FPS magnitude / trend
  // rather than zooming into micro-fluctuations.
  fpsMin = 0.0f;
  fpsMax = std::max(fpsMax * 1.1f, 120.0f);

  char fpsOverlay[64];
  std::snprintf(fpsOverlay, sizeof(fpsOverlay), "%.1f FPS  (0-%.0f)", fps,
                fpsMax);
  ImGui::Text("FPS: %.1f", fps);
  ImGui::PlotLines("##fpsgraph", app.fpsHistory, AppState::kFpsHistorySize,
                   app.fpsHistoryOffset, fpsOverlay, fpsMin, fpsMax,
                   ImVec2(0.0f, 50.0f));
  ImGui::Text("Particles: %zu | Springs: %zu", app.cloth.positions().size(),
              app.cloth.springs().size());

  int presetIndex = static_cast<int>(app.scenePreset);
  const char *presetLabels[] = {"Baseline", "Dense Showcase", "Ball Drop",
                                "Ultra Dense", "Extreme Dense"};
  if (ImGui::Combo("Scene Preset", &presetIndex, presetLabels,
                   IM_ARRAYSIZE(presetLabels))) {
    applyPreset(app, static_cast<ScenePreset>(presetIndex));
  }

  if (ImGui::Button(app.paused ? "Resume" : "Pause")) {
    app.paused = !app.paused;
  }
  ImGui::SameLine();
  if (ImGui::Button("Step")) {
    app.requestStep = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset Scene")) {
    app.cloth.reset();
  }
  ImGui::SameLine();
  if (ImGui::Button("Drop Cloth")) {
    app.cloth.unpinAll();
  }

  ImGui::SeparatorText("Simulation");
  ImGui::SliderInt("Substeps", &app.substeps, 1, 8);
  ImGui::SliderInt("Solver Iterations", &app.solverIterations, 1, 32);
  ImGui::SliderFloat("Gravity", &app.gravity, -30.0f, 2.0f, "%.2f");
  ImGui::SliderFloat("Wind Strength", &app.windStrength, 0.0f, 30.0f, "%.2f");
  ImGui::SliderFloat("Wind Freq X", &app.windFrequencyX, 0.0f, 2.5f, "%.2f");
  ImGui::SliderFloat("Wind Freq Z", &app.windFrequencyZ, 0.0f, 2.5f, "%.2f");
  ImGui::SliderFloat("Wind Vertical", &app.windVerticalBias, -1.0f, 1.0f,
                     "%.2f");

  ImGui::SeparatorText("Material (XPBD)");
  ImGui::TextDisabled("Lower compliance = stiffer");
  if (ImGui::SliderFloat("Structural", &app.structuralCompliance,
                         1e-9f, 1e-3f, "%.2e",
                         ImGuiSliderFlags_Logarithmic)) {
    app.cloth.setStructuralCompliance(app.structuralCompliance);
  }
  if (ImGui::SliderFloat("Shear", &app.shearCompliance,
                         1e-7f, 1e-2f, "%.2e",
                         ImGuiSliderFlags_Logarithmic)) {
    app.cloth.setShearCompliance(app.shearCompliance);
  }
  if (ImGui::SliderFloat("Bend", &app.bendCompliance,
                         1e-5f, 1e+0f, "%.2e",
                         ImGuiSliderFlags_Logarithmic)) {
    app.cloth.setBendCompliance(app.bendCompliance);
  }
  if (ImGui::SliderFloat("Damping", &app.clothDamping, 0.9f, 1.0f,
                         "%.4f")) {
    app.cloth.setDamping(app.clothDamping);
  }

  ImGui::SeparatorText("Domain Decomposition");
  if (ImGui::Checkbox("Enable DD", &app.domainLocalSolveEnabled)) {
    app.cloth.setDomainLocalSolveEnabled(app.domainLocalSolveEnabled);
  }
  if (app.domainLocalSolveEnabled) {
    if (app.gpuSolver.available()) {
      if (ImGui::Checkbox("GPU Accelerated", &app.gpuSolverEnabled)) {
        if (app.gpuSolverEnabled) {
          app.gpuSolver.upload(app.cloth);
        }
      }
      ImGui::SameLine();
      ImGui::TextDisabled("(graph-coloring compute)");
    }
    if (!app.gpuSolverEnabled) {
      ImGui::TextDisabled("CPU threaded solver:");
      if (ImGui::Checkbox("Square Domains", &app.squareDomainDecomposition)) {
        app.cloth.setSquareDomainDecompositionEnabled(
            app.squareDomainDecomposition);
      }
      if (ImGui::SliderInt("Domain Count", &app.domainCount, 1, 32)) {
        app.cloth.setDomainCount(app.domainCount);
      }
    }
  }

  ImGui::SeparatorText("Render");
  ImGui::Checkbox("Surface", &app.drawSurface);
  ImGui::Checkbox("Wireframe", &app.drawWireframe);
  ImGui::Checkbox("Ground", &app.drawGround);
  ImGui::Checkbox("Domain Overlay", &app.showDomains);
  ImGui::Checkbox("Pinned Points", &app.showPinnedPoints);
  ImGui::Checkbox("HDRI Background", &app.hdriBackgroundEnabled);
  if (ImGui::Checkbox("MSAA (2x)", &app.msaaEnabled)) {
    if (app.msaaEnabled) {
      glEnable(GL_MULTISAMPLE);
    } else {
      glDisable(GL_MULTISAMPLE);
    }
  }
  ImGui::TextUnformatted(app.hdriPath.c_str());

  ImGui::SliderFloat("Point Size", &app.debugPointSize, 1.0f, 10.0f, "%.1f");
  ImGui::SliderFloat("Line Width", &app.debugLineWidth, 0.5f, 4.0f, "%.2f");

  ImGui::Checkbox("Show ImGui Demo", &app.showImGuiDemo);
  ImGui::Checkbox("Debug Info", &app.showDebugInfo);
  ImGui::End();

  if (app.showDebugInfo) {
    ImGui::SetNextWindowPos(ImVec2(380.0f, 12.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Debug Info", &app.showDebugInfo)) {

      ImGui::SeparatorText("Timing");
      ImGui::Text("Frame:      %.2f ms (%.0f FPS)", app.frameTimeMs,
                  app.frameTimeMs > 0.01f ? 1000.0f / app.frameTimeMs : 0.0f);
      ImGui::Text("Simulation: %.2f ms", app.simTimeMs);
      ImGui::Text("Render:     %.2f ms", app.renderTimeMs);
      const float otherMs =
          app.frameTimeMs - app.simTimeMs - app.renderTimeMs;
      ImGui::Text("Other:      %.2f ms", std::max(0.0f, otherMs));

      // Sim breakdown bar.
      if (app.frameTimeMs > 0.01f) {
        const float simFrac = app.simTimeMs / app.frameTimeMs;
        const float renFrac = app.renderTimeMs / app.frameTimeMs;
        ImGui::Text("Sim %.0f%% | Render %.0f%% | Other %.0f%%",
                    simFrac * 100.0f, renFrac * 100.0f,
                    std::max(0.0f, (1.0f - simFrac - renFrac) * 100.0f));
        const float barW = ImGui::GetContentRegionAvail().x;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + barW * simFrac, p.y + 14),
                          IM_COL32(80, 180, 255, 255));
        dl->AddRectFilled(ImVec2(p.x + barW * simFrac, p.y),
                          ImVec2(p.x + barW * (simFrac + renFrac), p.y + 14),
                          IM_COL32(255, 160, 80, 255));
        dl->AddRectFilled(
            ImVec2(p.x + barW * (simFrac + renFrac), p.y),
            ImVec2(p.x + barW, p.y + 14), IM_COL32(100, 100, 100, 255));
        ImGui::Dummy(ImVec2(barW, 18.0f));
      }

      ImGui::SeparatorText("Solver");
      const char *solverName = "CPU Sequential (no DD)";
      if (app.domainLocalSolveEnabled && app.gpuSolverEnabled &&
          app.gpuSolver.available())
        solverName = "DD + GPU Compute (graph-coloring)";
      else if (app.domainLocalSolveEnabled)
        solverName = "DD + CPU Threaded (strip/square)";
      ImGui::Text("Active: %s", solverName);
      ImGui::Text("Substeps: %d | Iterations: %d", app.substeps,
                  app.solverIterations);
      ImGui::Text("Total solves/frame: %d",
                  app.substeps * app.solverIterations);

      ImGui::SeparatorText("Mesh");
      ImGui::Text("Grid: %d x %d (%zu particles)", app.cloth.width(),
                  app.cloth.height(), app.cloth.positions().size());
      ImGui::Text("Springs: %zu", app.cloth.springs().size());
      // Count by type.
      int nStruct = 0, nShear = 0, nBend = 0;
      for (const auto &s : app.cloth.springs()) {
        switch (s.type) {
        case SpringType::Structural: ++nStruct; break;
        case SpringType::Shear:      ++nShear;  break;
        case SpringType::Bend:       ++nBend;   break;
        }
      }
      ImGui::Text("  Structural: %d | Shear: %d | Bend: %d", nStruct,
                  nShear, nBend);
      ImGui::Text("Triangles: %zu",
                  app.cloth.triangleIndices().size() / 3);

      ImGui::SeparatorText("Hardware");
#ifdef CLOTHDD_HAVE_GLAD
      ImGui::Text("GL: %s", glGetString(GL_VERSION));
      ImGui::Text("GPU: %s", glGetString(GL_RENDERER));
      ImGui::Text("Vendor: %s", glGetString(GL_VENDOR));
#else
      ImGui::Text("GL: 2.1 (legacy, no GLAD)");
#endif
      ImGui::Text("CPU threads: %d", app.cloth.workerThreadCount());
      ImGui::Text("GPU compute: %s",
                  app.gpuSolver.available() ? "available" : "unavailable");

      ImGui::SeparatorText("XPBD Compliance");
      ImGui::Text("Structural: %.2e", app.structuralCompliance);
      ImGui::Text("Shear:      %.2e", app.shearCompliance);
      ImGui::Text("Bend:       %.2e", app.bendCompliance);
      ImGui::Text("Damping:    %.4f", app.clothDamping);

      if (app.domainLocalSolveEnabled && !app.gpuSolverEnabled) {
        ImGui::SeparatorText("Domain Decomposition");
        ImGui::Text("Domains: %d (%s)", app.domainCount,
                    app.squareDomainDecomposition ? "square" : "strips");
      }
    }
    ImGui::End();
  }

  if (app.showImGuiDemo) {
    ImGui::ShowDemoWindow(&app.showImGuiDemo);
  }
}

void renderImGuiFrame() {
  ImGui::Render();
  ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
}

void shutdownImGui() {
  ImGui_ImplOpenGL2_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
}

void keyCallback(GLFWwindow *window, int key, int scancode, int action,
                 int mods) {
  ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);

  AppState *app = appFromWindow(window);
  if (app == nullptr) {
    return;
  }

  if (key == GLFW_KEY_TAB && action == GLFW_PRESS) {
    app->showUi = !app->showUi;
    return;
  }

  if (action != GLFW_PRESS && action != GLFW_REPEAT) {
    return;
  }

  if (imguiWantsKeyboard()) {
    return;
  }

  switch (key) {
  case GLFW_KEY_ESCAPE:
    glfwSetWindowShouldClose(window, GLFW_TRUE);
    break;
  case GLFW_KEY_P:
    app->paused = !app->paused;
    break;
  case GLFW_KEY_PERIOD:
    app->requestStep = true;
    break;
  case GLFW_KEY_1:
    applyBaseline(*app);
    break;
  case GLFW_KEY_2:
    applyDenseShowcase(*app);
    break;
  case GLFW_KEY_3:
    applyPreset(*app, ScenePreset::BallDrop);
    break;
  case GLFW_KEY_4:
    applyPreset(*app, ScenePreset::UltraDense);
    break;
  case GLFW_KEY_5:
    applyPreset(*app, ScenePreset::ExtremeDense);
    break;
  case GLFW_KEY_G:
    app->cloth.unpinAll();
    break;
  case GLFW_KEY_R:
    applyPreset(*app, app->scenePreset);
    break;
  case GLFW_KEY_CAPS_LOCK:
    app->paused = !app->paused;
    break;
  default:
    break;
  }
}

void mouseButtonCallback(GLFWwindow *window, int button, int action, int mods) {
  ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);

  AppState *app = appFromWindow(window);
  if (app == nullptr) {
    return;
  }

  if (action == GLFW_RELEASE) {
    app->rotatingCamera = false;
    app->panningCamera = false;
    return;
  }

  if (imguiWantsMouse()) {
    return;
  }

  if (action == GLFW_PRESS) {
    glfwGetCursorPos(window, &app->lastCursorX, &app->lastCursorY);

    if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
      app->panningCamera = true;
    } else if (button == GLFW_MOUSE_BUTTON_LEFT) {
      if (mods & GLFW_MOD_CONTROL) {
        app->panningCamera = true;
      } else {
        app->rotatingCamera = true;
      }
    }
  }
}

void cursorPositionCallback(GLFWwindow *window, double x, double y) {
  ImGui_ImplGlfw_CursorPosCallback(window, x, y);

  AppState *app = appFromWindow(window);
  if (app == nullptr || imguiWantsMouse()) {
    return;
  }

  const double dx = x - app->lastCursorX;
  const double dy = y - app->lastCursorY;

  if (app->rotatingCamera) {
    app->lastCursorX = x;
    app->lastCursorY = y;

    app->yaw += static_cast<float>(dx) * 0.22f;
    app->pitch += static_cast<float>(dy) * 0.22f;
    app->pitch = std::clamp(app->pitch, -85.0f, 85.0f);
  } else if (app->panningCamera) {
    app->lastCursorX = x;
    app->lastCursorY = y;

    constexpr float kPi = 3.14159265358979323846f;
    const float panScale = 0.004f * app->distance;
    const float yawRad = app->yaw * kPi / 180.0f;

    // Horizontal: move target in camera-local right direction
    app->targetX -= static_cast<float>(dx) * panScale * std::cos(yawRad);
    app->targetZ += static_cast<float>(dx) * panScale * std::sin(yawRad);

    // Vertical: move target up/down
    app->targetY += static_cast<float>(dy) * panScale;
  }
}

void scrollCallback(GLFWwindow *window, double xOffset, double yOffset) {
  ImGui_ImplGlfw_ScrollCallback(window, xOffset, yOffset);

  AppState *app = appFromWindow(window);
  if (app == nullptr || imguiWantsMouse()) {
    return;
  }

  app->distance -= static_cast<float>(yOffset) * 0.35f;
  app->distance = std::clamp(app->distance, 0.5f, 30.0f);
}

void charCallback(GLFWwindow *window, unsigned int character) {
  ImGui_ImplGlfw_CharCallback(window, character);
}

} // namespace clothdd

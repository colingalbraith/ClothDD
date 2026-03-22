/// @file gpu_solver.hpp
/// @brief GPU-accelerated cloth solver using OpenGL 4.3 compute shaders.
///
/// Uses graph coloring for constraint solving: springs are partitioned into
/// independent color groups (no two springs in the same group share a vertex).
/// Each color is dispatched as a fully-parallel compute pass with direct
/// position writes — no atomics needed, Gauss-Seidel-quality convergence.

#pragma once

#include "cloth.hpp"

#ifdef CLOTHDD_HAVE_GLAD
#include <glad/glad.h>
#endif

#include <vector>

class GpuSolver {
public:
  GpuSolver();
  ~GpuSolver();

  GpuSolver(const GpuSolver &) = delete;
  GpuSolver &operator=(const GpuSolver &) = delete;

  bool available() const { return m_available; }

  /// Upload all cloth data to GPU buffers.  Call after Cloth::reset().
  void upload(const Cloth &cloth);

  /// Run the full simulation step on the GPU, then read back positions and
  /// normals into the cloth's CPU vectors.
  void simulate(Cloth &cloth, float dt, int solverIterations,
                const Vec3 &gravity, const Vec3 &windDirection,
                float windStrength);

private:
#ifdef CLOTHDD_HAVE_GLAD
  bool initShaders();
  GLuint compileShader(const char *source);
  GLuint linkProgram(GLuint shader);

  bool m_available = false;
  bool m_uploaded = false;
  int m_particleCount = 0;
  int m_springCount = 0;
  int m_triangleCount = 0;

  // Per-color group: offset and count into the sorted springs SSBO.
  static constexpr int kMaxColors = 12;
  struct ColorGroup {
    int offset = 0;
    int count = 0;
  };
  ColorGroup m_colorGroups[kMaxColors] = {};
  int m_numColors = 0;

  enum BufferBinding : GLuint {
    kPositions = 0,
    kSprings = 1,
    kPinned = 2,
    kPrevPositions = 3,
    kInitialPositions = 4,
    kNormalsInt = 5,
    kTriangles = 6,
    kNormalsFloat = 7,
    kBufferCount = 8,
  };
  GLuint m_buffers[kBufferCount] = {};

  GLuint m_verletProg = 0;
  GLuint m_constraintProg = 0;
  GLuint m_collisionProg = 0;
  GLuint m_pinProg = 0;
  GLuint m_normalAccumProg = 0;
  GLuint m_normalFinalizeProg = 0;
#else
  bool m_available = false;
#endif
};

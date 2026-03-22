/// @file gpu_solver.cpp
/// @brief GPU XPBD solver with graph-coloring for race-free parallel constraints.

#include "gpu_solver.hpp"

#include <algorithm>
#include <cstdio>
#include <vector>

#ifndef CLOTHDD_HAVE_GLAD

GpuSolver::GpuSolver() {}
GpuSolver::~GpuSolver() {}
void GpuSolver::upload(const Cloth &) {}
void GpuSolver::simulate(Cloth &, float, int, const Vec3 &, const Vec3 &,
                          float) {}

#else // CLOTHDD_HAVE_GLAD

// ── GPU-side spring (matches GLSL layout) ────────────────────────────────

struct GpuSpring {
  int a, b;
  float restLength, compliance;
};

// ── Shader sources ───────────────────────────────────────────────────────

static const char *kVerletSrc = R"(
#version 430
layout(local_size_x = 64) in;
layout(std430, binding = 0) buffer Pos  { vec4 pos[];  };
layout(std430, binding = 3) buffer Prev { vec4 prev[]; };
layout(std430, binding = 2) buffer Pin  { uint pin[];  };
uniform vec3 u_gravity;
uniform vec3 u_wind;
uniform float u_windStr;
uniform float u_dt2;
uniform float u_damp;
uniform int u_n;
void main() {
  uint i = gl_GlobalInvocationID.x;
  if (i >= u_n || pin[i] != 0u) return;
  vec3 p = pos[i].xyz, pp = prev[i].xyz;
  vec3 a = u_gravity;
  if (u_windStr > 0.0) a += normalize(u_wind) * u_windStr * 0.5;
  vec3 v = (p - pp) * u_damp;
  prev[i].xyz = p;
  pos[i].xyz = p + v + a * u_dt2;
}
)";

// Direct-write constraint shader: no atomics needed because graph coloring
// guarantees no two invocations in the same dispatch touch the same vertex.
static const char *kConstraintSrc = R"(
#version 430
layout(local_size_x = 64) in;
struct Spr { int a; int b; float rest; float comp; };
layout(std430, binding = 0) buffer Pos { vec4 pos[]; };
layout(std430, binding = 1) buffer Sps { Spr  sps[]; };
layout(std430, binding = 2) buffer Pin { uint pin[]; };
uniform float u_dt2;
uniform int u_offset;
uniform int u_count;
void main() {
  int idx = int(gl_GlobalInvocationID.x);
  if (idx >= u_count) return;
  Spr s = sps[u_offset + idx];
  vec3 pa = pos[s.a].xyz, pb = pos[s.b].xyz;
  vec3 d = pb - pa;
  float dist = length(d);
  if (dist < 1e-6) return;
  float C = dist - s.rest;
  float wa = (pin[s.a] != 0u) ? 0.0 : 1.0;
  float wb = (pin[s.b] != 0u) ? 0.0 : 1.0;
  float w = wa + wb;
  if (w < 1e-6) return;
  float aT = s.comp / max(u_dt2, 1e-12);
  float dL = -C / (w + aT);
  vec3 corr = (d / dist) * dL;
  pos[s.a].xyz -= corr * wa;
  pos[s.b].xyz += corr * wb;
}
)";

static const char *kCollisionSrc = R"(
#version 430
layout(local_size_x = 64) in;
layout(std430, binding = 0) buffer Pos  { vec4 pos[];  };
layout(std430, binding = 3) buffer Prev { vec4 prev[]; };
layout(std430, binding = 2) buffer Pin  { uint pin[];  };
uniform int u_n;
uniform float u_floorY;
uniform bool u_floorOn;
uniform bool u_sphereOn;
uniform vec3 u_sphC;
uniform float u_sphR;
void main() {
  uint i = gl_GlobalInvocationID.x;
  if (i >= u_n || pin[i] != 0u) return;
  vec3 p = pos[i].xyz, pp = prev[i].xyz;
  if (u_floorOn) {
    float cy = u_floorY + 0.012;
    if (p.y < cy) { float vy = p.y - pp.y; p.y = cy; pp.y = p.y - vy * 0.15; }
  }
  if (u_sphereOn) {
    vec3 tp = p - u_sphC; float d = length(tp); float mr = u_sphR + 0.012;
    if (d < mr && d > 1e-6) {
      vec3 n = tp / d; vec3 np = u_sphC + n * mr;
      vec3 v = (p - pp) * 0.5; p = np; pp = p - v;
    }
  }
  pos[i].xyz = p; prev[i].xyz = pp;
}
)";

static const char *kPinSrc = R"(
#version 430
layout(local_size_x = 64) in;
layout(std430, binding = 0) buffer Pos  { vec4 pos[];  };
layout(std430, binding = 3) buffer Prev { vec4 prev[]; };
layout(std430, binding = 4) buffer Init { vec4 init[]; };
layout(std430, binding = 2) buffer Pin  { uint pin[];  };
uniform int u_n;
void main() {
  uint i = gl_GlobalInvocationID.x;
  if (i >= u_n || pin[i] == 0u) return;
  pos[i] = init[i]; prev[i] = init[i];
}
)";

static const char *kNormAccumSrc = R"(
#version 430
layout(local_size_x = 64) in;
layout(std430, binding = 0) buffer Pos { vec4 pos[]; };
layout(std430, binding = 5) buffer NI  { int  ni[];  };
layout(std430, binding = 6) buffer Tri { uint tri[]; };
uniform int u_tc;
const float S = 268435456.0;
void main() {
  uint i = gl_GlobalInvocationID.x;
  if (i >= u_tc) return;
  uint i0 = tri[i*3], i1 = tri[i*3+1], i2 = tri[i*3+2];
  vec3 n = cross(pos[i1].xyz - pos[i0].xyz, pos[i2].xyz - pos[i0].xyz);
  for (uint v = 0u; v < 3u; v++) {
    uint vi = (v==0u)?i0:((v==1u)?i1:i2);
    atomicAdd(ni[vi*3+0], int(n.x*S));
    atomicAdd(ni[vi*3+1], int(n.y*S));
    atomicAdd(ni[vi*3+2], int(n.z*S));
  }
}
)";

static const char *kNormFinSrc = R"(
#version 430
layout(local_size_x = 64) in;
layout(std430, binding = 5) buffer NI { int  ni[]; };
layout(std430, binding = 7) buffer NF { vec4 nf[]; };
uniform int u_n;
const float IS = 1.0/268435456.0;
void main() {
  uint i = gl_GlobalInvocationID.x;
  if (i >= u_n) return;
  vec3 n = vec3(float(ni[i*3])*IS, float(ni[i*3+1])*IS, float(ni[i*3+2])*IS);
  float l = length(n); if (l > 1e-6) n /= l;
  nf[i] = vec4(n, 0.0);
  ni[i*3]=0; ni[i*3+1]=0; ni[i*3+2]=0;
}
)";

// ── Helpers ──────────────────────────────────────────────────────────────

static int divUp(int n, int b) { return (n + b - 1) / b; }

GLuint GpuSolver::compileShader(const char *src) {
  GLuint s = glCreateShader(GL_COMPUTE_SHADER);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetShaderInfoLog(s, sizeof(log), nullptr, log);
    std::fprintf(stderr, "Shader compile error:\n%s\n", log);
    glDeleteShader(s);
    return 0;
  }
  return s;
}

GLuint GpuSolver::linkProgram(GLuint sh) {
  if (!sh) return 0;
  GLuint p = glCreateProgram();
  glAttachShader(p, sh);
  glLinkProgram(p);
  glDeleteShader(sh);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetProgramInfoLog(p, sizeof(log), nullptr, log);
    std::fprintf(stderr, "Program link error:\n%s\n", log);
    glDeleteProgram(p);
    return 0;
  }
  return p;
}

bool GpuSolver::initShaders() {
  m_verletProg     = linkProgram(compileShader(kVerletSrc));
  m_constraintProg = linkProgram(compileShader(kConstraintSrc));
  m_collisionProg  = linkProgram(compileShader(kCollisionSrc));
  m_pinProg        = linkProgram(compileShader(kPinSrc));
  m_normalAccumProg   = linkProgram(compileShader(kNormAccumSrc));
  m_normalFinalizeProg = linkProgram(compileShader(kNormFinSrc));
  return m_verletProg && m_constraintProg && m_collisionProg && m_pinProg &&
         m_normalAccumProg && m_normalFinalizeProg;
}

// ── Lifecycle ────────────────────────────────────────────────────────────

GpuSolver::GpuSolver() {
  if (!GLAD_GL_VERSION_4_3) {
    std::fprintf(stderr, "GpuSolver: GL 4.3 not available.\n");
    return;
  }
  if (!initShaders()) {
    std::fprintf(stderr, "GpuSolver: shader compilation failed.\n");
    return;
  }
  glGenBuffers(kBufferCount, m_buffers);
  m_available = true;
  std::printf("GpuSolver: ready (graph-coloring constraint solver).\n");
}

GpuSolver::~GpuSolver() {
  if (!m_available) return;
  glDeleteBuffers(kBufferCount, m_buffers);
  auto del = [](GLuint &p) { if (p) { glDeleteProgram(p); p = 0; } };
  del(m_verletProg); del(m_constraintProg); del(m_collisionProg);
  del(m_pinProg); del(m_normalAccumProg); del(m_normalFinalizeProg);
}

// ── Graph coloring for a regular grid mesh ───────────────────────────────

static int springColor(const Spring &s, int width) {
  int x0 = s.a % width, y0 = s.a / width;
  int x1 = s.b % width, y1 = s.b / width;
  int dx = x1 - x0, dy = y1 - y0;

  switch (s.type) {
  case SpringType::Structural:
    if (dy == 0) return 0 + (x0 % 2);             // horizontal
    else         return 2 + (y0 % 2);             // vertical
  case SpringType::Shear:
    if (dx > 0)  return 4 + (x0 % 2);             // NE diagonal
    else         return 6 + (std::min(x0, x1) % 2); // NW diagonal
  case SpringType::Bend:
    if (dy == 0) return 8  + ((x0 / 2) % 2);      // horizontal bend
    else         return 10 + ((y0 / 2) % 2);      // vertical bend
  }
  return 0;
}

// ── Upload ───────────────────────────────────────────────────────────────

void GpuSolver::upload(const Cloth &cloth) {
  if (!m_available) return;

  const auto &pos  = cloth.positions();
  const auto &prev = cloth.prevPositions();
  const auto &pins = cloth.pinnedMask();
  const auto &sprs = cloth.springs();
  const auto &tris = cloth.triangleIndices();
  const int width  = cloth.width();

  m_particleCount = static_cast<int>(pos.size());
  m_springCount   = static_cast<int>(sprs.size());
  m_triangleCount = static_cast<int>(tris.size()) / 3;

  // ── Sort springs by color ──
  std::vector<int> sortedIdx(static_cast<size_t>(m_springCount));
  for (int i = 0; i < m_springCount; ++i) sortedIdx[static_cast<size_t>(i)] = i;
  std::sort(sortedIdx.begin(), sortedIdx.end(), [&](int a, int b) {
    return springColor(sprs[static_cast<size_t>(a)], width) <
           springColor(sprs[static_cast<size_t>(b)], width);
  });

  // Build color groups.
  m_numColors = 0;
  for (auto &g : m_colorGroups) g = {0, 0};
  int prevColor = -1;
  for (int i = 0; i < m_springCount; ++i) {
    int c = springColor(sprs[static_cast<size_t>(sortedIdx[static_cast<size_t>(i)])], width);
    if (c != prevColor) {
      if (m_numColors < kMaxColors) {
        m_colorGroups[m_numColors].offset = i;
        m_colorGroups[m_numColors].count = 0;
      }
      prevColor = c;
      ++m_numColors;
    }
    if (m_numColors - 1 < kMaxColors)
      m_colorGroups[m_numColors - 1].count++;
  }
  m_numColors = std::min(m_numColors, kMaxColors);
  std::printf("GpuSolver: %d springs in %d color groups.\n",
              m_springCount, m_numColors);

  // ── Pack GPU springs in sorted order ──
  std::vector<GpuSpring> gpuSprs(static_cast<size_t>(m_springCount));
  for (int i = 0; i < m_springCount; ++i) {
    const Spring &s = sprs[static_cast<size_t>(sortedIdx[static_cast<size_t>(i)])];
    gpuSprs[static_cast<size_t>(i)] = {s.a, s.b, s.restLength, s.compliance};
  }

  // ── Pack particle data as vec4 ──
  auto packVec3 = [&](const std::vector<Vec3> &src) {
    std::vector<float> out(static_cast<size_t>(m_particleCount) * 4, 0.0f);
    for (int i = 0; i < m_particleCount; ++i) {
      const size_t i4 = static_cast<size_t>(i) * 4;
      out[i4]     = src[static_cast<size_t>(i)].x;
      out[i4 + 1] = src[static_cast<size_t>(i)].y;
      out[i4 + 2] = src[static_cast<size_t>(i)].z;
    }
    return out;
  };
  auto pos4  = packVec3(pos);
  auto prev4 = packVec3(prev);
  auto init4 = packVec3(pos); // at upload time positions == initial

  std::vector<unsigned int> pinnedU(static_cast<size_t>(m_particleCount));
  for (int i = 0; i < m_particleCount; ++i)
    pinnedU[static_cast<size_t>(i)] = pins[static_cast<size_t>(i)] ? 1u : 0u;

  std::vector<int>   normI(static_cast<size_t>(m_particleCount) * 3, 0);
  std::vector<float> normF(static_cast<size_t>(m_particleCount) * 4, 0.0f);

  // ── Upload SSBOs ──
  auto up = [](GLuint buf, GLsizeiptr sz, const void *d) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sz, d, GL_DYNAMIC_COPY);
  };
  up(m_buffers[kPositions],        m_particleCount * 4 * GLsizeiptr(sizeof(float)), pos4.data());
  up(m_buffers[kSprings],          m_springCount * GLsizeiptr(sizeof(GpuSpring)),    gpuSprs.data());
  up(m_buffers[kPinned],           m_particleCount * GLsizeiptr(sizeof(unsigned)),   pinnedU.data());
  up(m_buffers[kPrevPositions],    m_particleCount * 4 * GLsizeiptr(sizeof(float)), prev4.data());
  up(m_buffers[kInitialPositions], m_particleCount * 4 * GLsizeiptr(sizeof(float)), init4.data());
  up(m_buffers[kNormalsInt],       m_particleCount * 3 * GLsizeiptr(sizeof(int)),   normI.data());
  up(m_buffers[kTriangles],        GLsizeiptr(tris.size() * sizeof(unsigned)),      tris.data());
  up(m_buffers[kNormalsFloat],     m_particleCount * 4 * GLsizeiptr(sizeof(float)), normF.data());
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

  m_uploaded = true;
}

// ── Simulate ─────────────────────────────────────────────────────────────

void GpuSolver::simulate(Cloth &cloth, float dt, int solverIterations,
                          const Vec3 &gravity, const Vec3 &windDirection,
                          float windStrength) {
  if (!m_available || !m_uploaded || dt <= 0.0f) return;

  const float dt2 = dt * dt;
  const int iters = std::max(1, solverIterations);
  constexpr int WG = 64;

  for (int i = 0; i < kBufferCount; ++i)
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, static_cast<GLuint>(i),
                     m_buffers[i]);

  // ── Verlet ──
  glUseProgram(m_verletProg);
  glUniform3f(glGetUniformLocation(m_verletProg, "u_gravity"),
              gravity.x, gravity.y, gravity.z);
  glUniform3f(glGetUniformLocation(m_verletProg, "u_wind"),
              windDirection.x, windDirection.y, windDirection.z);
  glUniform1f(glGetUniformLocation(m_verletProg, "u_windStr"), windStrength);
  glUniform1f(glGetUniformLocation(m_verletProg, "u_dt2"), dt2);
  glUniform1f(glGetUniformLocation(m_verletProg, "u_damp"), cloth.damping());
  glUniform1i(glGetUniformLocation(m_verletProg, "u_n"), m_particleCount);
  glDispatchCompute(static_cast<GLuint>(divUp(m_particleCount, WG)), 1, 1);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

  // ── Constraint iterations (graph-colored Gauss-Seidel) ──
  glUseProgram(m_constraintProg);
  glUniform1f(glGetUniformLocation(m_constraintProg, "u_dt2"), dt2);
  const GLint offLoc = glGetUniformLocation(m_constraintProg, "u_offset");
  const GLint cntLoc = glGetUniformLocation(m_constraintProg, "u_count");

  for (int iter = 0; iter < iters; ++iter) {
    for (int c = 0; c < m_numColors; ++c) {
      const ColorGroup &g = m_colorGroups[c];
      if (g.count <= 0) continue;
      glUniform1i(offLoc, g.offset);
      glUniform1i(cntLoc, g.count);
      glDispatchCompute(static_cast<GLuint>(divUp(g.count, WG)), 1, 1);
      glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }

    // Collisions.
    glUseProgram(m_collisionProg);
    glUniform1i(glGetUniformLocation(m_collisionProg, "u_n"), m_particleCount);
    glUniform1f(glGetUniformLocation(m_collisionProg, "u_floorY"),
                cloth.floorY());
    glUniform1i(glGetUniformLocation(m_collisionProg, "u_floorOn"),
                cloth.floorEnabled() ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_collisionProg, "u_sphereOn"),
                cloth.sphereEnabled() ? 1 : 0);
    glUniform3f(glGetUniformLocation(m_collisionProg, "u_sphC"),
                cloth.sphereCenter().x, cloth.sphereCenter().y,
                cloth.sphereCenter().z);
    glUniform1f(glGetUniformLocation(m_collisionProg, "u_sphR"),
                cloth.sphereRadius());
    glDispatchCompute(static_cast<GLuint>(divUp(m_particleCount, WG)), 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // Pin enforcement.
    glUseProgram(m_pinProg);
    glUniform1i(glGetUniformLocation(m_pinProg, "u_n"), m_particleCount);
    glDispatchCompute(static_cast<GLuint>(divUp(m_particleCount, WG)), 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // Switch back to constraint program for next iteration.
    glUseProgram(m_constraintProg);
  }

  // ── Normals ──
  glUseProgram(m_normalAccumProg);
  glUniform1i(glGetUniformLocation(m_normalAccumProg, "u_tc"), m_triangleCount);
  glDispatchCompute(static_cast<GLuint>(divUp(m_triangleCount, WG)), 1, 1);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

  glUseProgram(m_normalFinalizeProg);
  glUniform1i(glGetUniformLocation(m_normalFinalizeProg, "u_n"),
              m_particleCount);
  glDispatchCompute(static_cast<GLuint>(divUp(m_particleCount, WG)), 1, 1);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
  glUseProgram(0);

  // ── Read back positions + normals ──
  auto readback = [](GLuint buf, int count, std::vector<Vec3> &dst) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf);
    const float *p = static_cast<const float *>(glMapBufferRange(
        GL_SHADER_STORAGE_BUFFER, 0,
        static_cast<GLsizeiptr>(count) * 4 * GLsizeiptr(sizeof(float)),
        GL_MAP_READ_BIT));
    if (p) {
      for (int i = 0; i < count; ++i) {
        const size_t i4 = static_cast<size_t>(i) * 4;
        dst[static_cast<size_t>(i)] = {p[i4], p[i4 + 1], p[i4 + 2]};
      }
      glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    }
  };
  readback(m_buffers[kPositions], m_particleCount, cloth.mutablePositions());
  readback(m_buffers[kNormalsFloat], m_particleCount, cloth.mutableNormals());
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

#endif // CLOTHDD_HAVE_GLAD

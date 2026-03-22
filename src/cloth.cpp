/// @file cloth.cpp
/// @brief Cloth simulation implementation — grid construction, spring topology,
///        Verlet integration, collision response, and domain decomposition.

#include "cloth.hpp"
#include "thread_pool.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kEpsilon = 1e-6f;

} // namespace

Vec3 operator*(float scalar, const Vec3 &value) { return value * scalar; }

float dot(const Vec3 &a, const Vec3 &b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross(const Vec3 &a, const Vec3 &b) {
  return Vec3{
      a.y * b.z - a.z * b.y,
      a.z * b.x - a.x * b.z,
      a.x * b.y - a.y * b.x,
  };
}

float length(const Vec3 &value) { return std::sqrt(dot(value, value)); }

Vec3 normalize(const Vec3 &value) {
  const float magnitude = length(value);
  if (magnitude <= kEpsilon) {
    return Vec3{};
  }
  return value / magnitude;
}

Cloth::Cloth(int widthPoints, int heightPoints, float pointSpacing)
    : m_width(std::max(2, widthPoints)), m_height(std::max(2, heightPoints)),
      m_spacing(std::max(1e-4f, pointSpacing)),
      m_threadPool(std::make_unique<ThreadPool>()) {
  reset();
}

Cloth::~Cloth() = default;
Cloth::Cloth(Cloth &&other) noexcept = default;
Cloth &Cloth::operator=(Cloth &&other) noexcept = default;

void Cloth::reset() {
  const int pointCount = m_width * m_height;
  m_initialPositions.assign(static_cast<std::size_t>(pointCount), Vec3{});
  m_positions.assign(static_cast<std::size_t>(pointCount), Vec3{});
  m_uvs.assign(static_cast<std::size_t>(pointCount), Vec2{});
  m_prevPositions.assign(static_cast<std::size_t>(pointCount), Vec3{});
  m_normals.assign(static_cast<std::size_t>(pointCount),
                   Vec3{0.0f, 0.0f, 1.0f});
  m_pinned.assign(static_cast<std::size_t>(pointCount), false);

  m_dynamicIndices.clear();
  m_pinnedIndices.clear();

  buildGrid();
  buildTopology();
  rebuildDomainDecomposition();
  recomputeNormals();
}

void Cloth::unpinAll() {
  m_pinned.assign(m_pinned.size(), false);
  m_dynamicIndices.clear();
  m_pinnedIndices.clear();
  for (std::size_t i = 0; i < m_positions.size(); ++i) {
    m_dynamicIndices.push_back(static_cast<int>(i));
  }
}

void Cloth::simulate(float dt, int solverIterations, const Vec3 &gravity,
                     const Vec3 &windDirection, float windStrength) {
  if (dt <= 0.0f) {
    return;
  }

  const bool hasWind = windStrength > 0.0f && length(windDirection) > kEpsilon;
  const Vec3 normalizedWind = hasWind ? normalize(windDirection) : Vec3{};
  const float dtSquared = dt * dt;
  const float damping = m_damping;

  for (int pointIndex : m_dynamicIndices) {
    const std::size_t i = static_cast<std::size_t>(pointIndex);

    Vec3 acceleration = gravity;
    if (hasWind) {
      const float facing = std::max(0.0f, dot(m_normals[i], normalizedWind));
      acceleration += normalizedWind * (windStrength * (0.20f + facing));
    }

    const Vec3 velocity = (m_positions[i] - m_prevPositions[i]) * damping;
    m_prevPositions[i] = m_positions[i];
    m_positions[i] += velocity + acceleration * dtSquared;
  }

  const int iterations = std::max(1, solverIterations);
  for (int iteration = 0; iteration < iterations; ++iteration) {
    if (m_domainLocalSolveEnabled && m_domainCount > 1 &&
        !m_domainSpringIndices.empty()) {
      // Solve domain-local springs in parallel — each domain's springs only
      // reference particles within that domain, so there are no data races.
      const int domainSlots =
          static_cast<int>(m_domainSpringIndices.size());
      m_threadPool->run(domainSlots, [this](int d) {
        for (int springIndex :
             m_domainSpringIndices[static_cast<std::size_t>(d)]) {
          solveSpringConstraint(
              m_springs[static_cast<std::size_t>(springIndex)]);
        }
      });
      // Interface springs cross domain boundaries — solve sequentially.
      for (int springIndex : m_interfaceSpringIndices) {
        solveSpringConstraint(m_springs[static_cast<std::size_t>(springIndex)]);
      }
    } else {
      for (const Spring &spring : m_springs) {
        solveSpringConstraint(spring);
      }
    }

    for (int pointIndex : m_dynamicIndices) {
      applyFloorCollision(pointIndex);
      applySphereCollision(pointIndex);
    }
    enforcePinnedPoints();
  }

  recomputeNormals();
}

void Cloth::setFloorEnabled(bool enabled) { m_floorEnabled = enabled; }

void Cloth::setFloorY(float y) { m_floorY = y; }

void Cloth::setSphereEnabled(bool enabled) { m_sphereEnabled = enabled; }

void Cloth::setSphere(const Vec3 &center, float radius) {
  m_sphereCenter = center;
  m_sphereRadius = radius;
}

void Cloth::setDomainLocalSolveEnabled(bool enabled) {
  m_domainLocalSolveEnabled = enabled;
}

void Cloth::setDomainCount(int domainCount) {
  m_domainCount = std::clamp(domainCount, 1, 64);
  rebuildDomainDecomposition();
}

void Cloth::setSquareDomainDecompositionEnabled(bool enabled) {
  m_squareDomainDecompositionEnabled = enabled;
  rebuildDomainDecomposition();
}

void Cloth::setSpringStiffness(float stiffness) {
  m_springStiffness = std::clamp(stiffness, 0.01f, 1.0f);
  for (Spring &s : m_springs) {
    s.stiffness = m_springStiffness;
  }
}

void Cloth::setDamping(float damping) {
  m_damping = std::clamp(damping, 0.9f, 1.0f);
}

int Cloth::workerThreadCount() const {
  return m_threadPool ? m_threadPool->threadCount() : 1;
}

int Cloth::index(int x, int y) const { return y * m_width + x; }

void Cloth::buildGrid() {
  const float startX = -0.5f * static_cast<float>(m_width - 1) * m_spacing;

  m_dynamicIndices.reserve(m_initialPositions.size());
  m_pinnedIndices.reserve(static_cast<std::size_t>(m_width));

  for (int y = 0; y < m_height; ++y) {
    for (int x = 0; x < m_width; ++x) {
      const int pointIndex = index(x, y);
      const std::size_t i = static_cast<std::size_t>(pointIndex);

      const Vec3 point{
          startX + static_cast<float>(x) * m_spacing,
          m_startY - static_cast<float>(y) * m_spacing * m_verticalSpacingScale,
          0.0f,
      };

      m_initialPositions[i] = point;
      m_positions[i] = point;
      m_uvs[i] = Vec2{static_cast<float>(x) /
                          static_cast<float>(std::max(1, m_width - 1)),
                      static_cast<float>(y) /
                          static_cast<float>(std::max(1, m_height - 1))};
      m_prevPositions[i] = point;

      if (y == 0) {
        m_pinned[i] = true;
        m_pinnedIndices.push_back(pointIndex);
      } else {
        m_dynamicIndices.push_back(pointIndex);
      }
    }
  }
}

void Cloth::buildTopology() {
  m_springs.clear();
  m_triangleIndices.clear();
  m_lineIndices.clear();

  const auto addSpring = [&](int a, int b) {
    m_springs.push_back(Spring{a, b, m_spacing, 1.0f});
    m_lineIndices.push_back(static_cast<unsigned int>(a));
    m_lineIndices.push_back(static_cast<unsigned int>(b));
  };

  for (int y = 0; y < m_height; ++y) {
    for (int x = 0; x < m_width; ++x) {
      const int p = index(x, y);
      if (x + 1 < m_width) {
        addSpring(p, index(x + 1, y));
      }
      if (y + 1 < m_height) {
        addSpring(p, index(x, y + 1));
      }
    }
  }

  for (int y = 0; y + 1 < m_height; ++y) {
    for (int x = 0; x + 1 < m_width; ++x) {
      const unsigned int i0 = static_cast<unsigned int>(index(x, y));
      const unsigned int i1 = static_cast<unsigned int>(index(x + 1, y));
      const unsigned int i2 = static_cast<unsigned int>(index(x, y + 1));
      const unsigned int i3 = static_cast<unsigned int>(index(x + 1, y + 1));

      m_triangleIndices.push_back(i0);
      m_triangleIndices.push_back(i2);
      m_triangleIndices.push_back(i1);

      m_triangleIndices.push_back(i1);
      m_triangleIndices.push_back(i2);
      m_triangleIndices.push_back(i3);
    }
  }
}

void Cloth::rebuildDomainDecomposition() {
  m_springDomains.assign(m_springs.size(), -1);
  m_domainSpringIndices.clear();
  m_interfaceSpringIndices.clear();

  if (m_domainCount <= 1 || m_springs.empty()) {
    return;
  }

  int domainSlots = m_domainCount;
  int domainGridX = m_domainCount;
  int domainGridY = 1;

  if (m_squareDomainDecompositionEnabled) {
    const float aspect = static_cast<float>(std::max(1, m_width)) /
                         static_cast<float>(std::max(1, m_height));
    domainGridX = static_cast<int>(std::round(
        std::sqrt(static_cast<float>(m_domainCount) * std::max(0.1f, aspect))));
    domainGridX = std::clamp(domainGridX, 1, m_domainCount);
    domainGridY = std::max(1, (m_domainCount + domainGridX - 1) / domainGridX);

    domainGridX = std::clamp(domainGridX, 1, std::max(1, m_width));
    domainGridY = std::clamp(domainGridY, 1, std::max(1, m_height));
    domainSlots = domainGridX * domainGridY;
  }

  m_domainSpringIndices.resize(
      static_cast<std::size_t>(std::max(1, domainSlots)));

  for (std::size_t springIndex = 0; springIndex < m_springs.size();
       ++springIndex) {
    const Spring &spring = m_springs[springIndex];
    int domainA = 0;
    int domainB = 0;
    if (!m_squareDomainDecompositionEnabled) {
      const int xA = spring.a % m_width;
      const int xB = spring.b % m_width;
      domainA = std::min(m_domainCount - 1,
                         (xA * m_domainCount) / std::max(1, m_width));
      domainB = std::min(m_domainCount - 1,
                         (xB * m_domainCount) / std::max(1, m_width));
    } else {
      const int xA = spring.a % m_width;
      const int yA = spring.a / m_width;
      const int xB = spring.b % m_width;
      const int yB = spring.b / m_width;

      const int cellAX =
          std::min(domainGridX - 1, (xA * domainGridX) / std::max(1, m_width));
      const int cellAY =
          std::min(domainGridY - 1, (yA * domainGridY) / std::max(1, m_height));
      const int cellBX =
          std::min(domainGridX - 1, (xB * domainGridX) / std::max(1, m_width));
      const int cellBY =
          std::min(domainGridY - 1, (yB * domainGridY) / std::max(1, m_height));
      domainA = cellAY * domainGridX + cellAX;
      domainB = cellBY * domainGridX + cellBX;
    }

    if (domainA == domainB) {
      m_springDomains[springIndex] = domainA;
      m_domainSpringIndices[static_cast<std::size_t>(domainA)].push_back(
          static_cast<int>(springIndex));
    } else {
      m_interfaceSpringIndices.push_back(static_cast<int>(springIndex));
    }
  }
}

void Cloth::solveSpringConstraint(const Spring &spring) {
  const std::size_t a = static_cast<std::size_t>(spring.a);
  const std::size_t b = static_cast<std::size_t>(spring.b);

  Vec3 &pa = m_positions[a];
  Vec3 &pb = m_positions[b];
  const Vec3 delta = pb - pa;
  const float distance = length(delta);
  if (distance <= kEpsilon) {
    return;
  }

  const float displacement = (distance - spring.restLength) / distance;
  const Vec3 correction = delta * (0.5f * spring.stiffness * displacement);
  const bool aPinned = m_pinned[a];
  const bool bPinned = m_pinned[b];

  if (!aPinned && !bPinned) {
    pa += correction;
    pb -= correction;
    return;
  }
  if (aPinned && !bPinned) {
    pb -= delta * (spring.stiffness * displacement);
    return;
  }
  if (!aPinned && bPinned) {
    pa += delta * (spring.stiffness * displacement);
  }
}

void Cloth::applyFloorCollision(int pointIndex) {
  if (!m_floorEnabled) {
    return;
  }

  const std::size_t i = static_cast<std::size_t>(pointIndex);
  Vec3 &point = m_positions[i];
  Vec3 &previous = m_prevPositions[i];

  constexpr float kFloorContactOffset = 0.012f;
  const float floorContactY = m_floorY + kFloorContactOffset;
  if (point.y < floorContactY) {
    point.y = floorContactY;
    const float velocityY = point.y - previous.y;
    previous.y = point.y - velocityY * 0.15f;
  }
}

void Cloth::applySphereCollision(int pointIndex) {
  if (!m_sphereEnabled) {
    return;
  }

  const std::size_t i = static_cast<std::size_t>(pointIndex);
  Vec3 &point = m_positions[i];
  Vec3 &previous = m_prevPositions[i];

  Vec3 toPoint = point - m_sphereCenter;
  float dist = length(toPoint);
  constexpr float kOffset = 0.012f;
  const float minRadius = m_sphereRadius + kOffset;

  if (dist < minRadius) {
    Vec3 surfaceNormal = normalize(toPoint);
    Vec3 newPoint = m_sphereCenter + surfaceNormal * minRadius;

    Vec3 velocity = point - previous;

    // Friction
    velocity = velocity * 0.5f;

    point = newPoint;
    previous = point - velocity;
  }
}

void Cloth::enforcePinnedPoints() {
  for (int pointIndex : m_pinnedIndices) {
    const std::size_t i = static_cast<std::size_t>(pointIndex);
    m_positions[i] = m_initialPositions[i];
    m_prevPositions[i] = m_initialPositions[i];
  }
}

void Cloth::recomputeNormals() {
  std::fill(m_normals.begin(), m_normals.end(), Vec3{});

  for (std::size_t i = 0; i + 2 < m_triangleIndices.size(); i += 3) {
    const unsigned int i0 = m_triangleIndices[i];
    const unsigned int i1 = m_triangleIndices[i + 1];
    const unsigned int i2 = m_triangleIndices[i + 2];

    const Vec3 &p0 = m_positions[i0];
    const Vec3 &p1 = m_positions[i1];
    const Vec3 &p2 = m_positions[i2];
    const Vec3 triangleNormal = cross(p1 - p0, p2 - p0);

    m_normals[i0] += triangleNormal;
    m_normals[i1] += triangleNormal;
    m_normals[i2] += triangleNormal;
  }

  for (Vec3 &normal : m_normals) {
    normal = normalize(normal);
  }
}

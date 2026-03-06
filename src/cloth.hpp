/// @file cloth.hpp
/// @brief Core cloth simulation — mass-spring system with Verlet integration
///        and domain-decomposed constraint solving.

#pragma once

#include <vector>

struct Vec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;

  Vec3() = default;
  Vec3(float xValue, float yValue, float zValue)
      : x(xValue), y(yValue), z(zValue) {}

  Vec3 operator+(const Vec3 &rhs) const {
    return Vec3{x + rhs.x, y + rhs.y, z + rhs.z};
  }
  Vec3 operator-(const Vec3 &rhs) const {
    return Vec3{x - rhs.x, y - rhs.y, z - rhs.z};
  }
  Vec3 operator*(float scalar) const {
    return Vec3{x * scalar, y * scalar, z * scalar};
  }
  Vec3 operator/(float scalar) const {
    return Vec3{x / scalar, y / scalar, z / scalar};
  }

  Vec3 &operator+=(const Vec3 &rhs) {
    x += rhs.x;
    y += rhs.y;
    z += rhs.z;
    return *this;
  }

  Vec3 &operator-=(const Vec3 &rhs) {
    x -= rhs.x;
    y -= rhs.y;
    z -= rhs.z;
    return *this;
  }
};

struct Vec2 {
  float x = 0.0f;
  float y = 0.0f;
};

Vec3 operator*(float scalar, const Vec3 &value);
float dot(const Vec3 &a, const Vec3 &b);
Vec3 cross(const Vec3 &a, const Vec3 &b);
float length(const Vec3 &value);
Vec3 normalize(const Vec3 &value);

struct Spring {
  int a = 0;
  int b = 0;
  float restLength = 0.0f;
  float stiffness = 1.0f;
};

class Cloth {
public:
  Cloth(int widthPoints, int heightPoints, float pointSpacing);

  void reset();
  void unpinAll();
  void simulate(float dt, int solverIterations, const Vec3 &gravity,
                const Vec3 &windDirection, float windStrength);

  void setFloorEnabled(bool enabled);
  void setFloorY(float y);
  void setSphereEnabled(bool enabled);
  void setSphere(const Vec3 &center, float radius);
  void setDomainLocalSolveEnabled(bool enabled);
  void setDomainCount(int domainCount);
  void setSquareDomainDecompositionEnabled(bool enabled);
  void setSpringStiffness(float stiffness);
  void setDamping(float damping);
  float springStiffness() const { return m_springStiffness; }
  float damping() const { return m_damping; }

  int width() const { return m_width; }
  int height() const { return m_height; }
  float spacing() const { return m_spacing; }
  bool floorEnabled() const { return m_floorEnabled; }
  float floorY() const { return m_floorY; }
  bool sphereEnabled() const { return m_sphereEnabled; }
  float sphereRadius() const { return m_sphereRadius; }
  Vec3 sphereCenter() const { return m_sphereCenter; }
  bool domainLocalSolveEnabled() const { return m_domainLocalSolveEnabled; }
  int domainCount() const { return m_domainCount; }
  bool squareDomainDecompositionEnabled() const {
    return m_squareDomainDecompositionEnabled;
  }

  const std::vector<Vec3> &positions() const { return m_positions; }
  const std::vector<Vec2> &uvs() const { return m_uvs; }
  const std::vector<Vec3> &prevPositions() const { return m_prevPositions; }
  const std::vector<Vec3> &normals() const { return m_normals; }
  const std::vector<bool> &pinnedMask() const { return m_pinned; }
  const std::vector<Spring> &springs() const { return m_springs; }
  const std::vector<unsigned int> &triangleIndices() const {
    return m_triangleIndices;
  }
  const std::vector<unsigned int> &lineIndices() const { return m_lineIndices; }
  const std::vector<int> &springDomains() const { return m_springDomains; }

private:
  int index(int x, int y) const;
  void buildGrid();
  void buildTopology();
  void rebuildDomainDecomposition();
  void solveSpringConstraint(const Spring &spring);
  void applyFloorCollision(int pointIndex);
  void applySphereCollision(int pointIndex);
  void enforcePinnedPoints();
  void recomputeNormals();

  int m_width = 0;
  int m_height = 0;
  float m_spacing = 0.1f;

  float m_startY = 1.8f;
  float m_verticalSpacingScale = 0.65f;

  std::vector<Vec3> m_initialPositions;
  std::vector<Vec3> m_positions;
  std::vector<Vec2> m_uvs;
  std::vector<Vec3> m_prevPositions;
  std::vector<Vec3> m_normals;
  std::vector<bool> m_pinned;

  std::vector<int> m_dynamicIndices;
  std::vector<int> m_pinnedIndices;

  std::vector<Spring> m_springs;
  std::vector<unsigned int> m_triangleIndices;
  std::vector<unsigned int> m_lineIndices;

  std::vector<int> m_springDomains;
  std::vector<std::vector<int>> m_domainSpringIndices;
  std::vector<int> m_interfaceSpringIndices;

  bool m_floorEnabled = true;
  float m_floorY = -2.0f;
  bool m_sphereEnabled = false;
  float m_sphereRadius = 0.8f;
  Vec3 m_sphereCenter{0.0f, -0.6f, 0.0f};
  bool m_domainLocalSolveEnabled = true;
  int m_domainCount = 4;
  bool m_squareDomainDecompositionEnabled = false;
  float m_springStiffness = 1.0f;
  float m_damping = 0.995f;
};

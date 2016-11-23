#pragma once

#include <cstdint>
#include <cassert>
#include <algorithm>
#include <vector>

#include "TinyAlgebra.hpp"

// TODO: refactor
// TODO: replace vector by array
template<int32_t RADIUS>
class SphereIterator
{
public:
    SphereIterator();

    std::vector<iVec3> m_points;

private:
    struct Node
    {
        int32_t x, y, z, d;
        Node(int32_t xa, int32_t ya, int32_t za, int32_t da) : x{ xa }, y{ ya }, z{ za }, d{ da } {}
    };

    static int32_t square_distance(int32_t x, int32_t y,int32_t z)
    {
      assert(x < 10000 && y < 10000 && z < 10000 && "Conservative overflow protection.");

      return x * x + y * y + z * z;
    }
};

template<int RADIUS>
SphereIterator<RADIUS>::SphereIterator()
{
  std::vector<Node> nodes;
  std::vector<int32_t> start_points;
  std::vector<int32_t> reference_distances;

  for (int32_t i = 0; i <= RADIUS + 1; ++i)
  {
    reference_distances.push_back(square_distance(i, 0, 0));
  }

  const int32_t too_far = square_distance(RADIUS + 1, 0, 0);

  for (int32_t z = -RADIUS; z <= RADIUS; ++z)
    for (int32_t y = -RADIUS; y <= RADIUS; ++y)
      for (int32_t x = -RADIUS; x <= RADIUS; ++x)
      {
        const Node current = Node{ x, y, z, square_distance(x, y, z) };

        if (current.d < too_far) nodes.push_back(current);
      }

  std::sort(nodes.begin(), nodes.end(), [&](const Node & a, const Node & b) { return a.d < b.d; });


  auto current_reference_distance = reference_distances.begin();

  for (int32_t i = 0; i < nodes.size(); ++i)
  {
    if (*current_reference_distance == nodes[i].d)
    {
      start_points.push_back(i);
      ++current_reference_distance;
    }

  }

  start_points.push_back(static_cast<int32_t>(nodes.size()));

  for (auto & i : nodes)
      m_points.push_back({ i.x, i.y, i.z });

}

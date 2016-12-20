#pragma once

#include <cstdint>
#include <cassert>
#include <algorithm>
#include <vector>
#include <iostream>
#include <iomanip>


#include "TinyAlgebra.hpp"
#include "TinyAlgebraExtensions.hpp"

// TODO: refactor
// TODO: replace vector by array
template<int32_t RADIUS>
class SphereIterator
{
public:
    SphereIterator();

    enum class Task : int { SYNC, LOAD_CHUNK, LOAD_MESH };
    struct Job { iVec3 position; Task task; };
    std::vector<Job> m_points;
    std::vector<Job> m_points_tmp;
    std::vector<iVec3> m_chunk_generation_list;

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
  std::vector<int32_t> levels;

  for (int32_t i = 0; i <= RADIUS; ++i)
  {
      const int32_t current = square_distance(i, 0, 0);
      levels.push_back(current);
  }

  const int32_t too_far = square_distance(RADIUS, 0, 0);
  assert(too_far == levels.back() && "test");

  for (int32_t z = -RADIUS; z <= RADIUS; ++z)
    for (int32_t y = -RADIUS; y <= RADIUS; ++y)
      for (int32_t x = -RADIUS; x <= RADIUS; ++x)
      {
        const Node current = Node{ x, y, z, square_distance(x, y, z) };

        if (current.d < too_far) nodes.push_back(current);
      }

  std::sort(nodes.begin(), nodes.end(), [&](const Node & a, const Node & b) { return a.d < b.d; });


  auto it = levels.begin();
  ++it;

  for (auto & i : nodes)
  {
    assert(it != levels.end());
    if (i.d >= *it) // all that are smaller than *it are kept
    {
      m_points_tmp.push_back({ { 0, 0, 0 }, Task::SYNC });
      ++it;
    }

    m_points_tmp.push_back({ { i.x, i.y, i.z }, Task::LOAD_MESH });
  }
  // sync at the end
  m_points_tmp.push_back({ { 0, 0, 0 }, Task::SYNC });

  for (const auto & i : m_points_tmp)
  {
    std::cout << std::setw(10)
        << std::setw(4) << i.position(0)
        << std::setw(4) << i.position(1)
        << std::setw(4) << i.position(2)
        << " | " << std::setw(3) << square_distance(i.position(0), i.position(1), i.position(2))
        << " | " << static_cast<int>(i.task)
        << std::endl;
  }

  for (const auto & i : levels)
  {
    std::cout << std::setw(10)
              << i
              << std::endl;
  }

  // TODO: iterator for chunk loading => get current mesh ring and add all chunks that are needed minus all chunks that have already been loaded
  //         -> this is just for chunk generation. aka. making sure that all chunks are generated before generating meshess -> reading chunks can be done without locking

  auto insert_if_unique = [](auto & container, auto data)
  {
    for (const auto & i : container)
      if (all(i == data))
        return false;

    container.push_back(data);
    return true;
  };


  std::vector<Job> relevant_meshes;
  std::vector<Job> relevant_chunks;

  // TODO: save result to file and reuse because the algorithm is O(n^2) and takes forever
  // TODO: verify correctness
  for (const auto & i : m_points_tmp)
  {
    if (i.task == Task::SYNC)
    {
      m_chunk_generation_list.push_back(i.position + iVec3{1337, 1337, 1337}); // TODO: dummy please replace by SYNC

      /*
       * gen chunks, sync, gen meshes,
       * gen chunks, sync, gen meshes,
       * gen chunks, sync, gen meshes ...
       */
      m_points.insert (m_points.end(), relevant_chunks.begin(), relevant_chunks.end());
      m_points.push_back({ iVec3{ 0, 0, 0 }, Task::SYNC });
      m_points.insert (m_points.end(), relevant_meshes.begin(), relevant_meshes.end());

      relevant_meshes.clear();
      relevant_chunks.clear();
    }
    else
    {
      // TODO: use correct algorithm for determining dependencies

      auto r = insert_if_unique(m_chunk_generation_list, iVec3{i.position + iVec3{0, 0, 0}});
      if (r) relevant_chunks.push_back(Job{ iVec3{ i.position + iVec3{ 0, 0, 0 } }, Task::LOAD_CHUNK});

      r = insert_if_unique(m_chunk_generation_list, iVec3{i.position + iVec3{0, 0, 1}});
      if (r) relevant_chunks.push_back(Job{ iVec3{ i.position + iVec3{ 0, 0, 1 } }, Task::LOAD_CHUNK});

      r = insert_if_unique(m_chunk_generation_list, iVec3{i.position + iVec3{0, 1, 0}});
      if (r) relevant_chunks.push_back(Job{ iVec3{ i.position + iVec3{ 0, 1, 0 } }, Task::LOAD_CHUNK});

      r = insert_if_unique(m_chunk_generation_list, iVec3{i.position + iVec3{0, 1, 1}});
      if (r) relevant_chunks.push_back(Job{ iVec3{ i.position + iVec3{ 0, 1, 1 } }, Task::LOAD_CHUNK});

      r = insert_if_unique(m_chunk_generation_list, iVec3{i.position + iVec3{1, 0, 0}});
      if (r) relevant_chunks.push_back(Job{ iVec3{ i.position + iVec3{ 1, 0, 0 } }, Task::LOAD_CHUNK});

      r = insert_if_unique(m_chunk_generation_list, iVec3{i.position + iVec3{1, 0, 1}});
      if (r) relevant_chunks.push_back(Job{ iVec3{ i.position + iVec3{ 1, 0, 1 } }, Task::LOAD_CHUNK});

      r = insert_if_unique(m_chunk_generation_list, iVec3{i.position + iVec3{1, 1, 0}});
      if (r) relevant_chunks.push_back(Job{ iVec3{ i.position + iVec3{ 1, 1, 0 } }, Task::LOAD_CHUNK});

      r = insert_if_unique(m_chunk_generation_list, iVec3{i.position + iVec3{1, 1, 1}});
      if (r) relevant_chunks.push_back(Job{ iVec3{ i.position + iVec3{ 1, 1, 1 } }, Task::LOAD_CHUNK});

      relevant_meshes.push_back(i);
    }
  }

  for (const auto & i : m_chunk_generation_list)
  {
    std::cout << std::setw(10)
              << std::setw(4) << i(0)
              << std::setw(4) << i(1)
              << std::setw(4) << i(2)
              << std::endl;
  }

  std::cout << "Final points: ---------------------------------" << std::endl;

  for (const auto & i : m_points)
  {
    std::cout << std::setw(10)
              << std::setw(4) << i.position(0)
              << std::setw(4) << i.position(1)
              << std::setw(4) << i.position(2)
              << " | " << std::setw(3) << square_distance(i.position(0), i.position(1), i.position(2))
              << " | " << static_cast<int>(i.task)
              << std::endl;
  }

  int dummy = 0;
}

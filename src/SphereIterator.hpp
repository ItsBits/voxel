#pragma once

#include <cstdint>
#include <cassert>
#include <algorithm>
#include <vector>
#include <iostream>
#include <iomanip>
#include <fstream>


#include "TinyAlgebra.hpp"
#include "TinyAlgebraExtensions.hpp"

// TODO: refactor
// TODO: replace vector by array
template<int32_t RADIUS, int32_t SYNC_REPETITIONS>
class SphereIterator
{
public:
    SphereIterator();

    enum class Task : int { SYNC = 0, LAST_SYNC_AND_LOAD_REGION = 1, GENERATE_CHUNK = 2, GENERATE_MESH = 3, END_MARKER = 4 };
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

template<int32_t RADIUS, int32_t SYNC_REPETITIONS>
SphereIterator<RADIUS, SYNC_REPETITIONS>::SphereIterator() // TODO: move template parameters to function parameters
{
    // TODO: include load region command xor (both can be done if it's guaranteed that all stay)
    // TODO: interleave GENERATE_CHUNK and GENERATE_MESH to reduce command queue load

    // TODO: for region, yust include range and exact regions should be calculated at runtime
    //       elementwise min and max for the range  (max == -min ?)

    const std::string file_name{"iterator/" + std::to_string(RADIUS) + "_" + std::to_string(SYNC_REPETITIONS)};
    std::ifstream input_file{file_name, std::ifstream::ate | std::ofstream::binary};
    if (input_file.good())
    {
        auto size = input_file.tellg();

        m_points.resize(size / sizeof(m_points[0]));

        input_file.seekg(0);
        input_file.read(reinterpret_cast<char *>(m_points.data()), size * sizeof(m_points[0]));
        //return;
        //goto PRINT_ITERATOR;
    }
    else
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
                const Node current = Node{x, y, z, square_distance(x, y, z)};

                if (current.d < too_far) nodes.push_back(current);
            }

    std::sort(nodes.begin(), nodes.end(), [&](const Node &a, const Node &b)
    { return a.d < b.d; });


    auto it = levels.begin();
    ++it;

    for (auto &i : nodes)
    {
        assert(it != levels.end());
        if (i.d >= *it) // all that are smaller than *it are kept
        {
            m_points_tmp.push_back({{0, 0, 0}, Task::SYNC});
            ++it;
        }

        m_points_tmp.push_back({{i.x, i.y, i.z}, Task::GENERATE_MESH});
    }
    // sync at the end
    m_points_tmp.push_back({{0, 0, 0}, Task::SYNC}); // why is this actually missing in the end product?
    // sync at the beginning
    m_points_tmp.insert(m_points_tmp.begin(), {{0, 0, 0}, Task::SYNC});

    /*for (const auto & i : m_points_tmp)
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
    }*/

    // TODO: iterator for chunk loading => get current mesh ring and add all chunks that are needed minus all chunks that have already been loaded
    //         -> this is just for chunk generation. aka. making sure that all chunks are generated before generating meshess -> reading chunks can be done without locking

    auto insert_if_unique = [](auto &container, auto data)
    {
        for (const auto &i : container)
            if (all(i == data))
                return false;

        container.push_back(data);
        return true;
    };


    std::vector<Job> relevant_meshes;
    std::vector<Job> relevant_chunks;

    // TODO: save result to file and reuse because the algorithm is O(n^2) and takes forever
    // TODO: verify correctness
    // TODO: try only doing positive octant (all other positions are mirrored (+++)(++-)(+-+)(+--)(-++)(-+-)(--+)(---)
    size_t re = m_points_tmp.size();
    const size_t one_percent = re / 100 == 0 ? 1 : re / 100;
    for (const auto &i : m_points_tmp)
    {
        if (--re % one_percent == 0)
            std::cout << re << std::endl;

        if (i.task == Task::SYNC)
        {
            m_chunk_generation_list.push_back(
                i.position + iVec3{1337, 1337, 1337}); // TODO: dummy please replace by SYNC

            /*
             * gen chunks, sync, gen meshes,
             * gen chunks, sync, gen meshes,
             * gen chunks, sync, gen meshes ...
             */
            m_points.insert(m_points.end(), relevant_chunks.begin(), relevant_chunks.end());

            static_assert(SYNC_REPETITIONS > 0, "Can't have no sync.");

            for (int32_t x = 0; x < SYNC_REPETITIONS - 1; ++x)
                m_points.push_back({iVec3{0, 0, 0}, Task::SYNC});

            m_points.push_back({iVec3{0, 0, 0}, Task::LAST_SYNC_AND_LOAD_REGION});

            m_points.insert(m_points.end(), relevant_meshes.begin(), relevant_meshes.end());

            relevant_meshes.clear();
            relevant_chunks.clear();
        } else
        {
            // TODO: use correct algorithm for determining dependencies
            // here is assumed that mesh_size == chunk_size AND mesh_offset < chunk_size AND mesh_offset > 0

            auto r = insert_if_unique(m_chunk_generation_list, iVec3{i.position + iVec3{0, 0, 0}});
            if (r) relevant_chunks.push_back(Job{iVec3{i.position + iVec3{0, 0, 0}}, Task::GENERATE_CHUNK});

            r = insert_if_unique(m_chunk_generation_list, iVec3{i.position + iVec3{0, 0, 1}});
            if (r) relevant_chunks.push_back(Job{iVec3{i.position + iVec3{0, 0, 1}}, Task::GENERATE_CHUNK});

            r = insert_if_unique(m_chunk_generation_list, iVec3{i.position + iVec3{0, 1, 0}});
            if (r) relevant_chunks.push_back(Job{iVec3{i.position + iVec3{0, 1, 0}}, Task::GENERATE_CHUNK});

            r = insert_if_unique(m_chunk_generation_list, iVec3{i.position + iVec3{0, 1, 1}});
            if (r) relevant_chunks.push_back(Job{iVec3{i.position + iVec3{0, 1, 1}}, Task::GENERATE_CHUNK});

            r = insert_if_unique(m_chunk_generation_list, iVec3{i.position + iVec3{1, 0, 0}});
            if (r) relevant_chunks.push_back(Job{iVec3{i.position + iVec3{1, 0, 0}}, Task::GENERATE_CHUNK});

            r = insert_if_unique(m_chunk_generation_list, iVec3{i.position + iVec3{1, 0, 1}});
            if (r) relevant_chunks.push_back(Job{iVec3{i.position + iVec3{1, 0, 1}}, Task::GENERATE_CHUNK});

            r = insert_if_unique(m_chunk_generation_list, iVec3{i.position + iVec3{1, 1, 0}});
            if (r) relevant_chunks.push_back(Job{iVec3{i.position + iVec3{1, 1, 0}}, Task::GENERATE_CHUNK});

            r = insert_if_unique(m_chunk_generation_list, iVec3{i.position + iVec3{1, 1, 1}});
            if (r) relevant_chunks.push_back(Job{iVec3{i.position + iVec3{1, 1, 1}}, Task::GENERATE_CHUNK});

            relevant_meshes.push_back(i);
        }
    }

    /*for (const auto & i : m_chunk_generation_list)
    {
      std::cout << std::setw(10)
                << std::setw(4) << i(0)
                << std::setw(4) << i(1)
                << std::setw(4) << i(2)
                << std::endl;
    }
  */
    std::cout << "Current points: ---------------------------------" << std::endl;

    for (const auto &i : m_points)
    {
        std::cout << std::setw(10)
                  << std::setw(4) << i.position(0)
                  << std::setw(4) << i.position(1)
                  << std::setw(4) << i.position(2)
                  << " | " << std::setw(3) << square_distance(i.position(0), i.position(1), i.position(2))
                  << " | " << static_cast<int>(i.task)
                  << std::endl;
    }

    // setup region ranges
    iVec3 min_range{0, 0, 0};
    iVec3 max_range{0, 0, 0};
    std::size_t last_sync_index = 0;
    std::size_t y = 0;

    // find first sync
    for (; y < m_points.size(); ++y)
        if (m_points[y].task == Task::LAST_SYNC_AND_LOAD_REGION)
            break;

    last_sync_index = y++;

    for (std::size_t x = y; x < m_points.size(); ++x)
    {
        const auto &i = m_points[x];

        if (i.task == Task::GENERATE_CHUNK)
        {
            min_range = min(min_range, i.position);
            max_range = max(max_range, i.position);
        } else if (i.task == Task::GENERATE_MESH)
        {
            // TODO: finish correct algorithm for determining dependencies
            // for now, the correct range is implicitly included in Task::GENERATE_CHUNK
        } else if (i.task == Task::LAST_SYNC_AND_LOAD_REGION)
        {
            // TODO: finish correct algorithm for determining dependencies
            const auto tmp = abs(min_range - 1);
            const auto r = all(tmp == max_range);
            const auto s = all(max_range >= iVec3{0, 0, 0});
            assert(r && "Not sure if this is a bug. Must select absolute max if not the same.");
            assert(s && "Max must be positive.");

            m_points[last_sync_index].position = max_range;

            min_range = iVec3{0, 0, 0};
            max_range = iVec3{0, 0, 0};
            last_sync_index = x;
        } else if (i.task == Task::SYNC)
        {
            // NO OP
        } else
        {
            assert(false && "Invalid command.");
        }
    }

    for (int32_t x = 0; x < SYNC_REPETITIONS - 1; ++x)
        m_points.push_back({iVec3{0, 0, 0}, Task::SYNC});
    m_points.push_back({iVec3{0, 0, 0}, Task::END_MARKER});


    int dummy = 0;

    //==========export==========================================================
    std::ofstream output_file{file_name, std::ofstream::trunc | std::ofstream::binary};
    output_file.write(reinterpret_cast<const char *>(m_points.data()), m_points.size() * sizeof(m_points[0]));
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
}

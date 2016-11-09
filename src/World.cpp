#include "World.hpp"
#include "QuadEBO.hpp"
#include <queue>
#include <cassert>
#include <cmath>
#include "TinyAlgebraExtensions.hpp"
#include "Debug.hpp"
#include "Profiler.hpp"
#include <cstring>
#include <fstream>

//==============================================================================
constexpr char World::WORLD_ROOT[];
constexpr iVec3 World::CHUNK_SIZES;
constexpr iVec3 World::CHUNK_CONTAINER_SIZES;
constexpr iVec3 World::REGION_CONTAINER_SIZES;
constexpr int World::META_DATA_SIZE;
constexpr iVec3 World::REGION_SIZES;
constexpr int World::MESH_BORDER_REQUIRED_SIZE;
constexpr unsigned char World::SHADDOW_STRENGTH;
constexpr iVec3 World::MESH_CONTAINER_SIZES;
constexpr iVec3 World::MESH_SIZES;
constexpr iVec3 World::MESH_OFFSETS;

//==============================================================================
World::World(const char * location) :
        m_data_location{ location },
        m_center{ { 0, 0, 0 }, { 0, 0, 0 } },
        m_back_buffer{ 0 },
        m_quit{ false },
        m_swap{ false },
        m_loader_waiting{ false },
        m_moved_far{ false },
        m_loader_finished{ false }
{
    for (auto & i : m_chunk_positions) i = { 0, 0, 0 };
    for (auto & i : m_mesh_positions) i = { 0, 0, 0 };
    m_chunk_positions[0] = { 1, 0, 0 };
    m_mesh_positions[0] = { 1, 0, 0 };

    for (auto & i : m_blocks) i = { 0 };
    for (auto & i : m_meshes) i = { 0, 0, 0 };

    for (auto & i : m_regions)
    {
        i.position = { 0, 0, 0 };
        for (auto & i2 : i.metas) i2 = { 0, 0 };
        i.data = nullptr;
        i.size = 0;
        i.container_size = 0;
    }
    m_regions[0].position = { 1, 0, 0 };

    for (auto & i : m_needs_save) i = false;

    m_loader_thread = std::thread{ &World::meshLoader, this };
}

//==============================================================================
World::~World()
{
    Debug::print(__func__, "Exiting loader thread.");

    exitLoaderThread();

    Debug::print(__func__, "Saving chunks.");

    // check all chunks if they need to be saved and save them
    for (auto chunk_index = 0; chunk_index < CHUNK_CONTAINER_SIZE; ++chunk_index)
        if (m_needs_save[chunk_index])
            saveChunkToRegion(chunk_index);

    // save all valid regions
    for (auto region_index = 0; region_index < REGION_CONTAINER_SIZE; ++region_index)
        saveRegionToDrive(region_index);

    Debug::print(__func__, "Cleaning up memory.");

    // cleanup
    for (auto & i : m_regions) std::free(i.data);

    // delete vertex and vao buffers from active meshes
    for (auto mesh_index = 0; mesh_index < MESH_CONTAINER_SIZE; ++mesh_index)
    {
        auto & mesh_data = m_meshes[mesh_index];

        // only both equal to 0 or both not equal to 0 is valid
        if (mesh_data.VAO == 0 && mesh_data.VBO == 0) continue;
        assert(mesh_data.VAO != 0 && mesh_data.VBO != 0 && "Active buffers should not be 0.");

        glDeleteVertexArrays(1, &mesh_data.VAO);
        glDeleteBuffers(1, &mesh_data.VBO);

        mesh_data.VAO = 0;
        mesh_data.VBO = 0;
        mesh_data.size = 0;
    }

    // delete vertex and vao buffers from unused meshes
    while (!m_unused_buffers.empty())
    {
        const auto & top = m_unused_buffers.top();

        assert(top.VAO != 0 && top.VBO != 0 && "Unused buffers should not be 0.");
        glDeleteVertexArrays(1, &top.VAO);
        glDeleteBuffers(1, &top.VBO);

        m_unused_buffers.pop();
    }
}

//==============================================================================
Block & World::getBlock(const iVec3 block_position)
{
    const auto relative_position = floorMod(block_position, CHUNK_SIZES);
    const auto chunk_position = floorDiv(block_position, CHUNK_SIZES);
    const auto chunk_relative = floorMod(chunk_position, CHUNK_CONTAINER_SIZES);

    const auto block_index = toIndex(relative_position, CHUNK_SIZES);

    const auto chunk_index = toIndex(chunk_relative, CHUNK_CONTAINER_SIZES);

    return m_blocks[chunk_index * CHUNK_SIZE + block_index];
}

//==============================================================================
int World::loadChunkRange(const iVec3 from_block, const iVec3 to_block)
{
    const auto chunk_position_from = floorDiv(from_block, CHUNK_SIZES);
    const auto chunk_position_to = floorDiv(to_block - 1, CHUNK_SIZES);

    iVec3 it;

    int chunks_loaded = 0;

    for (it(2) = chunk_position_from(2); it(2) <= chunk_position_to(2); ++it(2))
        for (it(1) = chunk_position_from(1); it(1) <= chunk_position_to(1); ++it(1))
            for (it(0) = chunk_position_from(0); it(0) <= chunk_position_to(0); ++it(0))
            {
                chunks_loaded += loadChunk(it);
            }

    Profiler::add(Profiler::Task::ChunksLoaded, chunks_loaded);
    return chunks_loaded;
}

//==============================================================================
void World::loadRegion(const iVec3 region_position)
{
    const auto region_relative = floorMod(region_position, REGION_CONTAINER_SIZES);
    const auto region_index = toIndex(region_relative, REGION_CONTAINER_SIZES);

    const auto old_position = m_regions[region_index].position;

    // return if already loaded
    if (all(old_position == region_position))
      return;

    // save existing region
    saveRegionToDrive(region_index);

    std::string in_file_name = WORLD_ROOT + toString(region_position);
    std::ifstream in_file{ in_file_name, std::ifstream::binary };
    if (in_file.good())
    {
        // load region from drive because it exists
        Debug::print(__func__, "Loading region ", toString(region_position));

        auto & region = m_regions[region_index];
        region.position = region_position;
        in_file.read(reinterpret_cast<char *>(&region.size), sizeof(int));
        in_file.read(reinterpret_cast<char *>(region.metas), META_DATA_SIZE);
        std::free(region.data);
        region.data = (Bytef *)std::malloc(static_cast<std::size_t>(region.size));
        in_file.read(reinterpret_cast<char *>(region.data), region.size);
        region.container_size = region.size;
    }
    else
    {
        // this is something similar to World constructor

        // create region file
        auto & region = m_regions[region_index];
        region.position = region_position;
        std::free(region.data);
        region.data = (Bytef*)std::malloc(static_cast<std::size_t>(REGION_DATA_SIZE_FACTOR));
        region.size = 0;
        region.container_size = REGION_DATA_SIZE_FACTOR;
        for (auto & i : region.metas) i = { 0, 0 };
    }
}

//==============================================================================
int World::loadChunk(const iVec3 chunk_position)
{
    const auto chunk_relative = floorMod(chunk_position, CHUNK_CONTAINER_SIZES);

    const auto chunk_index = toIndex(chunk_relative, CHUNK_CONTAINER_SIZES);

    // if already loaded
    if (all(m_chunk_positions[chunk_index] == chunk_position))
      return 0;

    const auto region_position = floorDiv(chunk_position, REGION_SIZES);
    const auto chunk_in_region_relative = floorMod(chunk_position, REGION_SIZES);
    const auto chunk_in_region_index = toIndex(chunk_in_region_relative, REGION_SIZES);

    const auto region_relative = floorMod(region_position, REGION_CONTAINER_SIZES);
    const auto region_index = toIndex(region_relative, REGION_CONTAINER_SIZES);

    // save previous chunk
    if (m_needs_save[chunk_index])
    {
        saveChunkToRegion(chunk_index);
    }

    // load region of new chunk
    loadRegion(region_position);

    // generate new chunk
    if (m_regions[region_index].metas[chunk_in_region_index].size == 0)
    {
        generateChunk(chunk_position * CHUNK_SIZES);
        m_chunk_positions[chunk_index] = chunk_position;
        m_needs_save[chunk_index] = true;
    }
    // load chunk from region
    else
    {
        auto * beginning_of_chunk = &getBlock(chunk_position * CHUNK_SIZES); // address of first block

        // load chunk from region
        uLongf destination_length = static_cast<uLongf>(SOURCE_LENGTH);
        const auto off = m_regions[region_index].metas[chunk_in_region_index].offset;
        const auto siz = m_regions[region_index].metas[chunk_in_region_index].size;

        const auto * source = m_regions[region_index].data + off;
        auto result = uncompress(
                reinterpret_cast<Bytef *>(beginning_of_chunk), &destination_length,
                source, static_cast<uLongf>(siz)
        );
        assert(result == Z_OK && destination_length == SOURCE_LENGTH && "Error in decompression.");

        m_chunk_positions[chunk_index] = chunk_position;
        m_needs_save[chunk_index] = false;
    }

    return 1;
}

//==============================================================================
std::vector<Vertex> World::generateMesh(const iVec3 from_block, const iVec3 to_block)
{
    const auto f = from_block - MESH_BORDER_REQUIRED_SIZE;
    const auto t = to_block + MESH_BORDER_REQUIRED_SIZE;

    Profiler::add(Profiler::Task::MeshesGenerated, 1);

    iVec3 position;
    std::vector<Vertex> mesh;

    // TODO: try to iterate over indices instead of coordinates and write functions indexPlus1x(), indexPlus1y(), indexPlus1z(), indexMinus1x(), indexMinus1y(),indexMinus1z()
    // TODO: and see if it is faster
    for (position(2) = from_block(2); position(2) < to_block(2); ++position(2))
        for (position(1) = from_block(1); position(1) < to_block(1); ++position(1))
            for (position(0) = from_block(0); position(0) < to_block(0); ++position(0))
            {
                auto block = getBlock(position);

                if (block.isEmpty()) continue;

                if (getBlock({ position(0) + 1, position(1), position(2) }).isEmpty())
                {
                    const bool aos[8]{
                            !getBlock({ position(0) + 1, position(1) - 1, position(2)     }).isEmpty(),
                            !getBlock({ position(0) + 1, position(1)    , position(2) - 1 }).isEmpty(),
                            !getBlock({ position(0) + 1, position(1) + 1, position(2)     }).isEmpty(),
                            !getBlock({ position(0) + 1, position(1)    , position(2) + 1 }).isEmpty(),

                            !getBlock({ position(0) + 1, position(1) - 1, position(2) - 1 }).isEmpty(),
                            !getBlock({ position(0) + 1, position(1) - 1, position(2) + 1 }).isEmpty(),
                            !getBlock({ position(0) + 1, position(1) + 1, position(2) + 1 }).isEmpty(),
                            !getBlock({ position(0) + 1, position(1) + 1, position(2) - 1 }).isEmpty()
                    };

                    const ucVec4 ao_result = static_cast<unsigned char>(UCHAR_MAX) - ucVec4{
                            vertAO(aos[0], aos[3], aos[5]),
                            vertAO(aos[2], aos[3], aos[6]),
                            vertAO(aos[0], aos[1], aos[4]),
                            vertAO(aos[2], aos[1], aos[7])
                    } * SHADDOW_STRENGTH;

                    mesh.push_back({ { position(0) + 1, position(1)    , position(2) + 1 }, block.get(), ao_result });
                    mesh.push_back({ { position(0) + 1, position(1)    , position(2)     }, block.get(), ao_result });
                    mesh.push_back({ { position(0) + 1, position(1) + 1, position(2)     }, block.get(), ao_result });
                    mesh.push_back({ { position(0) + 1, position(1) + 1, position(2) + 1 }, block.get(), ao_result });
                }
                if (getBlock({ position(0) - 1, position(1), position(2) }).isEmpty())
                {
                    const bool aos[8]{
                            !getBlock({ position(0) - 1, position(1) - 1, position(2)     }).isEmpty(),
                            !getBlock({ position(0) - 1, position(1)    , position(2) - 1 }).isEmpty(),
                            !getBlock({ position(0) - 1, position(1) + 1, position(2)     }).isEmpty(),
                            !getBlock({ position(0) - 1, position(1)    , position(2) + 1 }).isEmpty(),

                            !getBlock({ position(0) - 1, position(1) - 1, position(2) - 1 }).isEmpty(),
                            !getBlock({ position(0) - 1, position(1) - 1, position(2) + 1 }).isEmpty(),
                            !getBlock({ position(0) - 1, position(1) + 1, position(2) + 1 }).isEmpty(),
                            !getBlock({ position(0) - 1, position(1) + 1, position(2) - 1 }).isEmpty()
                    };

                    const ucVec4 ao_result = static_cast<unsigned char>(UCHAR_MAX) - ucVec4{
                            vertAO(aos[0], aos[1], aos[4]),
                            vertAO(aos[2], aos[1], aos[7]),
                            vertAO(aos[0], aos[3], aos[5]),
                            vertAO(aos[2], aos[3], aos[6])
                    } * SHADDOW_STRENGTH;

                    mesh.push_back({ { position(0)    , position(1)    , position(2)     }, block.get(), ao_result });
                    mesh.push_back({ { position(0)    , position(1)    , position(2) + 1 }, block.get(), ao_result });
                    mesh.push_back({ { position(0)    , position(1) + 1, position(2) + 1 }, block.get(), ao_result });
                    mesh.push_back({ { position(0)    , position(1) + 1, position(2)     }, block.get(), ao_result });
                }
                if (getBlock({ position(0), position(1) + 1, position(2) }).isEmpty())
                {
                    const bool aos[8]{
                            !getBlock({ position(0) - 1, position(1) + 1, position(2)     }).isEmpty(),
                            !getBlock({ position(0)    , position(1) + 1, position(2) - 1 }).isEmpty(),
                            !getBlock({ position(0) + 1, position(1) + 1, position(2)     }).isEmpty(),
                            !getBlock({ position(0)    , position(1) + 1, position(2) + 1 }).isEmpty(),

                            !getBlock({ position(0) - 1, position(1) + 1, position(2) - 1 }).isEmpty(),
                            !getBlock({ position(0) - 1, position(1) + 1, position(2) + 1 }).isEmpty(),
                            !getBlock({ position(0) + 1, position(1) + 1, position(2) + 1 }).isEmpty(),
                            !getBlock({ position(0) + 1, position(1) + 1, position(2) - 1 }).isEmpty()
                    };

                    const ucVec4 ao_result = static_cast<unsigned char>(UCHAR_MAX) - ucVec4{
                            vertAO(aos[2], aos[1], aos[7]),
                            vertAO(aos[2], aos[3], aos[6]),
                            vertAO(aos[0], aos[1], aos[4]),
                            vertAO(aos[0], aos[3], aos[5])
                    } * SHADDOW_STRENGTH;

                    mesh.push_back({ { position(0) + 1, position(1) + 1, position(2)     }, block.get(), ao_result });
                    mesh.push_back({ { position(0)    , position(1) + 1, position(2)     }, block.get(), ao_result });
                    mesh.push_back({ { position(0)    , position(1) + 1, position(2) + 1 }, block.get(), ao_result });
                    mesh.push_back({ { position(0) + 1, position(1) + 1, position(2) + 1 }, block.get(), ao_result });
                }
                if (getBlock({ position(0), position(1) - 1, position(2) }).isEmpty())
                {
                    const bool aos[8]{
                            !getBlock({ position(0) - 1, position(1) - 1, position(2)     }).isEmpty(),
                            !getBlock({ position(0)    , position(1) - 1, position(2) - 1 }).isEmpty(),
                            !getBlock({ position(0) + 1, position(1) - 1, position(2)     }).isEmpty(),
                            !getBlock({ position(0)    , position(1) - 1, position(2) + 1 }).isEmpty(),

                            !getBlock({ position(0) - 1, position(1) - 1, position(2) - 1 }).isEmpty(),
                            !getBlock({ position(0) - 1, position(1) - 1, position(2) + 1 }).isEmpty(),
                            !getBlock({ position(0) + 1, position(1) - 1, position(2) + 1 }).isEmpty(),
                            !getBlock({ position(0) + 1, position(1) - 1, position(2) - 1 }).isEmpty()
                    };

                    const ucVec4 ao_result = static_cast<unsigned char>(UCHAR_MAX) - ucVec4{
                            vertAO(aos[0], aos[1], aos[4]),
                            vertAO(aos[0], aos[3], aos[5]),
                            vertAO(aos[2], aos[1], aos[7]),
                            vertAO(aos[2], aos[3], aos[6])
                    } * SHADDOW_STRENGTH;

                    mesh.push_back({ { position(0)    , position(1)    , position(2)     }, block.get(), ao_result });
                    mesh.push_back({ { position(0) + 1, position(1)    , position(2)     }, block.get(), ao_result });
                    mesh.push_back({ { position(0) + 1, position(1)    , position(2) + 1 }, block.get(), ao_result });
                    mesh.push_back({ { position(0)    , position(1)    , position(2) + 1 }, block.get(), ao_result });
                }
                if (getBlock({ position(0), position(1), position(2) + 1 }).isEmpty())
                {
                    const bool aos[8]{
                            !getBlock({ position(0) - 1, position(1)    , position(2) + 1 }).isEmpty(),
                            !getBlock({ position(0)    , position(1) - 1, position(2) + 1 }).isEmpty(),
                            !getBlock({ position(0) + 1, position(1)    , position(2) + 1 }).isEmpty(),
                            !getBlock({ position(0)    , position(1) + 1, position(2) + 1 }).isEmpty(),

                            !getBlock({ position(0) - 1, position(1) - 1, position(2) + 1 }).isEmpty(),
                            !getBlock({ position(0) - 1, position(1) + 1, position(2) + 1 }).isEmpty(),
                            !getBlock({ position(0) + 1, position(1) + 1, position(2) + 1 }).isEmpty(),
                            !getBlock({ position(0) + 1, position(1) - 1, position(2) + 1 }).isEmpty()
                    };

                    const ucVec4 ao_result = static_cast<unsigned char>(UCHAR_MAX) - ucVec4{
                            vertAO(aos[0], aos[1], aos[4]),
                            vertAO(aos[0], aos[3], aos[5]),
                            vertAO(aos[2], aos[1], aos[7]),
                            vertAO(aos[2], aos[3], aos[6])
                    } * SHADDOW_STRENGTH;

                    mesh.push_back({ { position(0)    , position(1)    , position(2) + 1 }, block.get(), ao_result });
                    mesh.push_back({ { position(0) + 1, position(1)    , position(2) + 1 }, block.get(), ao_result });
                    mesh.push_back({ { position(0) + 1, position(1) + 1, position(2) + 1 }, block.get(), ao_result });
                    mesh.push_back({ { position(0)    , position(1) + 1, position(2) + 1 }, block.get(), ao_result });
                }
                if (getBlock({ position(0), position(1), position(2) - 1 }).isEmpty())
                {
                    const bool aos[8]{
                            !getBlock({ position(0) - 1, position(1)    , position(2) - 1 }).isEmpty(),
                            !getBlock({ position(0)    , position(1) - 1, position(2) - 1 }).isEmpty(),
                            !getBlock({ position(0) + 1, position(1)    , position(2) - 1 }).isEmpty(),
                            !getBlock({ position(0)    , position(1) + 1, position(2) - 1 }).isEmpty(),

                            !getBlock({ position(0) - 1, position(1) - 1, position(2) - 1 }).isEmpty(),
                            !getBlock({ position(0) - 1, position(1) + 1, position(2) - 1 }).isEmpty(),
                            !getBlock({ position(0) + 1, position(1) + 1, position(2) - 1 }).isEmpty(),
                            !getBlock({ position(0) + 1, position(1) - 1, position(2) - 1 }).isEmpty()
                    };

                    const ucVec4 ao_result = static_cast<unsigned char>(UCHAR_MAX) - ucVec4{
                            vertAO(aos[2], aos[1], aos[7]),
                            vertAO(aos[2], aos[3], aos[6]),
                            vertAO(aos[0], aos[1], aos[4]),
                            vertAO(aos[0], aos[3], aos[5])
                    } * SHADDOW_STRENGTH;

                    mesh.push_back({ { position(0) + 1, position(1)    , position(2)     }, block.get(), ao_result });
                    mesh.push_back({ { position(0)    , position(1)    , position(2)     }, block.get(), ao_result });
                    mesh.push_back({ { position(0)    , position(1) + 1, position(2)     }, block.get(), ao_result });
                    mesh.push_back({ { position(0) + 1, position(1) + 1, position(2)     }, block.get(), ao_result });
                }
            }

    return mesh;
}

//==============================================================================
unsigned char World::vertAO(const bool side_a, const bool side_b, const bool corner)
{
  if (side_a && side_b) return 3;

  return
          static_cast<unsigned char>(side_a) +
          static_cast<unsigned char>(side_b) +
          static_cast<unsigned char>(corner);
}

//==============================================================================
void World::generateChunk(const iVec3 from_block)
{
    sineChunk(from_block);
    //debugChunk(from_block);
}

//==============================================================================
void World::debugChunk(const iVec3 from_block)
{
    const iVec3 to_block = from_block + CHUNK_SIZES;

    iVec3 position;

    const auto pos_maybe = floorDiv(from_block, CHUNK_SIZES);

    for (position(2) = from_block(2); position(2) < to_block(2); ++position(2))
        for (position(1) = from_block(1); position(1) < to_block(1); ++position(1))
            for (position(0) = from_block(0); position(0) < to_block(0); ++position(0))
            {
                Block b = 0;

                const auto p = position(0) - from_block(0);

                if (p < pos_maybe(0)) b = 1;
                if (position(1) != 0 || position(2) != from_block(2)) b = 0;
                if (pos_maybe(0) < 0) b = 0;

                getBlock(position) = b;
            }
}

//==============================================================================
void World::sineChunk(const iVec3 from_block)
{
  const iVec3 to_block = from_block + CHUNK_SIZES;

  iVec3 position;

  for (position(2) = from_block(2); position(2) < to_block(2); ++position(2))
    for (position(1) = from_block(1); position(1) < to_block(1); ++position(1))
      for (position(0) = from_block(0); position(0) < to_block(0); ++position(0))
      {
        auto & block = getBlock(position);
#if 0
        block = Block{ std::rand() % 300 == 0 };
#else
        if (std::sin(position(0) * 0.1f) * std::sin(position(2) * 0.1f) * 10.0f > static_cast<float>(position(1)))
          std::rand() % 2 ? block = Block{ 1 } : block = Block{ 2 };
        else
          block = Block{ 0 };
#endif
      }
}

//==============================================================================
void World::meshLoader()
{
    while (!m_quit)
    {
        Profiler::resetAll();
        Debug::print(__func__, "New loader thread loop.");

        Tasks & tasks = m_tasks[m_back_buffer];
        const iVec3 center = m_center[m_back_buffer];

        // reset
        tasks.remove.clear();
        tasks.render.clear();
        tasks.upload.clear();

        for (auto & i : m_mesh_loaded) i = Status::UNLOADED;

        // update remove and render in task list
        std::size_t count = m_loaded_meshes.size();
        Debug::print(__func__, "Loaded meshes count: ", count);
        for (std::size_t i = 0; i < count;)
        {
            const iVec3 mesh_center = m_loaded_meshes[i].position * MESH_SIZES + MESH_OFFSETS + (MESH_SIZES / 2);

            const auto mesh_relative = floorMod(m_loaded_meshes[i].position, MESH_CONTAINER_SIZES);
            const auto mesh_index = toIndex(mesh_relative, MESH_CONTAINER_SIZES);

            if (!inRange(center, mesh_center, SQUARE_REMOVE_DISTANCE))
            {
                if (!m_loaded_meshes[i].empty)
                    tasks.remove.push_back({ mesh_index });
                m_loaded_meshes[i] = m_loaded_meshes[--count];
            }
            else
            {
                if (!m_loaded_meshes[i].empty)
                    tasks.render.push_back({ mesh_index, m_loaded_meshes[i].position });
                m_mesh_loaded[mesh_index] = Status::LOADED;
                ++i;
            }

        }
        m_loaded_meshes.resize(count);

        // find meshes to generate and generate them
        std::queue<iVec3> check_list;
        // add center mesh
        const auto center_mesh = floorDiv(center - MESH_OFFSETS, MESH_SIZES);
        check_list.push(center_mesh);

        // breadth first search finds all borders of loaded are and loads it
        while (!check_list.empty() && tasks.upload.size() < MESH_COUNT_NEEDED_FOR_RESET && !m_moved_far)
        {
            const auto current = check_list.front();
            check_list.pop();
            const auto current_index = absoluteToIndex(current, MESH_CONTAINER_SIZES);

            if (m_mesh_loaded[current_index] == Status::CHECKED)
            {
                continue;
            }
            // load
            else if (m_mesh_loaded[current_index] == Status::UNLOADED)
            {
                const auto from_block = current * MESH_SIZES + MESH_OFFSETS;
                const auto to_block = from_block + MESH_SIZES;
                // TODO: paralelize next two lines?
                loadChunkRange(from_block - MESH_BORDER_REQUIRED_SIZE, to_block + MESH_BORDER_REQUIRED_SIZE);
                const auto mesh = generateMesh(from_block, to_block);

                if (mesh.size() > 0) tasks.upload.push_back({ current_index, current, mesh });

                m_mesh_loaded[current_index] = Status::CHECKED;
                m_loaded_meshes.push_back({ current, mesh.size() == 0 });
            }
            // push neighbours that are in render radius
            else if (m_mesh_loaded[current_index] == Status::LOADED)
            {
                const iVec3 neighbours[6]{
                        current + iVec3{ 1, 0, 0 }, current + iVec3{ 0, 1, 0 }, current + iVec3{ 0, 0, 1 },
                        current - iVec3{ 1, 0, 0 }, current - iVec3{ 0, 1, 0 }, current - iVec3{ 0, 0, 1 },
                };

                for (const iVec3 * pos = neighbours; pos < neighbours + 6; ++pos)
                    if (inRange(center, *pos * MESH_SIZES + MESH_OFFSETS + (MESH_SIZES / 2), SQUARE_RENDER_DISTANCE))
                        check_list.push(*pos);
            }
            else
            {
               assert(0);
            }
            m_mesh_loaded[current_index] = Status::CHECKED;
        }

        if (m_moved_far)
            Debug::print(__func__, "Resetting because moved far.");

        Debug::print(__func__, "Render: ", tasks.render.size());
        Debug::print(__func__, "Upload: ", tasks.upload.size());
        Debug::print(__func__, "Remove: ", tasks.remove.size());
        {
            // request task buffer swap wait for it
            std::unique_lock<std::mutex> lock{ m_lock };
            m_loader_waiting = true;
            m_cond_var.wait(lock, [this] { return m_swap || m_quit; });
            m_swap = false;
        }

        // TODO: increase the ratio by generating meshes in bulk
        const int loaded_c = Profiler::get(Profiler::Task::ChunksLoaded);
        const int loaded_m = Profiler::get(Profiler::Task::MeshesGenerated);
        Debug::printAlways(__func__,
                     "Chunks loaded: ", loaded_c,
                     " Meshes generated: ", loaded_m,
                     " Ratio: ", static_cast<float>(loaded_m) / static_cast<float>(loaded_c)
        );
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    Debug::print(__func__, "Exited loader thread.");
    m_loader_finished = true;
}

//==============================================================================
bool World::inRange(const iVec3 center_block, const iVec3 position_block, const int square_max_distance)
{
    const iVec3 distances = position_block - center_block;
    const int square_distance = dot(distances, distances);

    return square_distance <= square_max_distance;
}

//==============================================================================
void World::draw(const iVec3 new_center, const fVec4 frustum_planes[6])
{
    Tasks & tasks = m_tasks[(m_back_buffer + 1) % 2];
    m_center[(m_back_buffer + 1) % 2] = new_center;

    const auto delta_movement = new_center - m_center[m_back_buffer];
    const auto square_distance = dot(delta_movement, delta_movement);
    const auto moved_far = square_distance >= SQUARE_LOAD_RESET_DISTANCE;
    m_moved_far = m_moved_far || moved_far; // set as soon as outside range once

    // remove
    if (!tasks.remove.empty())
    {
        const Remove & task = tasks.remove.back();

        auto & mesh_data = m_meshes[task.index];

#if 1
      assert(mesh_data.VBO && mesh_data.VAO && "Should not be 0.");
      m_unused_buffers.push({ mesh_data.VAO, mesh_data.VBO });
#else
        glDeleteBuffers(1, &mesh_data.VBO);
        glDeleteVertexArrays(1, &mesh_data.VAO);
#endif

        mesh_data.VAO = 0;
        mesh_data.VBO = 0;

        tasks.remove.pop_back();
    }

    // upload only after nothing left to remove
    if (!tasks.upload.empty() && tasks.remove.empty())
    {
        const Upload & task = tasks.upload.back();

        GLuint VAO = 0, VBO = 0;
        if (!m_unused_buffers.empty())
        {
            const auto & top = m_unused_buffers.top();

            VAO = top.VAO;
            VBO = top.VBO;

            glBindBuffer(GL_ARRAY_BUFFER, VBO);

            m_unused_buffers.pop();
        }
        else
        {
          glGenVertexArrays(1, &VAO);
          glGenBuffers(1, &VBO);

          glBindVertexArray(VAO);
          glBindBuffer(GL_ARRAY_BUFFER, VBO);
          QuadEBO::bind();

          glVertexAttribIPointer(0, 3, GL_INT, sizeof(Vertex), (GLvoid*)(0));
          glVertexAttribIPointer(1, 1, GL_INT, sizeof(Vertex), (GLvoid*)(sizeof(Vertex::position)));
          glVertexAttribIPointer(2, 4, GL_UNSIGNED_BYTE, sizeof(Vertex), (GLvoid*)(sizeof(Vertex::position) + sizeof(Vertex::type)));
          glEnableVertexAttribArray(0);
          glEnableVertexAttribArray(1);
          glEnableVertexAttribArray(2);

          glBindVertexArray(0);
        }

        assert(task.mesh.size() > 0 && "Mesh size must be over 0.");
        assert(VAO != 0 && VBO != 0 && "Failed to gent VAO and VBO for mesh.");

        // upload mesh
        glBufferData(GL_ARRAY_BUFFER, task.mesh.size() * sizeof(task.mesh[0]), task.mesh.data(), GL_STATIC_DRAW);

        assert(m_meshes[task.index].VAO == 0 && m_meshes[task.index].VBO == 0 && "Buffers not cleaned up.");
        m_meshes[task.index].VAO = VAO;
        m_meshes[task.index].VBO = VBO;

        tasks.render.push_back({ task.index, task.position });

        // fast multiply by 1.5
        const int EBO_size = static_cast<int>((task.mesh.size() >> 1) + task.mesh.size());
        m_meshes[task.index].size = EBO_size;
        QuadEBO::resize(EBO_size);

        tasks.upload.pop_back();
    }

    // render
    for (auto & m : tasks.render)
    {
        // only render if not too far away
        // TODO: could be combined with frustum culling (make far frustum sqrt(SQUARE_RENDER_DISTACE) away)
        static_assert(!(MESH_SIZE_X % 2 || MESH_SIZE_Y % 2 || MESH_SIZE_Z % 2), "Assuming even mesh sizes");
        if (!inRange(new_center, m.position * MESH_SIZES + MESH_OFFSETS + (MESH_SIZES / 2), SQUARE_RENDER_DISTANCE))
            continue;

        if (meshInFrustum(frustum_planes, m.position * MESH_SIZES))
        {
            const auto & mesh_data = m_meshes[m.index];

            assert(mesh_data.size <= QuadEBO::size() && mesh_data.size > 0);
            assert(mesh_data.VAO != 0 && mesh_data.VBO != 0 && "VAO and VBO not loaded.");

            glBindVertexArray(mesh_data.VAO);
            glDrawElements(GL_TRIANGLES, mesh_data.size, QuadEBO::type(), 0);
            glBindVertexArray(0);
        }

    }

    // swap task buffers if loader ready
    if (tasks.upload.empty() && tasks.remove.empty())
    {
        std::unique_lock<std::mutex> lock{ m_lock };
        if (m_loader_waiting)
        {
            m_back_buffer = (m_back_buffer + 1) % 2;
            m_swap = true;
            m_loader_waiting = false;
            m_moved_far = false;
            m_cond_var.notify_all();
        }
    }
}

//==============================================================================
void World::exitLoaderThread()
{
    if (!m_loader_thread.joinable())
        return;

    // ugly loader thread exit
    m_quit = true;
    while (true)
    {
        {
            std::unique_lock<std::mutex> lock{ m_lock };
            if (m_loader_waiting || m_loader_finished)
            {
                m_cond_var.notify_all();
                break;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    m_loader_thread.join();
}

//==============================================================================
bool World::meshInFrustum(const fVec4 planes[6], const iVec3 mesh_offset)
{
    const fVec3 from{ static_cast<float>(mesh_offset(0)), static_cast<float>(mesh_offset(1)), static_cast<float>(mesh_offset(2)) };
    const fVec3 to{ from + fVec3{ static_cast<float>(MESH_SIZES(0)), static_cast<float>(MESH_SIZES(1)), static_cast<float>(MESH_SIZES(2)) } };

    // TODO: there are faster algorithms
    for (auto i = 0; i < 6; ++i)
    {
#if 0 // assume sphere ?
        float side = planePointDistance(plane[i], point);
        if (side < -Chunk::BOUNDING_SPHERE_RADIUS)
        return false;
#else
        // Reset counters for corners in and out
        int out = 0;
        int in = 0;

        // https://sites.google.com/site/letsmakeavoxelengine/home/frustum-culling

        if (dot(planes[i], fVec4{ from(0), from(1), from(2), 1 }) < 0.0f) ++out;
        else ++in;
        if (dot(planes[i], fVec4{ from(0), from(1), to(2), 1 }) < 0.0f) ++out;
        else ++in;
        if (dot(planes[i], fVec4{ from(0), to(1), from(2), 1 }) < 0.0f) ++out;
        else ++in;
        if (dot(planes[i], fVec4{ from(0), to(1), to(2), 1 }) < 0.0f) ++out;
        else ++in;
        if (dot(planes[i], fVec4{ to(0), from(1), from(2), 1 }) < 0.0f) ++out;
        else ++in;
        if (dot(planes[i], fVec4{ to(0), from(1), to(2), 1 }) < 0.0f) ++out;
        else ++in;
        if (dot(planes[i], fVec4{ to(0), to(1), from(2), 1 }) < 0.0f) ++out;
        else ++in;
        if (dot(planes[i], fVec4{ to(0), to(1), to(2), 1 }) < 0.0f) ++out;
        else ++in;

        // If all corners are out
        if (in == 0) return false;
#endif
    }

    return true;
}

//==============================================================================
void World::saveChunkToRegion(const int chunk_index)
{
    // load correct region file
    const auto previous_chunk_position = m_chunk_positions[chunk_index];
    const auto previous_region_position = floorDiv(previous_chunk_position, REGION_SIZES);
    loadRegion(previous_region_position);

    const auto previous_region_relative = floorMod(previous_region_position, REGION_CONTAINER_SIZES);
    const auto previous_region_index = toIndex(previous_region_relative, REGION_CONTAINER_SIZES);

    const auto previous_chunk_in_region_relative = floorMod(previous_chunk_position, REGION_SIZES);
    const auto previous_chunk_in_region_index = toIndex(previous_chunk_in_region_relative, REGION_SIZES);

    // compress chunk
    uLong destination_length = compressBound(static_cast<uLong>(SOURCE_LENGTH)); // compressBound could be static

    // resize if potentially out of space
    if (m_regions[previous_region_index].size + static_cast<int>(destination_length) > m_regions[previous_region_index].container_size)
    {
        Debug::print(__func__, "Reallocating region container.");
        m_regions[previous_region_index].container_size += REGION_DATA_SIZE_FACTOR < static_cast<int>(destination_length) ? static_cast<int>(destination_length) : REGION_DATA_SIZE_FACTOR;
        m_regions[previous_region_index].data = (Bytef*)std::realloc(m_regions[previous_region_index].data, static_cast<std::size_t>(m_regions[previous_region_index].container_size));
    }

    const auto * beginning_of_chunk = &getBlock(previous_chunk_position * CHUNK_SIZES); // address of first block
    Bytef * destination = m_regions[previous_region_index].data + m_regions[previous_region_index].size;

    // TODO: checkout other compression libraries that are faster
    // compress and save at once data to region
    auto result = compress2(destination, &destination_length, reinterpret_cast<const Bytef *>(beginning_of_chunk), static_cast<uLong>(SOURCE_LENGTH), Z_BEST_SPEED);

    assert(result == Z_OK && "Error compressing chunk.");
    assert(destination_length <= compressBound(static_cast<uLong>(SOURCE_LENGTH)) && "ZLib lied about the maximum possible size of compressed data.");

    m_regions[previous_region_index].metas[previous_chunk_in_region_index].size = static_cast<int>(destination_length);
    m_regions[previous_region_index].metas[previous_chunk_in_region_index].offset = m_regions[previous_region_index].size;

    m_regions[previous_region_index].size += static_cast<int>(destination_length);
}

//==============================================================================
void World::saveRegionToDrive(const int region_index)
{
    const auto position = m_regions[region_index].position;

    // only if valid
    if (
            (region_index == 0 && all(position == iVec3{ 0, 0, 0 })) ||
            (region_index != 0 && !all(position == iVec3{ 0, 0, 0 }))
        )
    {
        // save old region
        Debug::print(__func__, "Saving region ", toString(position));
        assert(m_regions[region_index].data != nullptr && "No idea why this can happen.");

        std::string file_name = WORLD_ROOT + toString(position);
        std::ofstream file{ file_name, std::ofstream::out | std::ofstream::binary | std::ofstream::trunc };

        file.write(reinterpret_cast<const char *>(&m_regions[region_index].size), sizeof(int));
        file.write(reinterpret_cast<const char *>(m_regions[region_index].metas), META_DATA_SIZE);
        file.write(reinterpret_cast<const char *>(m_regions[region_index].data), m_regions[region_index].size);
    }
}

#include "World.hpp"
#include "QuadEBO.hpp"
#include <cassert>
#include <cmath>
#include "TinyAlgebraExtensions.hpp"
#include "Debug.hpp"
#include "Profiler.hpp"
#include <cstring>
#include <fstream>
//#include "Settings.hpp"

//==============================================================================
constexpr char World::WORLD_ROOT[];
constexpr char World::MESH_CACHE_ROOT[];
constexpr iVec3 World::CHUNK_SIZES;
constexpr iVec3 World::CHUNK_CONTAINER_SIZES;
constexpr iVec3 World::REGION_CONTAINER_SIZES;
constexpr iVec3 World::MESH_REGION_CONTAINER_SIZES;
constexpr iVec3 World::MESH_REGION_SIZES;
constexpr int World::META_DATA_SIZE;
constexpr iVec3 World::REGION_SIZES;
constexpr int World::MESH_BORDER_REQUIRED_SIZE;
constexpr unsigned char World::SHADDOW_STRENGTH;
constexpr iVec3 World::MESH_CONTAINER_SIZES;
constexpr iVec3 World::MESH_SIZES;
constexpr iVec3 World::MESH_OFFSETS;
constexpr int World::SLEEP_MS;

//==============================================================================
World::World() :
        m_reference_center{ 0, 0, 0 },
        m_center{ { 0, 0, 0 } },
        m_quit{ false },
        m_moved_far{ false }
{
    for (auto & i : m_chunk_positions) i = { 0, 0, 0 };
    for (auto & i : m_mesh_positions) i = { 0, 0, 0 };
    m_chunk_positions[0] = { 1, 0, 0 };
    m_mesh_positions[0] = { 1, 0, 0 };

    for (auto & i : m_blocks) i = { 0 };

    for (auto & i : m_regions)
    {
        i.position = { 0, 0, 0 };
        for (auto & i2 : i.metas) i2 = { 0, 0 };
        i.data = nullptr;
        i.size = 0;
        i.container_size = 0;
        i.needs_save = false;
    }
    m_regions[0].position = { 1, 0, 0 };

    for (auto & i : m_mesh_cache_infos)
    {
        i.position = { 0, 0, 0 };
        for (auto & s : i.statuses) s = MeshCache::Status::UNKNOWN;
        i.needs_save = false;
        for (auto & s : i.decompressed_size) s = 0;
        for (auto & s : i.compressed_size) s = 0;
        for (auto & s : i.offset) s = 0;
        i.data = nullptr;
    }
    m_mesh_cache_infos[0].position = { 1, 0, 0 };

    for (auto & i : m_needs_save) i = false;
    for (auto & i : m_mesh_loaded) i = Status::UNLOADED;

    //std::atomic_thread_fence(std::memory_order_seq_cst);
    m_loader_thread = std::thread{ &World::meshLoader, this };
}

//==============================================================================
World::~World()
{
    Debug::print("Exiting loader thread.");

    exitLoaderThread();

    Debug::print("Saving unsaved chunks.");

    // check all chunks if they need to be saved and save them
    for (auto chunk_index = 0; chunk_index < CHUNK_CONTAINER_SIZE; ++chunk_index)
        if (m_needs_save[chunk_index])
            saveChunkToRegion(chunk_index);

    // save all valid regions
    for (auto region_index = 0; region_index < REGION_CONTAINER_SIZE; ++region_index)
        saveRegionToDrive(region_index);

    // save all mesh caches to drive
    for (auto mesh_cache_index = 0; mesh_cache_index < MESH_REGION_CONTAINER_SIZE; ++mesh_cache_index)
        saveMeshCacheToDrive(mesh_cache_index);

    Debug::print("Cleaning up memory.");

    // cleanup
    for (auto & i : m_regions) std::free(i.data);

    // delete vertex and vao buffers from active meshes
    const auto * i = m_meshes.begin();
    const auto * end = m_meshes.end();
    for (; i != end; ++i)
    {
        auto & mesh_data = i->data.mesh;

        // only both equal to 0 or both not equal to 0 is valid
        if (mesh_data.VAO == 0 && mesh_data.VBO == 0) continue;
        assert(mesh_data.VAO != 0 && mesh_data.VBO != 0 && "Active buffers should not be 0.");

        glDeleteVertexArrays(1, &mesh_data.VAO);
        glDeleteBuffers(1, &mesh_data.VBO);

        m_meshes.reset();
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
World::MeshCache::Status World::meshStatus(const iVec3 mesh_position)
{
    const auto mesh_cache_position = floorDiv(mesh_position, MESH_REGION_SIZES);

    const auto mesh_in_mesh_cache_relative = floorMod(mesh_position, MESH_REGION_SIZES);
    const auto mesh_in_mesh_cache_index = toIndex(mesh_in_mesh_cache_relative, MESH_REGION_SIZES);

    const auto mesh_cache_relative = floorMod(mesh_cache_position, MESH_REGION_CONTAINER_SIZES);
    const auto mesh_cache_index = toIndex(mesh_cache_relative, MESH_REGION_CONTAINER_SIZES);

    const auto & mesh_cache = m_mesh_cache_infos[mesh_cache_index];

    if (!all(mesh_cache.position == mesh_cache_position))
        loadMeshCache(mesh_cache_position);

    return mesh_cache.statuses[mesh_in_mesh_cache_index];
}

//==============================================================================
/*void World::setMeshStatus(const iVec3 mesh_position, const MeshCache::Status new_status)
{
    const auto mesh_cache_position = floorDiv(mesh_position, MESH_REGION_SIZES);

    const auto mesh_in_mesh_cache_relative = floorMod(mesh_position, MESH_REGION_SIZES);
    const auto mesh_in_mesh_cache_index = toIndex(mesh_in_mesh_cache_relative, MESH_REGION_SIZES);

    const auto mesh_cache_relative = floorMod(mesh_cache_position, MESH_REGION_CONTAINER_SIZES);
    const auto mesh_cache_index = toIndex(mesh_cache_relative, MESH_REGION_CONTAINER_SIZES);

    auto & mesh_cache = m_mesh_cache_infos[mesh_cache_index];

    if (!all(mesh_cache.position == mesh_cache_position))
        loadMeshCache(mesh_cache_position);

    assert(mesh_cache.statuses[mesh_in_mesh_cache_index] != new_status && "Could indicate a bug.");
    mesh_cache.statuses[mesh_in_mesh_cache_index] = new_status;
    m_mesh_cache_infos[mesh_cache_index].needs_save = true;
}*/

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
        Debug::print("Loading region ", toString(region_position));

        auto & region = m_regions[region_index];
        region.position = region_position;
        in_file.read(reinterpret_cast<char *>(&region.size), sizeof(int));
        in_file.read(reinterpret_cast<char *>(region.metas), META_DATA_SIZE);
        std::free(region.data);
        region.data = (Bytef *)std::malloc(static_cast<std::size_t>(region.size));
        in_file.read(reinterpret_cast<char *>(region.data), region.size);
        region.container_size = region.size;

        if (!in_file.good()) std::runtime_error("Reading file failed.");
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
void World::loadMeshCache(const iVec3 mesh_cache_position)
{
    const auto mesh_cache_relative = floorMod(mesh_cache_position, MESH_REGION_CONTAINER_SIZES);
    const auto mesh_cache_index = toIndex(mesh_cache_relative, MESH_REGION_CONTAINER_SIZES);

    const auto old_position = m_mesh_cache_infos[mesh_cache_index].position;

    // return if already loaded
    if (all(old_position == mesh_cache_position))
        return;

    saveMeshCacheToDrive(mesh_cache_index);

    std::string in_file_name = MESH_CACHE_ROOT + toString(mesh_cache_position);
    std::ifstream in_file{ in_file_name, std::ifstream::binary };
    if (in_file.good())
    {
        Debug::print("Loading mesh cache ", toString(mesh_cache_position));

        auto & cache = m_mesh_cache_infos[mesh_cache_index];
        cache.position = mesh_cache_position;

        std::free(cache.data); // get rid of old data
        in_file.read(reinterpret_cast<char *>(cache.statuses), MESH_REGION_SIZE * sizeof(MeshCache::Status));

        in_file.read(reinterpret_cast<char *>(cache.decompressed_size), sizeof(cache.decompressed_size));
        in_file.read(reinterpret_cast<char *>(cache.compressed_size), sizeof(cache.compressed_size));
        in_file.read(reinterpret_cast<char *>(cache.offset), sizeof(cache.offset));

        in_file.read(reinterpret_cast<char *>(&cache.size), sizeof(int));

        cache.data = (Bytef *)std::malloc(static_cast<std::size_t>(cache.size));
        cache.container_size = cache.size;

        in_file.read(reinterpret_cast<char *>(cache.data), cache.size);

        if (!in_file.good()) std::runtime_error("Reading file failed.");
    }
    else
    {
        // construct new mesh cache
        for (auto & i : m_mesh_cache_infos[mesh_cache_index].statuses)
            i = MeshCache::Status::UNKNOWN;

        auto & cache = m_mesh_cache_infos[mesh_cache_index];

        cache.position = mesh_cache_position;

        //cache.needs_save = false;
        for (auto & s : cache.decompressed_size) s = 0;
        for (auto & s : cache.compressed_size) s = 0;
        for (auto & s : cache.offset) s = 0;
        cache.data = nullptr;
    }

    m_mesh_cache_infos[mesh_cache_index].needs_save = false;
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
          {
              auto r = std::rand() % 16;

              if (r < 3)
                  block = Block{ 3 };
              else if (r < 11)
                  block = Block{ 2 };
              else
                  block = Block{ 1 };
          }
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
        Debug::print("New loader thread loop.");

        const iVec3 center = m_center.load();
        bool buffer_stall = false;

        // remove out of range meshes
        std::size_t count = m_loaded_meshes.size();
        Debug::print("Loaded meshes count: ", count);
        for (std::size_t i = 0; i < count;)
        {
            const iVec3 mesh_center = m_loaded_meshes[i].position * MESH_SIZES + MESH_OFFSETS + (MESH_SIZES / 2);
            const auto mesh_relative = floorMod(m_loaded_meshes[i].position, MESH_CONTAINER_SIZES);
            const auto mesh_index = toIndex(mesh_relative, MESH_CONTAINER_SIZES);

            if (!inRange(center, mesh_center, SQUARE_REMOVE_DISTANCE))
            {
                if (!m_loaded_meshes[i].empty)
                {
                    Command * command = m_commands.initPush();
                    if (command != nullptr)
                    {
                        command->type = Command::Type::REMOVE;
                        command->index = mesh_index;
                        m_commands.commitPush();
                        Profiler::add(Profiler::Task::DeleteCommandsSubmitted, 1);
                    }
                    else
                    {
                        Debug::print("Command buffer is full.");
                        buffer_stall = true;
                        break;
                    }
                }
                m_loaded_meshes[i] = m_loaded_meshes[--count];
                assert(m_mesh_loaded[mesh_index] == Status::LOADED && "Mesh must be loaded in order to be unloaded.");
                m_mesh_loaded[mesh_index] = Status::UNLOADED;
            }
            else
            {
                ++i;
            }

        }
        m_loaded_meshes.resize(count);

        m_moved_far = false;

        bool new_stuff_found = false;
        // TODO: figure out when to reset queue

        // load new meshes
        if (!buffer_stall)
        {
            /*
            if (m_check_list.empty())
            {
                const auto center_mesh = floorDiv(center - MESH_OFFSETS, MESH_SIZES);
                m_check_list.push(center_mesh);
            }*/

            const auto center_mesh = floorDiv(center - MESH_OFFSETS, MESH_SIZES);
            int iterator = 0;

            // main loader loop
            //while (!m_check_list.empty() && !m_moved_far)
            while (iterator < m_iterator.m_points.size() && !m_moved_far && !m_quit)
            {
                // Debug::print("Queue size: ", m_check_list.size());

                //const auto current = m_check_list.front();

                // TODO: don't always start from scratch use reference positions from where to start when moving
                assert(iterator < m_iterator.m_points.size() && "Out of bounds.");
                const auto current = m_iterator.m_points[iterator++] + center_mesh;

                //Debug::print("Load mesh: ", toString(current));

                // or better just make the m_iterator size correct so break on iterator < m_iterator.m_points.size() is useful
                if (!inRange(center, current * MESH_SIZES + MESH_OFFSETS + (MESH_SIZES / 2), SQUARE_RENDER_DISTANCE))
                    break;

                const auto current_index = absoluteToIndex(current, MESH_CONTAINER_SIZES);

                switch (m_mesh_loaded[current_index])
                {
                    case Status::UNLOADED:
                    {
                        new_stuff_found = true;
                        // load mesh
                        if (meshStatus(current) == MeshCache::Status::UNKNOWN)
                        {
                            Command *command = m_commands.initPush();
                            if (command == nullptr)
                            {
                                // command buffer stall
                                buffer_stall = true;
                                break;
                            }

                            const auto from_block = current * MESH_SIZES + MESH_OFFSETS;
                            const auto to_block = from_block + MESH_SIZES;
                            // TODO: paralelize next two lines?
                            loadChunkRange(from_block - MESH_BORDER_REQUIRED_SIZE, to_block + MESH_BORDER_REQUIRED_SIZE);
                            const auto mesh = generateMesh(from_block, to_block);

                            if (mesh.size() > 0)
                            {
                                // For when actually caching mesh gets implemented
                                // assert(meshStatus(current) == MeshCache::Status::UNKNOWN && "Why load again?");
                                //if (meshStatus(current) == MeshCache::Status::UNKNOWN)
                                    //setMeshStatus(current, MeshCache::Status::NON_EMPTY);

                                command->type = Command::Type::UPLOAD;
                                command->index = current_index;
                                command->position = current;
                                command->mesh = mesh;
                                m_commands.commitPush();
                            }
                            else
                            {
                                // setMeshStatus(current, MeshCache::Status::EMPTY);
                                m_commands.discardPush();
                            }
                            saveMeshToMeshCache(current, mesh);

                            m_loaded_meshes.push_back({ current, mesh.size() == 0 });
                        }
                        else if (meshStatus(current) == MeshCache::Status::NON_EMPTY)
                        {
                            Command *command = m_commands.initPush();
                            if (command == nullptr)
                            {
                                // command buffer stall
                                buffer_stall = true;
                                break;
                            }

                            const auto from_block = current * MESH_SIZES + MESH_OFFSETS;
                            //const auto to_block = from_block + MESH_SIZES;
                            // TODO: paralelize next two lines?
                            //loadChunkRange(from_block - MESH_BORDER_REQUIRED_SIZE, to_block + MESH_BORDER_REQUIRED_SIZE);
                            const auto mesh = loadMesh(current);

                            assert(mesh.size() != 0 && "Loaded empty mesh?");
                            if (mesh.size() > 0)
                            {
                                // For when actually caching mesh gets implemented
                                // assert(meshStatus(current) == MeshCache::Status::UNKNOWN && "Why load again?");
                                //if (meshStatus(current) == MeshCache::Status::UNKNOWN)
                                //setMeshStatus(current, MeshCache::Status::NON_EMPTY);

                                command->type = Command::Type::UPLOAD;
                                command->index = current_index;
                                command->position = current;
                                command->mesh = mesh;
                                m_commands.commitPush();
                            }
                            else
                            {
                                // setMeshStatus(current, MeshCache::Status::EMPTY);
                                m_commands.discardPush();
                            }
                            // saveMeshToMeshCache(current, mesh);


                            m_loaded_meshes.push_back({ current, mesh.size() == 0 });
                        }
                        else if (meshStatus(current) == MeshCache::Status::EMPTY)
                        {
                            m_loaded_meshes.push_back({ current, true });
                        }
                        else
                        {
                            assert(0 && "Invalid state.");
                        }

                        // add neighbors
                        //const iVec3 neighbours[6]{
                        //        current + iVec3{1, 0, 0}, current + iVec3{0, 1, 0}, current + iVec3{0, 0, 1},
                        //        current - iVec3{1, 0, 0}, current - iVec3{0, 1, 0}, current - iVec3{0, 0, 1},
                        //};

                        //for (const iVec3 * pos = neighbours; pos < neighbours + 6; ++pos)
                            //if (inRange(center, *pos * MESH_SIZES + MESH_OFFSETS + (MESH_SIZES / 2), SQUARE_RENDER_DISTANCE))
                                //m_check_list.push(*pos);

                        // update mesh state
                        m_mesh_loaded[current_index] = Status::LOADED;
                    }
                    break;
                    case Status::LOADED:
                    {
                        // NO_OP
                    }
                    break;
                    default:
                    {
                        assert(0 && "Invalid state.");
                    }

                    //m_check_list.pop();
                }
            }

        }

        if (!new_stuff_found) // this doubles as sleep if buffer stall (not sure)
        {
            // TODO: replace sleep by conditional variable that is signaled when m_moved_far is set to true
            Debug::print("All loaded. Loader sleeping for ", SLEEP_MS, "ms.");
            std::this_thread::sleep_for(std::chrono::milliseconds(SLEEP_MS));
        }
    }

    Debug::print("Exited loader thread.");
}

#if 0
//==============================================================================
void World::meshLoaderOld()
{
    while (!m_quit)
    {
        Profiler::resetAll();
        Debug::print("New loader thread loop.");

        const iVec3 center = m_center.load();

        bool buffer_stall = false;
        bool new_stuff_found = false;

        // update remove and render in task list
        std::size_t count = m_loaded_meshes.size();
        Debug::print("Loaded meshes count: ", count);
        for (std::size_t i = 0; i < count;)
        {
            const iVec3 mesh_center = m_loaded_meshes[i].position * MESH_SIZES + MESH_OFFSETS + (MESH_SIZES / 2);

            const auto mesh_relative = floorMod(m_loaded_meshes[i].position, MESH_CONTAINER_SIZES);
            const auto mesh_index = toIndex(mesh_relative, MESH_CONTAINER_SIZES);

            if (!inRange(center, mesh_center, SQUARE_REMOVE_DISTANCE))
            {
                if (!m_loaded_meshes[i].empty)
                {
                    Command * command = m_commands.initPush();
                    if (command != nullptr)
                    {
                        command->type = Command::Type::REMOVE;
                        command->index = mesh_index;
                        m_commands.commitPush();
                        Profiler::add(Profiler::Task::DeleteCommandsSubmitted, 1);
                    }
                    else
                    {
                        Debug::print("Command buffer is full.");
                        buffer_stall = true;
                        break;
                    }
                }
                m_loaded_meshes[i] = m_loaded_meshes[--count];
                m_mesh_loaded[mesh_index] = Status::UNLOADED;
            }
            else
            {
                m_mesh_loaded[mesh_index] = Status::LOADED;
                ++i;
            }

        }
        m_loaded_meshes.resize(count);

        // WARNING: do not run mesh loader if buffer_stall is true,
        // because it can cause memory leak on the GPU (although should be caught by SparseMap if NDEBUG is note defined)

        bool moved_far = m_moved_far.exchange(false);

        // if command buffer was full let renderer breathe for a bit
        if (!buffer_stall)
        {
            // find meshes to generate and generate them

            // add center mesh
            const auto center_mesh = floorDiv(center - MESH_OFFSETS, MESH_SIZES);

#ifdef RESET_QUEUE
            { decltype(m_check_list) empty; std::swap(m_check_list, empty); }
#endif

            m_check_list.push(center_mesh);

            int meshes_loaded = 0;
            // breadth first search finds all borders of loaded are and loads it
            while (!m_check_list.empty() && !moved_far && meshes_loaded < MESHES_TO_LOAD_PER_LOOP)
            {
                const auto current = m_check_list.front();

                const auto current_index = absoluteToIndex(current, MESH_CONTAINER_SIZES);

                if (m_mesh_loaded[current_index] == Status::CHECKED)
                {
                    m_check_list.pop();
                    continue;
                }
                // load
                else if (m_mesh_loaded[current_index] == Status::UNLOADED)
                {
                    new_stuff_found = true;
                    if (meshStatus(current) != MeshCache::Status::EMPTY)
                    {
                        Command *command = m_commands.initPush();
                        if (command == nullptr)
                        {
                            // command buffer stall
                            buffer_stall = true;
                            break;
                        }

                        const auto from_block = current * MESH_SIZES + MESH_OFFSETS;
                        const auto to_block = from_block + MESH_SIZES;
                        // TODO: paralelize next two lines?
                        loadChunkRange(from_block - MESH_BORDER_REQUIRED_SIZE, to_block + MESH_BORDER_REQUIRED_SIZE);
                        const auto mesh = generateMesh(from_block, to_block);

                        if (mesh.size() > 0)
                        {
                            setMeshStatus(current, MeshCache::Status::NON_EMPTY);

                            command->type = Command::Type::UPLOAD;
                            command->index = current_index;
                            command->position = current;
                            command->mesh = mesh;
                            m_commands.commitPush();
                            ++meshes_loaded;
                        }
                        else
                        {
                            setMeshStatus(current, MeshCache::Status::EMPTY);
                            m_commands.discardPush();
                        }

                        m_loaded_meshes.push_back({ current, mesh.size() == 0 });
                    }
                    else
                    {
                        m_loaded_meshes.push_back({ current, true });
                    }
                }
                // push neighbours that are in render radius
                else if (m_mesh_loaded[current_index] == Status::LOADED)
                {
                    const iVec3 neighbours[6]{
                            current + iVec3{1, 0, 0}, current + iVec3{0, 1, 0}, current + iVec3{0, 0, 1},
                            current - iVec3{1, 0, 0}, current - iVec3{0, 1, 0}, current - iVec3{0, 0, 1},
                    };

                    for (const iVec3 * pos = neighbours; pos < neighbours + 6; ++pos)
                        if (inRange(center, *pos * MESH_SIZES + MESH_OFFSETS + (MESH_SIZES / 2), SQUARE_RENDER_DISTANCE))
                            m_check_list.push(*pos);
                }
                else
                {
                    assert(0);
                }

                m_check_list.pop();
                m_mesh_loaded[current_index] = Status::CHECKED;

                moved_far = m_moved_far;
            }
        }

        if (moved_far)
            Debug::print("Resetting because moved far.");

        if (buffer_stall)
            Debug::print("Resetting command buffer is full.");

        // TODO: increase the ratio by generating meshes in bulk
        const int loaded_c = Profiler::get(Profiler::Task::ChunksLoaded);
        const int loaded_m = Profiler::get(Profiler::Task::MeshesGenerated);
        const auto command_delete_times = Profiler::get(Profiler::Task::DeleteCommandsSubmitted);

        Debug::print(
                     "Chunks loaded: ", loaded_c,
                     " Meshes generated: ", loaded_m,
                     " Deletions requested: ", command_delete_times,
                     " Ratio: ", static_cast<float>(loaded_m) / static_cast<float>(loaded_c)
        );

        if (!new_stuff_found) // this doubles as sleep if buffer stall (not sure)
        {
            Debug::print("All loaded. Loader sleeping for ", SLEEP_MS, "ms.");
            std::this_thread::sleep_for(std::chrono::milliseconds(SLEEP_MS));
        }
    }

    Debug::print("Exited loader thread.");
}
#endif

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
    m_center = new_center;

    const auto delta_movement = new_center - m_reference_center;
    const auto square_distance = dot(delta_movement, delta_movement);
    const auto moved_far = square_distance >= SQUARE_LOAD_RESET_DISTANCE;

    if (moved_far)
    {
        m_reference_center = new_center;
        m_moved_far = true;
    }

    Command * command = nullptr;
    int commands_executed = 0;

    do
    {
        command = m_commands.initPop();

        // TODO: combine REMOVE and UPLOAD if-statement

        // remove out of range chunks
        if (command != nullptr && command->type == Command::Type::REMOVE)
        {
            const auto &mesh_data = m_meshes.get(command->index)->data.mesh;

            if (true)
            {
                assert(mesh_data.VBO && mesh_data.VAO && "Should not be 0.");
                m_unused_buffers.push({mesh_data.VAO, mesh_data.VBO});
            }
            else
            {
                glDeleteBuffers(1, &mesh_data.VBO);
                glDeleteVertexArrays(1, &mesh_data.VAO);
            }

            m_meshes.del(command->index);
            m_commands.commitPop();
        }

        // upload only after nothing left to remove
        else if (command != nullptr && command->type == Command::Type::UPLOAD) // else if because of m_commands.commitPop(); in previous if !!! => data race in ring buffer
        {
            GLuint VAO = 0, VBO = 0;
            if (!m_unused_buffers.empty())
            {
                const auto &top = m_unused_buffers.top();

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

                glVertexAttribIPointer(0, 3, GL_INT, sizeof(Vertex), (GLvoid *) (0));
                glVertexAttribIPointer(1, 1, GL_INT, sizeof(Vertex), (GLvoid *) (sizeof(Vertex::position)));
                glVertexAttribIPointer(2, 4, GL_UNSIGNED_BYTE, sizeof(Vertex),
                                       (GLvoid *) (sizeof(Vertex::position) + sizeof(Vertex::type)));
                glEnableVertexAttribArray(0);
                glEnableVertexAttribArray(1);
                glEnableVertexAttribArray(2);

                glBindVertexArray(0);
            }
            assert(command->mesh.size() > 0 && "Mesh size must be over 0.");

            assert(VAO != 0 && VBO != 0 && "Failed to gent VAO and VBO for mesh.");

            // upload mesh
            glBufferData(GL_ARRAY_BUFFER, command->mesh.size() * sizeof(command->mesh[0]), command->mesh.data(),
                         GL_STATIC_DRAW);
            // fast multiply by 1.5
            const int EBO_size = static_cast<int>((command->mesh.size() >> 1) + command->mesh.size());
            QuadEBO::resize(EBO_size);

            m_meshes.add(command->index, {{VAO, VBO, EBO_size}, command->position});

            // TODO: make loader thread clean up this data
            command->mesh.clear(); // does not deallocate to reduce space but whatever TODO: should replace with flat array anyway
            m_commands.commitPop();
        }
    }
    while(command != nullptr && commands_executed++ < MAX_COMMANDS_PER_FRAME);

    // render
    const auto * i = m_meshes.begin();
    const auto * end = m_meshes.end();
    for (; i != end; ++i)
    {
        // only render if not too far away
        // TODO: could be combined with frustum culling (make far frustum sqrt(SQUARE_RENDER_DISTACE) away)
        static_assert(!(MESH_SIZE_X % 2 || MESH_SIZE_Y % 2 || MESH_SIZE_Z % 2), "Assuming even mesh sizes");
        if (!inRange(new_center, i->data.position * MESH_SIZES + MESH_OFFSETS + (MESH_SIZES / 2), SQUARE_RENDER_DISTANCE))
            continue;

        if (meshInFrustum(frustum_planes, i->data.position * MESH_SIZES + MESH_OFFSETS))
        {
            const auto & mesh_data = i->data.mesh;

            assert(mesh_data.size <= QuadEBO::size() && mesh_data.size > 0);
            assert(mesh_data.VAO != 0 && mesh_data.VBO != 0 && "VAO and VBO not loaded.");

            glBindVertexArray(mesh_data.VAO);
            glDrawElements(GL_TRIANGLES, mesh_data.size, QuadEBO::type(), 0);
            glBindVertexArray(0);
        }
    }
}

//==============================================================================
void World::exitLoaderThread()
{
    if (!m_loader_thread.joinable())
    {
        assert(0 && "Why is loader not joinable?");
        return;
    }

    m_quit = true;

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
    const auto chunk_position = m_chunk_positions[chunk_index];
    const auto region_position = floorDiv(chunk_position, REGION_SIZES);
    loadRegion(region_position);

    const auto region_relative = floorMod(region_position, REGION_CONTAINER_SIZES);
    const auto region_index = toIndex(region_relative, REGION_CONTAINER_SIZES);

    const auto chunk_in_region_relative = floorMod(chunk_position, REGION_SIZES);
    const auto chunk_in_region_index = toIndex(chunk_in_region_relative, REGION_SIZES);

    // compress chunk
    uLong destination_length = compressBound(static_cast<uLong>(SOURCE_LENGTH)); // compressBound could be static

    auto & cache = m_regions[region_index];
    assert(cache.size <= cache.container_size && "Capacity should always be more than size.");

    // resize if potentially out of space
    if (m_regions[region_index].size + static_cast<int>(destination_length) > m_regions[region_index].container_size)
    {
        Debug::print("Reallocating region container.");
        m_regions[region_index].container_size += REGION_DATA_SIZE_FACTOR < static_cast<int>(destination_length) ? static_cast<int>(destination_length) : REGION_DATA_SIZE_FACTOR;
        m_regions[region_index].data = (Bytef*)std::realloc(m_regions[region_index].data, static_cast<std::size_t>(m_regions[region_index].container_size));
    }

    const auto * beginning_of_chunk = &getBlock(chunk_position * CHUNK_SIZES); // address of first block
    Bytef * destination = m_regions[region_index].data + m_regions[region_index].size;

    // TODO: checkout other compression libraries that are faster
    // compress and save at once data to region

    // TODO: profile how much it is compressed
    auto result = compress2(destination, &destination_length, reinterpret_cast<const Bytef *>(beginning_of_chunk), static_cast<uLong>(SOURCE_LENGTH), Z_BEST_SPEED);

    assert(result == Z_OK && "Error compressing chunk.");
    assert(destination_length <= compressBound(static_cast<uLong>(SOURCE_LENGTH)) && "ZLib lied about the maximum possible size of compressed data.");

    m_regions[region_index].metas[chunk_in_region_index].size = static_cast<int>(destination_length);
    m_regions[region_index].metas[chunk_in_region_index].offset = m_regions[region_index].size;

    m_regions[region_index].size += static_cast<int>(destination_length);
    m_regions[region_index].needs_save = true;
}

//==============================================================================
void World::saveRegionToDrive(const int region_index)
{
    if (!m_regions[region_index].needs_save)
        return;

    const auto position = m_regions[region_index].position;

    // only if valid
    if (
            (region_index == 0 && all(position == iVec3{ 0, 0, 0 })) ||
            (region_index != 0 && !all(position == iVec3{ 0, 0, 0 }))
        )
    {
        // save old region
        Debug::print("Saving region ", toString(position));
        assert(m_regions[region_index].data != nullptr && "No idea why this can happen.");

        std::string file_name = WORLD_ROOT + toString(position);
        std::ofstream file{ file_name, std::ofstream::out | std::ofstream::binary | std::ofstream::trunc };

        file.write(reinterpret_cast<const char *>(&m_regions[region_index].size), sizeof(int));
        file.write(reinterpret_cast<const char *>(m_regions[region_index].metas), META_DATA_SIZE);
        file.write(reinterpret_cast<const char *>(m_regions[region_index].data), m_regions[region_index].size);

        if (!file.good()) std::runtime_error("Writing file failed.");
    }
}

//==============================================================================
void World::saveMeshCacheToDrive(const int mesh_cache_index)
{
    if (!m_mesh_cache_infos[mesh_cache_index].needs_save)
        return;

    const auto position = m_mesh_cache_infos[mesh_cache_index].position;

    // only if valid
    if (
            (mesh_cache_index == 0 && all(position == iVec3{ 0, 0, 0 })) ||
            (mesh_cache_index != 0 && !all(position == iVec3{ 0, 0, 0 }))
       )
    {
        // save old mesh cache
        Debug::print("Saving mesh cache ", toString(position));

        std::string file_name = MESH_CACHE_ROOT + toString(position);
        std::ofstream file{ file_name, std::ofstream::out | std::ofstream::binary | std::ofstream::trunc };

        file.write(reinterpret_cast<const char *>(m_mesh_cache_infos[mesh_cache_index].statuses), MESH_REGION_SIZE * sizeof(MeshCache::Status));
        file.write(reinterpret_cast<const char *>(m_mesh_cache_infos[mesh_cache_index].decompressed_size), sizeof(m_mesh_cache_infos[mesh_cache_index].decompressed_size));
        file.write(reinterpret_cast<const char *>(m_mesh_cache_infos[mesh_cache_index].compressed_size), sizeof(m_mesh_cache_infos[mesh_cache_index].compressed_size));
        file.write(reinterpret_cast<const char *>(m_mesh_cache_infos[mesh_cache_index].offset), sizeof(m_mesh_cache_infos[mesh_cache_index].offset));

        file.write(reinterpret_cast<const char *>(&m_mesh_cache_infos[mesh_cache_index].size), sizeof(int));

        file.write(reinterpret_cast<const char *>(m_mesh_cache_infos[mesh_cache_index].data), m_mesh_cache_infos[mesh_cache_index].size);

        if (!file.good()) std::runtime_error("Writing file failed.");
    }
}

//==============================================================================
void World::saveMeshToMeshCache(const iVec3 mesh_position, const std::vector<Vertex> & mesh)
{
    const auto new_status = mesh.size() == 0 ? MeshCache::Status::EMPTY : MeshCache::Status::NON_EMPTY;

    const auto mesh_cache_position = floorDiv(mesh_position, MESH_REGION_SIZES);

    const auto mesh_in_mesh_cache_relative = floorMod(mesh_position, MESH_REGION_SIZES);
    const auto mesh_in_mesh_cache_index = toIndex(mesh_in_mesh_cache_relative, MESH_REGION_SIZES);

    const auto mesh_cache_relative = floorMod(mesh_cache_position, MESH_REGION_CONTAINER_SIZES);
    const auto mesh_cache_index = toIndex(mesh_cache_relative, MESH_REGION_CONTAINER_SIZES);

    auto & mesh_cache = m_mesh_cache_infos[mesh_cache_index];

    if (!all(mesh_cache.position == mesh_cache_position))
        loadMeshCache(mesh_cache_position);

    // save new mesh status
    assert(mesh_cache.statuses[mesh_in_mesh_cache_index] != new_status && "Could indicate a bug.");
    mesh_cache.statuses[mesh_in_mesh_cache_index] = new_status;
    m_mesh_cache_infos[mesh_cache_index].needs_save = true;

    if (new_status == MeshCache::Status::EMPTY)
        return;

    // compress mesh
    uLong destination_length = compressBound(static_cast<uLong>(mesh.size()) * sizeof(mesh[0]));

    auto & cache = m_mesh_cache_infos[mesh_cache_index];
    assert(cache.size <= cache.container_size && "Capacity should always be more than size.");

    // resize if potentially out of space
    if (m_mesh_cache_infos[mesh_cache_index].size + static_cast<int>(destination_length) > m_mesh_cache_infos[mesh_cache_index].container_size)
    {
        Debug::print("Reallocating mesh container.");
        m_mesh_cache_infos[mesh_cache_index].container_size += MESH_CACHE_DATA_SIZE_FACTOR < static_cast<int>(destination_length) ? static_cast<int>(destination_length) : MESH_CACHE_DATA_SIZE_FACTOR;
        // realloc(nullptr) acts as malloc
        m_mesh_cache_infos[mesh_cache_index].data = (Bytef*)std::realloc(m_mesh_cache_infos[mesh_cache_index].data, static_cast<std::size_t>(m_mesh_cache_infos[mesh_cache_index].container_size));
    }

    Bytef * destination = m_mesh_cache_infos[mesh_cache_index].data + m_mesh_cache_infos[mesh_cache_index].size;

    // TODO: checkout other compression libraries that are faster
    // compress and save at once data to region

    // TODO: profile how much it is compressed
    auto result = compress2(destination, &destination_length, reinterpret_cast<const Bytef *>(mesh.data()), static_cast<uLong>(mesh.size() * sizeof(mesh[0])), Z_BEST_SPEED);

    assert(result == Z_OK && "Error compressing mesh.");
    assert(destination_length <= compressBound(static_cast<uLong>(mesh.size() * sizeof(mesh[0]))) && "ZLib lied about the maximum possible size of compressed data.");

    m_mesh_cache_infos[mesh_cache_index].compressed_size[mesh_in_mesh_cache_index] = static_cast<int>(destination_length);
    m_mesh_cache_infos[mesh_cache_index].decompressed_size[mesh_in_mesh_cache_index] = static_cast<int>(mesh.size());
    m_mesh_cache_infos[mesh_cache_index].offset[mesh_in_mesh_cache_index] = m_mesh_cache_infos[mesh_cache_index].size;

    m_mesh_cache_infos[mesh_cache_index].size += static_cast<int>(destination_length);
    m_mesh_cache_infos[mesh_cache_index].needs_save = true;
}

//==============================================================================
std::vector<Vertex> World::loadMesh(const iVec3 mesh_position)
{
    const auto mesh_cache_position = floorDiv(mesh_position, MESH_REGION_SIZES);

    const auto mesh_in_mesh_cache_relative = floorMod(mesh_position, MESH_REGION_SIZES);
    const auto mesh_in_mesh_cache_index = toIndex(mesh_in_mesh_cache_relative, MESH_REGION_SIZES);

    const auto mesh_cache_relative = floorMod(mesh_cache_position, MESH_REGION_CONTAINER_SIZES);
    const auto mesh_cache_index = toIndex(mesh_cache_relative, MESH_REGION_CONTAINER_SIZES);

    auto & mesh_cache = m_mesh_cache_infos[mesh_cache_index];

    if (!all(mesh_cache.position == mesh_cache_position))
        loadMeshCache(mesh_cache_position);

    // empty mesh
    if (mesh_cache.decompressed_size[mesh_in_mesh_cache_index] == 0)
    {
        //return{};
        assert(0 && "Function should not be called for empty chunks.");
    }
    // load mesh from mesh cache
    else
    {
        std::vector<Vertex> mesh{ static_cast<size_t>(mesh_cache.decompressed_size[mesh_in_mesh_cache_index]) };
        uLongf destination_length = static_cast<uLongf>(mesh.size() * sizeof(mesh[0]));

        const auto off = mesh_cache.offset[mesh_in_mesh_cache_index];
        const auto * source = mesh_cache.data + off;
        auto result = uncompress(
                reinterpret_cast<Bytef *>(mesh.data()), &destination_length,
                source, static_cast<uLongf>(mesh_cache.compressed_size[mesh_in_mesh_cache_index])
        );
        assert(result == Z_OK && destination_length == mesh_cache.decompressed_size[mesh_in_mesh_cache_index] * sizeof(mesh[0]) && "Error in decompression.");

        return mesh;
    }
}

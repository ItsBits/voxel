#include "World.hpp"
#include "QuadEBO.hpp"
#include <cassert>
#include <cmath>
#include "TinyAlgebraExtensions.hpp"
#include "Debug.hpp"
#include "Profiler.hpp"
#include <cstring>
#include <fstream>

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
    for (auto & status : m_chunk_statuses)
        status = { { 0, 0, 0 }, false };
    m_chunk_statuses[{ 0, 0, 0 }].position = { 1, 0, 0 };

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
    m_regions[{ 0, 0, 0 }].position = { 1, 0, 0 };

    for (auto & i : m_mesh_cache_infos)
    {
        i.position = { 0, 0, 0 };
        i.size = 0;
        i.container_size = 0;
        i.needs_save = false;
        for (auto & s : i.info) s = { MeshCache::Status::UNKNOWN, 0, 0, 0 };
        i.data = nullptr;
    }
    m_mesh_cache_infos[{ 0, 0, 0 }].position = { 1, 0, 0 };

    for (auto & i : m_mesh_loaded) i = Status::UNLOADED;

    m_loader_thread = std::thread{ &World::meshLoader, this };
}

//==============================================================================
World::~World()
{
    Debug::print("Exiting loader thread.");

    exitLoaderThread();

    Debug::print("Saving unsaved chunks.");

    // check all chunks if they need to be saved and save them
    for (auto & chunk_status : m_chunk_statuses)
        if (chunk_status.needs_save) // TODO: check if this is this optional because saveChunkToRegions checks it?
            saveChunkToRegion(chunk_status.position);

    // save all valid regions
    for (const auto & region : m_regions)
    {
        saveRegionToDrive(region.position);
    }

    // save all mesh caches to drive
    bool first_TODO_delete = true;
    for (const auto & mesh_cache : m_mesh_cache_infos)
    {
        if (first_TODO_delete)
        {
            first_TODO_delete = false;
            saveMeshCacheToDrive(mesh_cache, { 0, 0, 0 });
        }
        else
        {
            if (all(mesh_cache.position == iVec3{ 0, 0, 0 }))
                saveMeshCacheToDrive(mesh_cache, { 1, 0, 0 });
            else
                saveMeshCacheToDrive(mesh_cache, mesh_cache.position);
        }
    }

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

    const auto & mesh_cache = m_mesh_cache_infos[mesh_cache_position];

    if (!all(mesh_cache.position == mesh_cache_position))
        loadMeshCache(mesh_cache_position);

    //return mesh_cache.statuses[mesh_in_mesh_cache_index];
    return mesh_cache.info[mesh_position].status;
}

//==============================================================================
void World::loadRegion(const iVec3 region_position)
{
    const auto old_position = m_regions[region_position].position;

    // return if already loaded
    if (all(old_position == region_position))
        return;

    // save existing region
    if (all(floorMod(old_position, REGION_CONTAINER_SIZES) == floorMod(region_position, REGION_CONTAINER_SIZES)))
        saveRegionToDrive(old_position);
    else
        assert(m_regions[region_position].needs_save == false && "Something broke.");

    std::string in_file_name = WORLD_ROOT + toString(region_position);
    std::ifstream in_file{ in_file_name, std::ifstream::binary };
    if (in_file.good())
    {
        // load region from drive because it exists
        Debug::print("Loading region ", toString(region_position));

        auto & region = m_regions[region_position];
        region.position = region_position;
        in_file.read(reinterpret_cast<char *>(&region.size), sizeof(int));
        in_file.read(reinterpret_cast<char *>(region.metas.begin()), META_DATA_SIZE);
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
        auto & region = m_regions[region_position];
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
    /*cond*/ auto & mesh_cache_info = m_mesh_cache_infos[mesh_cache_position];
    const auto old_position = mesh_cache_info.position;

    // return if already loaded
    if (all(old_position == mesh_cache_position))
        return;

    if (all(floorMod(mesh_cache_position, MESH_REGION_CONTAINER_SIZES) == floorMod(mesh_cache_info.position, MESH_REGION_CONTAINER_SIZES)))
        saveMeshCacheToDrive(mesh_cache_info, mesh_cache_info.position);
    else if (mesh_cache_info.needs_save)
        // TODO: just for debugging. remove that
        assert(0 && "Something is broken.");

    std::string in_file_name = MESH_CACHE_ROOT + toString(mesh_cache_position);
    std::ifstream in_file{ in_file_name, std::ifstream::binary };
    if (in_file.good())
    {
        Debug::print("Loading mesh cache ", toString(mesh_cache_position));

        auto & cache = mesh_cache_info;
        cache.position = mesh_cache_position;

        std::free(cache.data); // get rid of old data

        in_file.read(reinterpret_cast<char *>(&cache.size), sizeof(int));

        in_file.read(reinterpret_cast<char *>(cache.info.begin()), sizeof(cache.info));

        cache.data = (Bytef *)std::malloc(static_cast<std::size_t>(cache.size));
        cache.container_size = cache.size;

        in_file.read(reinterpret_cast<char *>(cache.data), cache.size);

        if (!in_file.good()) std::runtime_error("Reading file failed.");
    }
    else
    {
        // construct new mesh cache
        for (auto & i : mesh_cache_info.info)
            i = { MeshCache::Status::UNKNOWN, 0, 0, 0 };
        mesh_cache_info.size = 0;
        mesh_cache_info.container_size = 0;
        mesh_cache_info.needs_save = false;

        auto & cache = mesh_cache_info;

        cache.position = mesh_cache_position;

        cache.data = nullptr;
    }

    mesh_cache_info.needs_save = false;
}

//==============================================================================
int World::loadChunk(const iVec3 chunk_position)
{
    auto & chunk_status = m_chunk_statuses[chunk_position];

    // if already loaded
    if (all(chunk_status.position == chunk_position))
      return 0;

    const auto region_position = floorDiv(chunk_position, REGION_SIZES);

    // save previous chunk
    if (chunk_status.needs_save)
        saveChunkToRegion(chunk_status.position);

    // load region of new chunk
    loadRegion(region_position);

    auto & chunk_meta = m_regions[region_position].metas[chunk_position];

    // generate new chunk
    if (chunk_meta.size == 0)
    {
        generateChunk(chunk_position * CHUNK_SIZES);
        chunk_status.position = chunk_position;
        chunk_status.needs_save = true; // TODO: maybe immediately save to region
    }
    // load chunk from region
    else
    {
        auto * beginning_of_chunk = &getBlock(chunk_position * CHUNK_SIZES); // address of first block

        // load chunk from region
        uLongf destination_length = static_cast<uLongf>(SOURCE_LENGTH);
        const auto off = chunk_meta.offset;
        const auto siz = chunk_meta.size;

        const auto * source = m_regions[region_position].data + off;
        auto result = uncompress(
                reinterpret_cast<Bytef *>(beginning_of_chunk), &destination_length,
                source, static_cast<uLongf>(siz)
        );
        assert(result == Z_OK && destination_length == SOURCE_LENGTH && "Error in decompression.");

        chunk_status.position = chunk_position;
        chunk_status.needs_save = false;
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
            const auto test_position_DELETE_QUESTIONMARK = m_loaded_meshes[i].position;
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
                assert(m_mesh_loaded[test_position_DELETE_QUESTIONMARK] == Status::LOADED && "Mesh must be loaded in order to be unloaded.");
                m_mesh_loaded[test_position_DELETE_QUESTIONMARK] = Status::UNLOADED;
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
            const auto center_mesh = floorDiv(center - MESH_OFFSETS, MESH_SIZES);
            int iterator = 0;

            // main loader loop
            while (iterator < m_iterator.m_points.size() && !m_moved_far && !m_quit)
            {
                // TODO: don't always start from scratch use reference positions from where to start when moving
                assert(iterator < m_iterator.m_points.size() && "Out of bounds.");
                const auto current = m_iterator.m_points[iterator++] + center_mesh;

                // or better just make the m_iterator size correct so break on iterator < m_iterator.m_points.size() is useful
                if (!inRange(center, current * MESH_SIZES + MESH_OFFSETS + (MESH_SIZES / 2), SQUARE_RENDER_DISTANCE))
                {
                    Debug::print("Break because out of range.");
                    break;
                }

                const auto current_index = absoluteToIndex(current, MESH_CONTAINER_SIZES);

                switch (m_mesh_loaded[current])
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
                                goto BREAK_LOOP; // because of stupid switch
                            }

                            const auto from_block = current * MESH_SIZES + MESH_OFFSETS;
                            const auto to_block = from_block + MESH_SIZES;
                            // TODO: paralelize next two lines?
                            loadChunkRange(from_block - MESH_BORDER_REQUIRED_SIZE, to_block + MESH_BORDER_REQUIRED_SIZE);
                            const auto mesh = generateMesh(from_block, to_block);

                            if (mesh.size() > 0)
                            {
                                command->type = Command::Type::UPLOAD;
                                command->index = current_index;
                                command->position = current;
                                command->mesh = mesh;
                                m_commands.commitPush();
                            }
                            else
                            {
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
                                goto BREAK_LOOP; // because of stupid switch
                            }

                            const auto from_block = current * MESH_SIZES + MESH_OFFSETS;
                            //const auto to_block = from_block + MESH_SIZES;
                            // TODO: paralelize next line?
                            const auto mesh = loadMesh(current);

                            assert(mesh.size() != 0 && "Loaded empty mesh?");
                            if (mesh.size() > 0)
                            {
                                command->type = Command::Type::UPLOAD;
                                command->index = current_index;
                                command->position = current;
                                command->mesh = mesh;
                                m_commands.commitPush();
                            }
                            else
                            {
                                m_commands.discardPush();
                            }

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

                        // update mesh state
                        m_mesh_loaded[current] = Status::LOADED;
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
                }
            }
BREAK_LOOP: (void)0;

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
void World::saveChunkToRegion(const iVec3 chunk_position)
{
    assert(all(m_chunk_statuses[chunk_position].position == chunk_position) && "Something broke.");
    // load correct region file
    const auto region_position = floorDiv(chunk_position, REGION_SIZES);

    loadRegion(region_position);

    // compress chunk
    uLong destination_length = compressBound(static_cast<uLong>(SOURCE_LENGTH)); // compressBound could be static

    auto & cache = m_regions[region_position];
    assert(cache.size <= cache.container_size && "Capacity should always be more than size.");

    // resize if potentially out of space
    if (cache.size + static_cast<int>(destination_length) > cache.container_size)
    {
        Debug::print("Reallocating region container.");
        cache.container_size += REGION_DATA_SIZE_FACTOR < static_cast<int>(destination_length) ? static_cast<int>(destination_length) : REGION_DATA_SIZE_FACTOR;
        cache.data = (Bytef*)std::realloc(cache.data, static_cast<std::size_t>(cache.container_size));
    }

    const auto * beginning_of_chunk = &getBlock(chunk_position * CHUNK_SIZES); // address of first block
    Bytef * destination = cache.data + cache.size;

    // TODO: checkout other compression libraries that are faster
    // compress and save at once data to region

    // TODO: profile how much it is compressed
    auto result = compress2(destination, &destination_length, reinterpret_cast<const Bytef *>(beginning_of_chunk), static_cast<uLong>(SOURCE_LENGTH), Z_BEST_SPEED);

    assert(result == Z_OK && "Error compressing chunk.");
    assert(destination_length <= compressBound(static_cast<uLong>(SOURCE_LENGTH)) && "ZLib lied about the maximum possible size of compressed data.");

    auto & chunk_meta = cache.metas[chunk_position];

    chunk_meta.size = static_cast<int>(destination_length);
    chunk_meta.offset = cache.size;

    cache.size += static_cast<int>(destination_length);
    cache.needs_save = true;
}

//==============================================================================
void World::saveRegionToDrive(const iVec3 region_position)
{
    auto & region = m_regions[region_position];

    if (!region.needs_save)
        return;

    const auto position = region.position;

    // only if valid
    assert(all(region_position == region.position) && "Trying to save invalid region.");

    // save old region
    Debug::print("Saving region ", toString(position));
    assert(region.data != nullptr && "No idea why this can happen.");

    std::string file_name = WORLD_ROOT + toString(position);
    std::ofstream file{ file_name, std::ofstream::out | std::ofstream::binary | std::ofstream::trunc };

    file.write(reinterpret_cast<const char *>(&region.size), sizeof(int));
    file.write(reinterpret_cast<const char *>(region.metas.begin()), META_DATA_SIZE);
    file.write(reinterpret_cast<const char *>(region.data), region.size);

    if (!file.good()) std::runtime_error("Writing file failed.");

}

//==============================================================================
// TODO: refactor: should not need to take both mesh_cache and first_delete_that_it_s_just_for_testing
void World::saveMeshCacheToDrive(const MeshCache & mesh_cache, const iVec3 first_delete_that_it_s_just_for_testing)
{
    if (!mesh_cache.needs_save)
        return;

    const auto position = mesh_cache.position;

    assert(all(first_delete_that_it_s_just_for_testing == mesh_cache.position) && "Trying to save invalid mesh cache.");

    // save old mesh cache
    Debug::print("Saving mesh cache ", toString(position));

    std::string file_name = MESH_CACHE_ROOT + toString(position);
    std::ofstream file{ file_name, std::ofstream::out | std::ofstream::binary | std::ofstream::trunc };

    file.write(reinterpret_cast<const char *>(&mesh_cache.size), sizeof(int));
    file.write(reinterpret_cast<const char *>(mesh_cache.info.begin()), sizeof(mesh_cache.info));
    file.write(reinterpret_cast<const char *>(mesh_cache.data), mesh_cache.size);

    if (!file.good()) std::runtime_error("Writing file failed.");

}

//==============================================================================
void World::saveMeshToMeshCache(const iVec3 mesh_position, const std::vector<Vertex> & mesh)
{
    const auto new_status = mesh.size() == 0 ? MeshCache::Status::EMPTY : MeshCache::Status::NON_EMPTY;

    const auto mesh_cache_position = floorDiv(mesh_position, MESH_REGION_SIZES);

    auto & mesh_cache = m_mesh_cache_infos[mesh_cache_position];

    if (!all(mesh_cache.position == mesh_cache_position))
        loadMeshCache(mesh_cache_position);

    // save new mesh status
    assert(mesh_cache.info[mesh_position].status != new_status && "Could indicate a bug.");
    mesh_cache.info[mesh_position].status = new_status;
    mesh_cache.needs_save = true;

    if (new_status == MeshCache::Status::EMPTY)
        return;

    // compress mesh
    uLong destination_length = compressBound(static_cast<uLong>(mesh.size()) * sizeof(mesh[0]));

    auto & cache = m_mesh_cache_infos[mesh_cache_position];
    assert(cache.size <= cache.container_size && "Capacity should always be more than size.");

    // resize if potentially out of space
    if (cache.size + static_cast<int>(destination_length) > cache.container_size)
    {
        Debug::print("Reallocating mesh container.");
        cache.container_size += MESH_CACHE_DATA_SIZE_FACTOR < static_cast<int>(destination_length) ? static_cast<int>(destination_length) : MESH_CACHE_DATA_SIZE_FACTOR;
        // realloc(nullptr) acts as malloc
        cache.data = (Bytef*)std::realloc(cache.data, static_cast<std::size_t>(cache.container_size));
    }

    Bytef * destination = cache.data + cache.size;

    // TODO: checkout other compression libraries that are faster
    // compress and save at once data to region

    // TODO: profile how much it is compressed
    auto result = compress2(destination, &destination_length, reinterpret_cast<const Bytef *>(mesh.data()), static_cast<uLong>(mesh.size() * sizeof(mesh[0])), Z_BEST_SPEED);

    //Debug::print("Compression ratio: ", static_cast<double>(destination_length) / (mesh.size() * sizeof(mesh[0])));

    assert(result == Z_OK && "Error compressing mesh.");
    assert(destination_length <= compressBound(static_cast<uLong>(mesh.size() * sizeof(mesh[0]))) && "ZLib lied about the maximum possible size of compressed data.");

    // TODO: index from mesh_position will be recalculated in operator[]. Add ModTable functions that take index
    cache.info[mesh_position].compressed_size= static_cast<int>(destination_length);
    cache.info[mesh_position].decompressed_size= static_cast<int>(mesh.size());
    cache.info[mesh_position].offset= cache.size;

    cache.size += static_cast<int>(destination_length);
    cache.needs_save = true;
}

//==============================================================================
std::vector<Vertex> World::loadMesh(const iVec3 mesh_position)
{
    const auto mesh_cache_position = floorDiv(mesh_position, MESH_REGION_SIZES);

    auto & mesh_cache = m_mesh_cache_infos[mesh_cache_position];

    if (!all(mesh_cache.position == mesh_cache_position))
        loadMeshCache(mesh_cache_position);

    const auto & decompressed_size = mesh_cache.info[mesh_position].decompressed_size;

    // empty mesh
    if (decompressed_size == 0)
    {
        assert(0 && "Function should not be called for empty chunks.");
    }
    // load mesh from mesh cache
    else
    {
        std::vector<Vertex> mesh{ static_cast<size_t>(decompressed_size) };
        uLongf destination_length = static_cast<uLongf>(mesh.size() * sizeof(mesh[0]));

        const auto off = mesh_cache.info[mesh_position].offset;
        const auto * source = mesh_cache.data + off;
        auto result = uncompress(
                reinterpret_cast<Bytef *>(mesh.data()), &destination_length,
                source, static_cast<uLongf>(mesh_cache.info[mesh_position].compressed_size)
        );
        assert(result == Z_OK && destination_length == decompressed_size * sizeof(mesh[0]) && "Error in decompression.");

        return mesh;
    }
}

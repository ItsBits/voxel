#include "World.hpp"
#include "QuadEBO.hpp"
#include <cassert>
#include <cmath>
#include "TinyAlgebraExtensions.hpp"
#include "Debug.hpp"
#include "Profiler.hpp"
#include <cstring>
#include <fstream>
#include <malloc.h>

//==============================================================================
constexpr char World::WORLD_ROOT[];
constexpr char World::MESH_CACHE_ROOT[];
constexpr iVec3 World::CHUNK_SIZES;
constexpr iVec3 World::CHUNK_CONTAINER_SIZES;
constexpr iVec3 World::CHUNK_REGION_CONTAINER_SIZES;
constexpr iVec3 World::MESH_REGION_CONTAINER_SIZES;
constexpr iVec3 World::MESH_REGION_SIZES;
constexpr iVec3 World::CHUNK_REGION_SIZES;
constexpr int World::MESH_BORDER_REQUIRED_SIZE;
constexpr int World::REGION_DATA_SIZE_FACTOR;
constexpr unsigned char World::SHADDOW_STRENGTH;
constexpr iVec3 World::MESH_CONTAINER_SIZES;
constexpr iVec3 World::MESH_SIZES;
constexpr iVec3 World::MESH_OFFSETS;
constexpr iVec3 World::chunk_container_size;
constexpr int World::SLEEP_MS;
constexpr int World::STALL_SLEEP_MS;

//==============================================================================
World::World() :
//        m_reference_center{ 0, 0, 0 },
        //m_center{ { 0, 0, 0 } },
        m_center_mesh{ { 0, 0, 0 } }, // TODO: update to correct position before first use in meshLoader
        m_quit{ false },
        m_moved_center_mesh{ false }
{
    //std:: cout << sizeof(std::atomic_bool) << std::endl;

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

    for (auto & i : m_mesh_caches)
    {
        i.position = { 0, 0, 0 };
        i.size = 0;
        i.container_size = 0;
        i.needs_save = false;
        for (auto & s : i.infos) s = { MeshCache::Status::UNKNOWN, 0, 0, 0 };
        i.data = nullptr;
    }
    m_mesh_caches[{ 0, 0, 0 }].position = { 1, 0, 0 };

    for (auto & i : m_mesh_loaded) i = Status::UNLOADED;

    //m_loader_thread = std::thread{ &World::meshLoader, this };
    for (std::size_t i = 0; i < THREAD_COUNT; ++i)
        m_workers[i] = std::thread{ &World::multiThreadMeshLoader, this, i };
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

    // TODO: refactor
    // save all valid regions
    bool first_deelete_that = true;
    for (const auto & region : m_regions)
    {
        if (first_deelete_that)
        {
            first_deelete_that = false;

            if (all(region.position == iVec3{ 1, 0, 0 }))
                continue;
            else
                saveRegionToDrive(region.position);
        }
        else
        {
            if (!all(region.position == iVec3{ 0, 0, 0 }))
                saveRegionToDrive(region.position);
        }
    }

    // save all mesh caches to drive
    bool first_TODO_delete = true;
    for (const auto & mesh_cache : m_mesh_caches)
    {
        if (mesh_cache.needs_save)
            saveMeshCacheToDrive(mesh_cache.position);

        /*if (first_TODO_delete)
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
        }*/
    }

    Debug::print("Cleaning up memory.");

    // cleanup
    for (auto & i : m_regions) std::free(i.data);

    // delete vertex and vao buffers from active meshes
    const auto * i = m_meshes.begin();
    const auto * end = m_meshes.end();
    for (; i != end; ++i) // TODO: range based for
    {
        auto & mesh_data = i->data.mesh;

        // only both equal to 0 or both not equal to 0 is valid
        if (mesh_data.VAO == 0 && mesh_data.VBO == 0) continue;
        assert(mesh_data.VAO != 0 && mesh_data.VBO != 0 && "Active buffers should not be 0.");

        glDeleteVertexArrays(1, &mesh_data.VAO);
        glDeleteBuffers(1, &mesh_data.VBO);

        // WTF is this here?
        //m_meshes.reset();
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
    const auto block_index = positionToIndex(block_position, CHUNK_SIZES);

    const auto chunk_position = floorDiv(block_position, CHUNK_SIZES);
    const auto chunk_index = positionToIndex(chunk_position, CHUNK_CONTAINER_SIZES);

    return m_blocks[chunk_index * CHUNK_SIZE + block_index];
}

//==============================================================================
void World::loadChunkRange(const iVec3 from_block, const iVec3 to_block)
{
    assert(all(from_block < to_block) && "From values must be lower than to values.");

    const auto chunk_position_from = floorDiv(from_block, CHUNK_SIZES);
    const auto chunk_position_to = floorDiv(to_block - 1, CHUNK_SIZES);

    iVec3 position;

    for (position(2) = chunk_position_from(2); position(2) <= chunk_position_to(2); ++position(2))
        for (position(1) = chunk_position_from(1); position(1) <= chunk_position_to(1); ++position(1))
            for (position(0) = chunk_position_from(0); position(0) <= chunk_position_to(0); ++position(0))
                loadChunkToChunkContainer(position);
}

//==============================================================================
World::MeshCache::Status World::getMeshStatus(const iVec3 mesh_position)
{
    const auto mesh_cache_position = floorDiv(mesh_position, MESH_REGION_SIZES);

    const auto & mesh_cache = m_mesh_caches[mesh_cache_position];

    if (!all(mesh_cache.position == mesh_cache_position))
        loadMeshCache(mesh_cache_position);

    return mesh_cache.infos[mesh_position].status;
}

//==============================================================================
void World::loadRegion(const iVec3 region_position)
{
    auto & region = m_regions[region_position];

    // return if already loaded
    if (all(region_position == region.position))
        return;

    if (region.needs_save)
        saveRegionToDrive(region.position);

    const std::string file_name = WORLD_ROOT + toString(region_position);
    std::ifstream file{ file_name, std::ifstream::binary };

    if (file.good())
    {
        Debug::print("Loading region ", toString(region_position));

        // get rid of old data
        std::free(region.data);

        region.position = region_position;

        file.read(reinterpret_cast<char *>(&region.size), sizeof(region.size));
        file.read(reinterpret_cast<char *>(region.metas.begin()), sizeof(region.metas));

        if (region.size > 0)
        {
            region.data = (Bytef *) std::malloc(static_cast<std::size_t>(region.size));
            file.read(reinterpret_cast<char *>(region.data), region.size);
        }
        else
        {
            region.data = nullptr;
        }

        region.container_size = region.size;

        if (!file.good()) std::runtime_error("Reading file failed.");
    }
    else
    {
        // get rid of old data
        std::free(region.data);

        // create region file
        region.position = region_position;
        region.size = 0;
        region.data = nullptr;
        region.container_size = 0;
        for (auto & i : region.metas) i = { 0, 0 };
    }

    region.needs_save = false;
}

//==============================================================================
void World::loadMeshCache(const iVec3 mesh_cache_position)
{
    auto & mesh_cache = m_mesh_caches[mesh_cache_position];

    // return if already loaded
    if (all(mesh_cache_position == mesh_cache.position))
        return;

    if (mesh_cache.needs_save)
        saveMeshCacheToDrive(mesh_cache.position);

    const std::string file_name = MESH_CACHE_ROOT + toString(mesh_cache_position);
    std::ifstream file{ file_name, std::ifstream::binary };

    if (file.good())
    {
        Debug::print("Loading mesh cache ", toString(mesh_cache_position));

        // get rid of old data
        std::free(mesh_cache.data);

        mesh_cache.position = mesh_cache_position;

        file.read(reinterpret_cast<char *>(&mesh_cache.size), sizeof(mesh_cache.size));
        file.read(reinterpret_cast<char *>(mesh_cache.infos.begin()), sizeof(mesh_cache.infos));

        if (mesh_cache.size > 0)
        {
            mesh_cache.data = (Bytef *) std::malloc(static_cast<std::size_t>(mesh_cache.size));
            file.read(reinterpret_cast<char *>(mesh_cache.data), mesh_cache.size);
        }
        else
        {
            mesh_cache.data = nullptr;
        }

        mesh_cache.container_size = mesh_cache.size;

        if (!file.good()) std::runtime_error("Reading file failed.");
    }
    else
    {
        // get rid of old data
        std::free(mesh_cache.data);

        // construct new mesh cache
        for (auto & i : mesh_cache.infos)
            i = { MeshCache::Status::UNKNOWN, 0, 0, 0 };

        mesh_cache.position = mesh_cache_position;
        mesh_cache.size = 0;
        mesh_cache.container_size = 0;
        mesh_cache.data = nullptr;
    }

    mesh_cache.needs_save = false;
}

//==============================================================================
void World::loadChunkToChunkContainerNew(const iVec3 chunk_position, Block * const chunk, iVec3 * const chunk_container_meta)
{
    // if already loaded
    if (all(*chunk_container_meta == chunk_position))
        return;

    const auto region_position = floorDiv(chunk_position, CHUNK_REGION_SIZES);
    auto & chunk_region = m_regions[region_position];
    assert(all(chunk_region.position == region_position) && "Assuming that correct region is already loaded.");

    auto & chunk_meta = chunk_region.metas[chunk_position];

    // update mesh status // TODO: do this when sure that chunk is present and can be loaded
    *chunk_container_meta = chunk_position;

    // chunk must exist
    assert(chunk_meta.size != 0 && "Want to load nonexisting chunk.");

    // load chunk from region

    {
        // TODO: no need for locking if everything is correctly implemented (aka. realloc is removed)
        // std::unique_lock<std::mutex>{ chunk_region.write_lock };

        // address of first block
        auto * beginning_of_chunk = chunk;

        // load chunk from region
        uLongf destination_length = static_cast<uLongf>(CHUNK_DATA_SIZE);

        const auto * source = chunk_region.data + chunk_meta.offset;

        auto result = uncompress(
            reinterpret_cast<Bytef *>(beginning_of_chunk), &destination_length,
            source, static_cast<uLongf>(chunk_meta.size)
        );

        assert(result == Z_OK && destination_length == CHUNK_DATA_SIZE && "Error in decompression.");
    }
}

//==============================================================================
void World::loadChunkToChunkContainer(const iVec3 chunk_position)
{
    auto & chunk_status = m_chunk_statuses[chunk_position];

    // if already loaded
    if (all(chunk_status.position == chunk_position))
        return;

    const auto region_position = floorDiv(chunk_position, CHUNK_REGION_SIZES);
    const auto & chunk_region = m_regions[region_position];

    // save previous chunk
    if (chunk_status.needs_save)
        saveChunkToRegion(chunk_status.position);

    // load region of new chunk
    if (!all(chunk_region.position == region_position))
        loadRegion(region_position);

    auto & chunk_meta = chunk_region.metas[chunk_position];

    // update mesh status
    chunk_status.position = chunk_position;
    chunk_status.needs_save = false;

    // generate new chunk
    if (chunk_meta.size == 0)
    {
        const auto from_block = chunk_position * CHUNK_SIZES;
        generateChunk(from_block, from_block + CHUNK_SIZES, WorldType::SINE);

        chunk_status.needs_save = true;

        // immediately save chunk to region
        saveChunkToRegion(chunk_position);
    }
    // load chunk from region
    else
    {
        // address of first block
        auto * beginning_of_chunk = &getBlock(chunk_position * CHUNK_SIZES);

        // load chunk from region
        uLongf destination_length = static_cast<uLongf>(CHUNK_DATA_SIZE);

        const auto * source = chunk_region.data + chunk_meta.offset;

        auto result = uncompress(
                reinterpret_cast<Bytef *>(beginning_of_chunk), &destination_length,
                source, static_cast<uLongf>(chunk_meta.size)
        );

        assert(result == Z_OK && destination_length == CHUNK_DATA_SIZE && "Error in decompression.");
    }
}

//==============================================================================
std::vector<Vertex> World::generateMeshNew(const iVec3 mesh_position, /*const iVec3 chunk_container_size,*/ Block * const chunks, iVec3 * const chunk_metas)
{
    const auto from_block = mesh_position * CHUNK_SIZES + MESH_OFFSETS;
    const auto to_block = from_block + CHUNK_SIZES;

    // copy-paste from load chunk range

    assert(all(from_block < to_block) && "From values must be lower than to values.");

    const auto chunk_position_from = floorDiv(from_block, CHUNK_SIZES);
    const auto chunk_position_to = floorDiv(to_block - 1, CHUNK_SIZES);

    iVec3 position;

    for (position(2) = chunk_position_from(2); position(2) <= chunk_position_to(2); ++position(2))
        for (position(1) = chunk_position_from(1); position(1) <= chunk_position_to(1); ++position(1))
            for (position(0) = chunk_position_from(0); position(0) <= chunk_position_to(0); ++position(0))
            {
                //loadChunkToChunkContainer(position);
                const auto index = positionToIndex(position, chunk_container_size) * CHUNK_SIZE;
                auto * this_chunk = chunks + index;
                auto * this_chunk_meta = chunk_metas + index;

                loadChunkToChunkContainerNew(position, this_chunk, this_chunk_meta);
            }

    class Getttter
    {
    public:
        Getttter(const Block * const w) : worldd{ w } {}
        // TODO: fix this positionToIndex is not the correct function (more processing of block_position needed. See getBlock() )
        const Block & operator () (const iVec3 block_position)
        {

            const auto block_index = positionToIndex(block_position, World::CHUNK_SIZES);

            const auto chunk_position = floorDiv(block_position, World::CHUNK_SIZES);
            const auto chunk_index = positionToIndex(chunk_position, World::chunk_container_size);

            return worldd[chunk_index * World::CHUNK_SIZE + block_index];
        }

    private:
        const Block * const worldd;

    };
    return generateMesh(from_block, to_block, Getttter{ chunks });

    // TODO: for debug: zero out (or magic number) const Block * const chunks after using
}

//==============================================================================
std::vector<Vertex> World::generateMeshOld(const iVec3 from_block, const iVec3 to_block)
{
    loadChunkRange(from_block - MESH_BORDER_REQUIRED_SIZE, to_block + MESH_BORDER_REQUIRED_SIZE);
    return generateMesh(from_block, to_block, BlockGetter{ this });
}

//==============================================================================
template<typename GetBlock>
std::vector<Vertex> World::generateMesh(const iVec3 from_block, const iVec3 to_block, GetBlock blockGet)
{
    iVec3 position;
    std::vector<Vertex> mesh;

    for (position(2) = from_block(2); position(2) < to_block(2); ++position(2))
        for (position(1) = from_block(1); position(1) < to_block(1); ++position(1))
            for (position(0) = from_block(0); position(0) < to_block(0); ++position(0))
            {
                auto block = blockGet(position);

                if (block.isEmpty()) continue;

                // X + 1
                if (blockGet({ position(0) + 1, position(1), position(2) }).isEmpty())
                {
                    const bool aos[8]{
                            !blockGet({ position(0) + 1, position(1) - 1, position(2)     }).isEmpty(),
                            !blockGet({ position(0) + 1, position(1)    , position(2) - 1 }).isEmpty(),
                            !blockGet({ position(0) + 1, position(1) + 1, position(2)     }).isEmpty(),
                            !blockGet({ position(0) + 1, position(1)    , position(2) + 1 }).isEmpty(),

                            !blockGet({ position(0) + 1, position(1) - 1, position(2) - 1 }).isEmpty(),
                            !blockGet({ position(0) + 1, position(1) - 1, position(2) + 1 }).isEmpty(),
                            !blockGet({ position(0) + 1, position(1) + 1, position(2) + 1 }).isEmpty(),
                            !blockGet({ position(0) + 1, position(1) + 1, position(2) - 1 }).isEmpty()
                    };

                    const ucVec4 ao_result = static_cast<unsigned char>(UCHAR_MAX) - ucVec4{
                            vertexAO(aos[0], aos[3], aos[5]),
                            vertexAO(aos[2], aos[3], aos[6]),
                            vertexAO(aos[0], aos[1], aos[4]),
                            vertexAO(aos[2], aos[1], aos[7])
                    } * SHADDOW_STRENGTH;

                    mesh.push_back({ { position(0) + 1, position(1)    , position(2) + 1 }, block.get(), ao_result });
                    mesh.push_back({ { position(0) + 1, position(1)    , position(2)     }, block.get(), ao_result });
                    mesh.push_back({ { position(0) + 1, position(1) + 1, position(2)     }, block.get(), ao_result });
                    mesh.push_back({ { position(0) + 1, position(1) + 1, position(2) + 1 }, block.get(), ao_result });
                }

                // X - 1
                if (blockGet({ position(0) - 1, position(1), position(2) }).isEmpty())
                {
                    const bool aos[8]{
                            !blockGet({ position(0) - 1, position(1) - 1, position(2)     }).isEmpty(),
                            !blockGet({ position(0) - 1, position(1)    , position(2) - 1 }).isEmpty(),
                            !blockGet({ position(0) - 1, position(1) + 1, position(2)     }).isEmpty(),
                            !blockGet({ position(0) - 1, position(1)    , position(2) + 1 }).isEmpty(),

                            !blockGet({ position(0) - 1, position(1) - 1, position(2) - 1 }).isEmpty(),
                            !blockGet({ position(0) - 1, position(1) - 1, position(2) + 1 }).isEmpty(),
                            !blockGet({ position(0) - 1, position(1) + 1, position(2) + 1 }).isEmpty(),
                            !blockGet({ position(0) - 1, position(1) + 1, position(2) - 1 }).isEmpty()
                    };

                    const ucVec4 ao_result = static_cast<unsigned char>(UCHAR_MAX) - ucVec4{
                            vertexAO(aos[0], aos[1], aos[4]),
                            vertexAO(aos[2], aos[1], aos[7]),
                            vertexAO(aos[0], aos[3], aos[5]),
                            vertexAO(aos[2], aos[3], aos[6])
                    } * SHADDOW_STRENGTH;

                    mesh.push_back({ { position(0)    , position(1)    , position(2)     }, block.get(), ao_result });
                    mesh.push_back({ { position(0)    , position(1)    , position(2) + 1 }, block.get(), ao_result });
                    mesh.push_back({ { position(0)    , position(1) + 1, position(2) + 1 }, block.get(), ao_result });
                    mesh.push_back({ { position(0)    , position(1) + 1, position(2)     }, block.get(), ao_result });
                }

                // Y + 1
                if (blockGet({ position(0), position(1) + 1, position(2) }).isEmpty())
                {
                    const bool aos[8]{
                            !blockGet({ position(0) - 1, position(1) + 1, position(2)     }).isEmpty(),
                            !blockGet({ position(0)    , position(1) + 1, position(2) - 1 }).isEmpty(),
                            !blockGet({ position(0) + 1, position(1) + 1, position(2)     }).isEmpty(),
                            !blockGet({ position(0)    , position(1) + 1, position(2) + 1 }).isEmpty(),

                            !blockGet({ position(0) - 1, position(1) + 1, position(2) - 1 }).isEmpty(),
                            !blockGet({ position(0) - 1, position(1) + 1, position(2) + 1 }).isEmpty(),
                            !blockGet({ position(0) + 1, position(1) + 1, position(2) + 1 }).isEmpty(),
                            !blockGet({ position(0) + 1, position(1) + 1, position(2) - 1 }).isEmpty()
                    };

                    const ucVec4 ao_result = static_cast<unsigned char>(UCHAR_MAX) - ucVec4{
                            vertexAO(aos[2], aos[1], aos[7]),
                            vertexAO(aos[2], aos[3], aos[6]),
                            vertexAO(aos[0], aos[1], aos[4]),
                            vertexAO(aos[0], aos[3], aos[5])
                    } * SHADDOW_STRENGTH;

                    mesh.push_back({ { position(0) + 1, position(1) + 1, position(2)     }, block.get(), ao_result });
                    mesh.push_back({ { position(0)    , position(1) + 1, position(2)     }, block.get(), ao_result });
                    mesh.push_back({ { position(0)    , position(1) + 1, position(2) + 1 }, block.get(), ao_result });
                    mesh.push_back({ { position(0) + 1, position(1) + 1, position(2) + 1 }, block.get(), ao_result });
                }

                // Y - 1
                if (blockGet({ position(0), position(1) - 1, position(2) }).isEmpty())
                {
                    const bool aos[8]{
                            !blockGet({ position(0) - 1, position(1) - 1, position(2)     }).isEmpty(),
                            !blockGet({ position(0)    , position(1) - 1, position(2) - 1 }).isEmpty(),
                            !blockGet({ position(0) + 1, position(1) - 1, position(2)     }).isEmpty(),
                            !blockGet({ position(0)    , position(1) - 1, position(2) + 1 }).isEmpty(),

                            !blockGet({ position(0) - 1, position(1) - 1, position(2) - 1 }).isEmpty(),
                            !blockGet({ position(0) - 1, position(1) - 1, position(2) + 1 }).isEmpty(),
                            !blockGet({ position(0) + 1, position(1) - 1, position(2) + 1 }).isEmpty(),
                            !blockGet({ position(0) + 1, position(1) - 1, position(2) - 1 }).isEmpty()
                    };

                    const ucVec4 ao_result = static_cast<unsigned char>(UCHAR_MAX) - ucVec4{
                            vertexAO(aos[0], aos[1], aos[4]),
                            vertexAO(aos[0], aos[3], aos[5]),
                            vertexAO(aos[2], aos[1], aos[7]),
                            vertexAO(aos[2], aos[3], aos[6])
                    } * SHADDOW_STRENGTH;

                    mesh.push_back({ { position(0)    , position(1)    , position(2)     }, block.get(), ao_result });
                    mesh.push_back({ { position(0) + 1, position(1)    , position(2)     }, block.get(), ao_result });
                    mesh.push_back({ { position(0) + 1, position(1)    , position(2) + 1 }, block.get(), ao_result });
                    mesh.push_back({ { position(0)    , position(1)    , position(2) + 1 }, block.get(), ao_result });
                }

                // Z + 1
                if (blockGet({ position(0), position(1), position(2) + 1 }).isEmpty())
                {
                    const bool aos[8]{
                            !blockGet({ position(0) - 1, position(1)    , position(2) + 1 }).isEmpty(),
                            !blockGet({ position(0)    , position(1) - 1, position(2) + 1 }).isEmpty(),
                            !blockGet({ position(0) + 1, position(1)    , position(2) + 1 }).isEmpty(),
                            !blockGet({ position(0)    , position(1) + 1, position(2) + 1 }).isEmpty(),

                            !blockGet({ position(0) - 1, position(1) - 1, position(2) + 1 }).isEmpty(),
                            !blockGet({ position(0) - 1, position(1) + 1, position(2) + 1 }).isEmpty(),
                            !blockGet({ position(0) + 1, position(1) + 1, position(2) + 1 }).isEmpty(),
                            !blockGet({ position(0) + 1, position(1) - 1, position(2) + 1 }).isEmpty()
                    };

                    const ucVec4 ao_result = static_cast<unsigned char>(UCHAR_MAX) - ucVec4{
                            vertexAO(aos[0], aos[1], aos[4]),
                            vertexAO(aos[0], aos[3], aos[5]),
                            vertexAO(aos[2], aos[1], aos[7]),
                            vertexAO(aos[2], aos[3], aos[6])
                    } * SHADDOW_STRENGTH;

                    mesh.push_back({ { position(0)    , position(1)    , position(2) + 1 }, block.get(), ao_result });
                    mesh.push_back({ { position(0) + 1, position(1)    , position(2) + 1 }, block.get(), ao_result });
                    mesh.push_back({ { position(0) + 1, position(1) + 1, position(2) + 1 }, block.get(), ao_result });
                    mesh.push_back({ { position(0)    , position(1) + 1, position(2) + 1 }, block.get(), ao_result });
                }

                // Z - 1
                if (blockGet({ position(0), position(1), position(2) - 1 }).isEmpty())
                {
                    const bool aos[8]{
                            !blockGet({ position(0) - 1, position(1)    , position(2) - 1 }).isEmpty(),
                            !blockGet({ position(0)    , position(1) - 1, position(2) - 1 }).isEmpty(),
                            !blockGet({ position(0) + 1, position(1)    , position(2) - 1 }).isEmpty(),
                            !blockGet({ position(0)    , position(1) + 1, position(2) - 1 }).isEmpty(),

                            !blockGet({ position(0) - 1, position(1) - 1, position(2) - 1 }).isEmpty(),
                            !blockGet({ position(0) - 1, position(1) + 1, position(2) - 1 }).isEmpty(),
                            !blockGet({ position(0) + 1, position(1) + 1, position(2) - 1 }).isEmpty(),
                            !blockGet({ position(0) + 1, position(1) - 1, position(2) - 1 }).isEmpty()
                    };

                    const ucVec4 ao_result = static_cast<unsigned char>(UCHAR_MAX) - ucVec4{
                            vertexAO(aos[2], aos[1], aos[7]),
                            vertexAO(aos[2], aos[3], aos[6]),
                            vertexAO(aos[0], aos[1], aos[4]),
                            vertexAO(aos[0], aos[3], aos[5])
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
unsigned char World::vertexAO(const bool side_a, const bool side_b, const bool corner)
{
  if (side_a && side_b) return 3;

  return
          static_cast<unsigned char>(side_a) +
          static_cast<unsigned char>(side_b) +
          static_cast<unsigned char>(corner);
}

//==============================================================================
void World::generateChunk(const iVec3 from_block, const iVec3 to_block, const WorldType world_type)
{
    switch(world_type)
    {
        case WorldType::SINE: sineChunk(from_block, to_block); break;
        case WorldType::SMALL_BLOCK: smallBlockChunk(from_block, to_block); break;
        case WorldType::FLOOR: floorTilesChunk(from_block, to_block); break;
        case WorldType::EMPTY: default: emptyChunk(from_block, to_block); break;
    }
}

//==============================================================================
void World::generateChunkNew(Block *destination, const iVec3 from_block, const iVec3 to_block, const WorldType world_type)
{
    switch(world_type)
    {
        case WorldType::SINE: sineChunkNew(destination, from_block, to_block); break;
        case WorldType::EMPTY: emptyChunkNew(destination, from_block, to_block); break;
        default: throw "Not implemented."; break;
    }
}

//==============================================================================
void World::emptyChunk(const iVec3 from_block, const iVec3 to_block)
{
    iVec3 position;

    for (position(2) = from_block(2); position(2) < to_block(2); ++position(2))
        for (position(1) = from_block(1); position(1) < to_block(1); ++position(1))
            for (position(0) = from_block(0); position(0) < to_block(0); ++position(0))
                getBlock(position) = Block{ 0 };
}

//==============================================================================
void World::emptyChunkNew(Block * destination, const iVec3 from_block, const iVec3 to_block)
{
    iVec3 position;
    int i = 0;

    for (position(2) = from_block(2); position(2) < to_block(2); ++position(2))
        for (position(1) = from_block(1); position(1) < to_block(1); ++position(1))
            for (position(0) = from_block(0); position(0) < to_block(0); ++position(0))
                destination[i++] = Block{ 0 };
}

//==============================================================================
void World::floorTilesChunk(const iVec3 from_block, const iVec3 to_block)
{
    const iVec3 chunk_position = floorMod(floorDiv(from_block, CHUNK_SIZES), iVec3{ 2, 2, 2 });
    const bool even_chunk = static_cast<bool>(chunk_position(0) ^ chunk_position(2));

    iVec3 position;

    for (position(2) = from_block(2); position(2) < to_block(2); ++position(2))
        for (position(1) = from_block(1); position(1) < to_block(1); ++position(1))
            for (position(0) = from_block(0); position(0) < to_block(0); ++position(0))
                getBlock(position) = position(1) > 0 ? Block{ 0 } : even_chunk ? Block{ 1 } : Block{ 2 };
}

//==============================================================================
void World::smallBlockChunk(const iVec3 from_block, const iVec3 to_block)
{
    iVec3 position;

    for (position(2) = from_block(2); position(2) < to_block(2); ++position(2))
        for (position(1) = from_block(1); position(1) < to_block(1); ++position(1))
            for (position(0) = from_block(0); position(0) < to_block(0); ++position(0))
                getBlock(position) = Block{ 0 };

    const iVec3 new_from_block = from_block + CHUNK_SIZES / 2 - 2;
    const iVec3 new_to_block = from_block + CHUNK_SIZES / 2 + 2;

    for (position(2) = new_from_block(2); position(2) < new_to_block(2); ++position(2))
        for (position(1) = new_from_block(1); position(1) < new_to_block(1); ++position(1))
            for (position(0) = new_from_block(0); position(0) < new_to_block(0); ++position(0))
                getBlock(position) = Block{ 1 };
}

//==============================================================================
void World::sineChunk(const iVec3 from_block, const iVec3 to_block)
{
  iVec3 position;

  for (position(2) = from_block(2); position(2) < to_block(2); ++position(2))
    for (position(1) = from_block(1); position(1) < to_block(1); ++position(1))
      for (position(0) = from_block(0); position(0) < to_block(0); ++position(0))
      {
          auto & block = getBlock(position);

          if (std::sin(position(0) * 0.1f) * std::sin(position(2) * 0.1f) * 10.0f > static_cast<float>(position(1)))
          {
              auto random_value = std::rand() % 16;

              if (random_value < 3)
                  block = Block{ 3 };
              else if (random_value < 11)
                  block = Block{ 2 };
              else
                  block = Block{ 1 };
          }
          else
              block = Block{ 0 };

      }
}

//==============================================================================
void World::sineChunkNew(Block * destination, const iVec3 from_block, const iVec3 to_block)
{
    iVec3 position;
    int i = 0;

    for (position(2) = from_block(2); position(2) < to_block(2); ++position(2))
        for (position(1) = from_block(1); position(1) < to_block(1); ++position(1))
            for (position(0) = from_block(0); position(0) < to_block(0); ++position(0))
            {
                auto & block = destination[i++];

                if (std::sin(position(0) * 0.1f) * std::sin(position(2) * 0.1f) * 10.0f > static_cast<float>(position(1)))
                {
                    auto random_value = std::rand() % 16;

                    if (random_value < 3)
                        block = Block{ 3 };
                    else if (random_value < 11)
                        block = Block{ 2 };
                    else
                        block = Block{ 1 };
                }
                else
                    block = Block{ 0 };

            }
}

//==============================================================================
bool World::removeOutOfRangeMeshes(const iVec3 center_mesh)
{
    // remove out of range meshes
    auto count = m_loaded_meshes.size();

    bool completed = true;

    for (std::size_t i = 0; i < count;)
    {
        const auto mesh_position = m_loaded_meshes[i].position;

        const auto mesh_index = positionToIndex(mesh_position, MESH_CONTAINER_SIZES);

        if (!inRange(center_mesh, mesh_position, SQUARE_REMOVE_DISTANCE))
        {
            if (!m_loaded_meshes[i].empty)
            {
                auto * command = m_commands.initPush();

                if (command != nullptr)
                {
                    command->type = Command::Type::REMOVE;
                    command->index = mesh_index;
                    m_commands.commitPush();
                }
                else
                {
                    Debug::print("Command buffer is full.");
                    completed = false;
                    break;
                }
            }
            m_loaded_meshes[i] = m_loaded_meshes[--count];

            assert(m_mesh_loaded[mesh_position] == Status::LOADED && "Mesh must be loaded in order to be unloaded.");
            m_mesh_loaded[mesh_position] = Status::UNLOADED;
        }
        else
        {
            ++i;
        }

    }
    m_loaded_meshes.resize(count);

    return completed;
}


//==============================================================================
void World::multiThreadMeshLoader(const int thread_id)
{
    std::unique_ptr<Block[]> container{ std::make_unique<Block[]>(CHUNK_SIZE) };
    static_assert(CSIZE == 16 && MSIZE == 16 && MOFF == 8, "Temporary.");
    constexpr size_t SZEE = CHUNK_SIZE * product(chunk_container_size);
    std::unique_ptr<iVec3[]> chunk_positions{ std::make_unique<iVec3[]>(SZEE) }; // TODO: correct algorithm for determining needed size
    std::unique_ptr<Block[]> chunks{ std::make_unique<Block[]>(SZEE) }; // TODO: correct algorithm for determining needed size

    // initialize this stuff
    for (std::size_t i = 0; i < SZEE; ++i)
        chunk_positions[i] = { 0, 0, 0 };
    chunk_positions[0] = { 1, 0, 0 };

    const iVec3 center_mesh = iVec3{ 0, 0, 0 }; // dummys
    const iVec3 center_chunk = iVec3{ 0, 0, 0 }; // dummys

    while (!m_quit)
    //while (true)
    {
        auto current_index = m_iterator_index.fetch_add(1);
        auto task = m_iterator.m_points[current_index];
        assert(current_index < m_iterator.m_points.size() && "Out of bounds access.");

        switch (task.task)
        {
            case decltype(m_iterator)::Task::SYNC:
            {
                m_barrier.wait();
            }
            break;
            case decltype(m_iterator)::Task::LAST_SYNC_AND_LOAD_REGION:
            {
                // TODO: figure out how to paralelize region loading
                const auto from_chunk = (task.position * -1) + 1;
                const auto to_chunk = task.position;

                const auto from_region = floorDiv(from_chunk, CHUNK_REGION_SIZES);
                const auto to_region = floorDiv(to_chunk, CHUNK_REGION_SIZES);

                iVec3 position;

                for (position(2) = from_region(2); position(2) <= to_region(2); ++position(2))
                    for (position(1) = from_region(1); position(1) <= to_region(1); ++position(1))
                        for (position(0) = from_region(0); position(0) <= to_region(0); ++position(0))
                            loadRegion(position);

                m_barrier.wait();
            }
            break;
            case decltype(m_iterator)::Task::GENERATE_CHUNK:
            {
                const iVec3 chunk_position{ task.position + center_chunk }; // warning when copy pasting center_chunk and center_mesh
                const iVec3 from{ chunk_position * CHUNK_SIZES };
                const iVec3 to{ from + CHUNK_SIZES };

                generateChunkNew(container.get(), from, to, WorldType::SINE);
                saveChunkToRegionNew(container.get(), chunk_position);
            }
            break;
            case decltype(m_iterator)::Task::GENERATE_MESH:
            {
                // TODO: implement multi threaded command queue

                auto * command = m_commands.initPush();

                // buffer is full
                while (command == nullptr)
                {
                    std::cout << "Buffer stall. Sleeping" << std::endl;
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    command = m_commands.initPush();
                }

                const auto current_mesh_position = task.position + center_mesh;
                const auto mesh = generateMeshNew(current_mesh_position, /*chunk_container_size,*/ chunks.get(), chunk_positions.get());

                if (mesh.size() == 0)
                {
                    m_commands.discardPush();
                }
                else
                {
                    command->type = Command::Type::UPLOAD;
                    command->index = positionToIndex(current_mesh_position, MESH_CONTAINER_SIZES);
                    command->position = current_mesh_position;
                    command->mesh = mesh;

                    m_commands.commitPush();
                }
            }
            break;
            case decltype(m_iterator)::Task::END_MARKER:
            {
                assert (current_index == m_iterator.m_points.size() - 1 && "End marker should only appear at the end of the hardcoded iterator.");

                m_iterator_index = 0;
                m_barrier.wait();
                // return is temporary and is here to prevent errors (only works if only 1 thread is running)
                return; // TODO: remove that that's temporary because checking if chunk is already loaded is not implmented yet and because REMOVE commant to renderer is also not implemented yet

            }
            break;
            default:
            {
                assert(false && "Invalid command.");
            }
            break;
        }
    }

    m_barrier.disable();
}

//==============================================================================
void World::meshLoader()
{
    while (!m_quit)
    {
        Debug::print("New loader thread loop.");

        m_moved_center_mesh = false; // reset flag

        const iVec3 center_mesh = m_center_mesh.load();

        bool buffer_stall = !removeOutOfRangeMeshes(center_mesh);

        if (!buffer_stall)
        {
            for (const auto & iterator : m_iterator.m_points)
            {
                if (m_moved_center_mesh || m_quit)
                    break;

                if (iterator.task != decltype(m_iterator)::Task::GENERATE_MESH)
                  continue;

                const auto current_mesh_position = iterator.position + center_mesh;

                assert(inRange(center_mesh, current_mesh_position, SQUARE_RENDER_DISTANCE) &&
                       "Iterator constructor should make sure this does not happen.");

                if (m_mesh_loaded[current_mesh_position] != Status::UNLOADED)
                    continue;

                const auto mesh_status = getMeshStatus(current_mesh_position);

                if (mesh_status == MeshCache::Status::EMPTY)
                {
                    // update mesh state
                    m_mesh_loaded[current_mesh_position] = Status::LOADED;
                    m_loaded_meshes.push_back({current_mesh_position, true});
                    continue;
                }

                auto * command = m_commands.initPush();

                decltype(command->mesh) mesh;

                // buffer is full
                if (command == nullptr)
                {
                    buffer_stall = true;
                    break;
                }

                assert(mesh_status == MeshCache::Status::NON_EMPTY || mesh_status == MeshCache::Status::UNKNOWN && "Invalid mesh status.");

                if (mesh_status == MeshCache::Status::NON_EMPTY)
                {
                    mesh = loadMesh(current_mesh_position);
                    assert(mesh.size() != 0 && "Loaded empty mesh?");
                }
                else if (mesh_status == MeshCache::Status::UNKNOWN)
                {
                    const auto from_block = current_mesh_position * MESH_SIZES + MESH_OFFSETS;
                    const auto to_block = from_block + MESH_SIZES;

                    mesh = generateMeshOld(from_block, to_block);
                    saveMeshToMeshCache(current_mesh_position, mesh);
                }

                if (mesh.size() > 0)
                {
                    command->type = Command::Type::UPLOAD;
                    command->index = positionToIndex(current_mesh_position, MESH_CONTAINER_SIZES);
                    command->position = current_mesh_position;
                    command->mesh = mesh;
                    m_commands.commitPush();
                }
                else
                {
                    m_commands.discardPush();
                }

                // update mesh state
                m_mesh_loaded[current_mesh_position] = Status::LOADED;
                m_loaded_meshes.push_back({current_mesh_position, mesh.size() == 0});
            }
        }

        if (buffer_stall)
        {
            Debug::print("Buffer stall. Loader sleeping for ", STALL_SLEEP_MS, "ms.");
            std::this_thread::sleep_for(std::chrono::milliseconds(STALL_SLEEP_MS));
        }
        else
        {
            while (!m_moved_center_mesh && !m_quit)
            {
                Debug::print("Loader sleeping for ", SLEEP_MS, "ms.");
                std::this_thread::sleep_for(std::chrono::milliseconds(SLEEP_MS));
            }
        }
    }

    Debug::print("Exited loader thread.");
}

//==============================================================================
bool World::inRange(const iVec3 center, const iVec3 position, const int max_square_distance)
{
    const iVec3 distances = position - center;

    const int square_distance = dot(distances, distances);

    return square_distance <= max_square_distance;
}

//==============================================================================
void World::executeRendererCommands(const int max_command_count)
{
    auto commands_executed = 0;

    while (commands_executed++ < max_command_count)
    {
        Command * command = m_commands.initPop();

        if (command == nullptr)
            return;

        switch (command->type)
        {
            case Command::Type::REMOVE:
            {
                const auto & mesh_data = m_meshes.get(command->index)->data.mesh;
#if 1
                assert(mesh_data.VBO && mesh_data.VAO && "Should not be 0.");
                m_unused_buffers.push({ mesh_data.VAO, mesh_data.VBO });
#else
                glDeleteBuffers(1, &mesh_data.VBO);
                glDeleteVertexArrays(1, &mesh_data.VAO);
#endif
                m_meshes.del(command->index);
            }
            break;
            case Command::Type::UPLOAD:
            {
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

                    glVertexAttribIPointer(0, 3, GL_INT, sizeof(Vertex), (GLvoid *) (0));
                    glVertexAttribIPointer(1, 1, GL_INT, sizeof(Vertex), (GLvoid *) (sizeof(Vertex::position)));
                    glVertexAttribIPointer(2, 4, GL_UNSIGNED_BYTE, sizeof(Vertex), (GLvoid *) (sizeof(Vertex::position) + sizeof(Vertex::type)));
                    glEnableVertexAttribArray(0);
                    glEnableVertexAttribArray(1);
                    glEnableVertexAttribArray(2);

                    glBindVertexArray(0);
                }

                assert(command->mesh.size() > 0 && "Mesh size must be over 0.");
                assert(VAO != 0 && VBO != 0 && "Failed to generate VAO and/or VBO for mesh.");

                // upload mesh
                glBufferData(GL_ARRAY_BUFFER, command->mesh.size() * sizeof(command->mesh[0]), command->mesh.data(), GL_STATIC_DRAW);
                // fast multiply by 1.5
                const auto EBO_size = static_cast<int>((command->mesh.size() >> 1) + command->mesh.size());
                QuadEBO::resize(EBO_size);

                m_meshes.add(command->index, { { VAO, VBO, EBO_size }, command->position });

                // this does not deallocate and popping command queue does not call destructor
                command->mesh.clear();
            }
            break;
            default:
            {
                assert(0 && "Unknown command.");
            }
            break;
        }

        m_commands.commitPop();
    }
}

//==============================================================================
void World::draw(const iVec3 new_center, const fVec4 frustum_planes[6])
{
    const auto center_mesh = floorDiv(new_center - MESH_OFFSETS, MESH_SIZES);
    const auto old_center_mesh = m_center_mesh.exchange(center_mesh);

    if (!all(old_center_mesh == center_mesh))
        m_moved_center_mesh = true;

    executeRendererCommands(MAX_COMMANDS_PER_FRAME);

    // render
    for (const auto & m : m_meshes)
    {
        // only render if not too far away
#if 0
        if (!inRange(center_mesh, m.data.position, SQUARE_RENDER_DISTANCE))
            continue;
#endif

        // only render if in frustum
        if (!meshInFrustum(frustum_planes, m.data.position * MESH_SIZES + MESH_OFFSETS))
            continue;

        const auto & mesh_data = m.data.mesh;

        assert(mesh_data.size <= QuadEBO::size() && mesh_data.size > 0);
        assert(mesh_data.VAO != 0 && mesh_data.VBO != 0 && "VAO and/or VBO not loaded.");

        glBindVertexArray(mesh_data.VAO);
        glDrawElements(GL_TRIANGLES, mesh_data.size, QuadEBO::type(), 0);
        glBindVertexArray(0);
    }
}

//==============================================================================
void World::exitLoaderThread()
{
/*
    assert(m_loader_thread.joinable() && "Loader thread is not joinable.");

    m_quit = true;

    if (!m_loader_thread.joinable())
        return;

    m_loader_thread.join();
    */

    m_quit = true;

    for (std::size_t i = 0; i < THREAD_COUNT; ++i)
    {
        assert(m_workers[i].joinable() && "Why is this not joinable?");
        m_workers[i].join();
    }
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
void World::saveChunkToRegionNew(const Block * const source, const iVec3 chunk_position)
{
    const auto region_position = floorDiv(chunk_position, CHUNK_REGION_SIZES);
    auto & region = m_regions[region_position];

    std::unique_lock<std::mutex> lock{ region.write_lock };

    assert(all(region.position == region_position) && "Incorrect region loaded.");

    uLong destination_length = compressBound(static_cast<uLong>(CHUNK_DATA_SIZE));

    // resize if potentially out of space
    if (region.size + static_cast<int>(destination_length) > region.container_size)
    {
        Debug::print("Reallocating region container.");
        const auto new_container_size = region.container_size + std::max(REGION_DATA_SIZE_FACTOR, static_cast<int>(destination_length));
        auto * const new_data = (Bytef*)std::realloc(region.data, static_cast<std::size_t>(new_container_size));
        if (new_data == nullptr) throw 0;
        region.container_size = new_container_size;
        region.data = new_data;
    }

    Bytef * const destination = region.data + region.size; // TODO: figure out if this is size in char, Bytef or Block

    const auto result = compress2(destination, &destination_length, reinterpret_cast<const Bytef *>(source), static_cast<uLong>(CHUNK_DATA_SIZE), Z_BEST_SPEED);

    assert(result == Z_OK && "Error compressing chunk.");
    assert(destination_length <= compressBound(static_cast<uLong>(CHUNK_DATA_SIZE)) && "ZLib lied about the maximum possible size of compressed data.");

    auto & chunk_meta = region.metas[chunk_position];

    chunk_meta.size = static_cast<int>(destination_length);
    chunk_meta.offset = region.size;

    region.size += static_cast<int>(destination_length);
    region.needs_save = true;
}

//==============================================================================
void World::saveChunkToRegion(const iVec3 chunk_position)
{
    const auto region_position = floorDiv(chunk_position, CHUNK_REGION_SIZES);
    auto & region = m_regions[region_position];

    if (!m_chunk_statuses[chunk_position].needs_save)
        return;

    assert(all(m_chunk_statuses[chunk_position].position == chunk_position) && "Broken data structure.");

    // load correct region
    loadRegion(region_position);

    uLong destination_length = compressBound(static_cast<uLong>(CHUNK_DATA_SIZE));

    assert(region.size <= region.container_size && "Capacity should always be more or equal as size.");

    // resize if potentially out of space
    if (region.size + static_cast<int>(destination_length) > region.container_size)
    {
        Debug::print("Reallocating region container.");
        region.container_size += REGION_DATA_SIZE_FACTOR < static_cast<int>(destination_length) ? static_cast<int>(destination_length) : REGION_DATA_SIZE_FACTOR;
        // assigning to same is bad, in case realloc fails
        region.data = (Bytef*)std::realloc(region.data, static_cast<std::size_t>(region.container_size));
    }

    const auto * beginning_of_chunk = &getBlock(chunk_position * CHUNK_SIZES); // address of first block
    Bytef * destination = region.data + region.size;

    // compress and save chunk to region
    auto result = compress2(destination, &destination_length, reinterpret_cast<const Bytef *>(beginning_of_chunk), static_cast<uLong>(CHUNK_DATA_SIZE), Z_BEST_SPEED);

    assert(result == Z_OK && "Error compressing chunk.");
    assert(destination_length <= compressBound(static_cast<uLong>(CHUNK_DATA_SIZE)) && "ZLib lied about the maximum possible size of compressed data.");

    auto & chunk_meta = region.metas[chunk_position];

    chunk_meta.size = static_cast<int>(destination_length);
    chunk_meta.offset = region.size;

    region.size += static_cast<int>(destination_length);
    region.needs_save = true;
}

//==============================================================================
void World::saveRegionToDrive(const iVec3 region_position)
{
    const auto & region = m_regions[region_position];

    if (!region.needs_save)
        return;

    // save only if valid
    assert(all(region_position == region.position) && "Trying to save invalid region.");

    // save region
    Debug::print("Saving region ", toString(region.position));
    assert((region.size == 0 && region.data == nullptr || region.size > 0 && region.data != nullptr) && "Data structure is broken.");

    const std::string file_name = WORLD_ROOT + toString(region.position);
    std::ofstream file{ file_name, std::ofstream::out | std::ofstream::binary | std::ofstream::trunc };

    file.write(reinterpret_cast<const char *>(&region.size), sizeof(region.size));
    file.write(reinterpret_cast<const char *>(region.metas.begin()), sizeof(region.metas));
    if (region.size > 0)
        file.write(reinterpret_cast<const char *>(region.data), region.size);

    if (!file.good()) std::runtime_error("Writing file failed.");
}

//==============================================================================
void World::saveMeshCacheToDrive(const iVec3 mesh_cache_position)
{
    const auto & mesh_cache = m_mesh_caches[mesh_cache_position];

    if (!mesh_cache.needs_save)
        return;

    // save only if valid
    assert(all(mesh_cache_position == mesh_cache.position) && "Trying to save invalid mesh cache.");

    // save mesh cache
    Debug::print("Saving mesh cache ", toString(mesh_cache.position));
    assert((mesh_cache.size == 0 && mesh_cache.data == nullptr || mesh_cache.size > 0 && mesh_cache.data != nullptr) && "Data structure is broken.");

    const std::string file_name = MESH_CACHE_ROOT + toString(mesh_cache.position);
    std::ofstream file{ file_name, std::ofstream::out | std::ofstream::binary | std::ofstream::trunc };

    file.write(reinterpret_cast<const char *>(&mesh_cache.size), sizeof(mesh_cache.size));
    file.write(reinterpret_cast<const char *>(mesh_cache.infos.begin()), sizeof(mesh_cache.infos));
    if (mesh_cache.size > 0)
        file.write(reinterpret_cast<const char *>(mesh_cache.data), mesh_cache.size);

    if (!file.good()) std::runtime_error("Writing file failed.");
}

//==============================================================================
void World::saveMeshToMeshCache(const iVec3 mesh_position, const std::vector<Vertex> & mesh)
{
    const auto new_status = mesh.size() == 0 ? MeshCache::Status::EMPTY : MeshCache::Status::NON_EMPTY;

    const auto mesh_cache_position = floorDiv(mesh_position, MESH_REGION_SIZES);
    auto & mesh_cache = m_mesh_caches[mesh_cache_position];

    // load correct mesh cache
    loadMeshCache(mesh_cache_position);

    assert(all(mesh_cache.position == mesh_cache_position) && "Broken data structure.");

    // save new mesh status
    auto & mesh_info = mesh_cache.infos[mesh_position];
    assert(mesh_info.status != new_status && "Not an issue but if could indicate a bug.");
    mesh_info.status = new_status;
    mesh_cache.needs_save = true;

    if (new_status == MeshCache::Status::EMPTY)
    {
        mesh_info.vertex_count = 0;
        mesh_info.compressed_size = 0;
        mesh_info.offset = 0;
        return;
    }

    // compress mesh
    uLong destination_length = compressBound(static_cast<uLong>(mesh.size()) * sizeof(mesh[0]));

    assert(mesh_cache.size <= mesh_cache.container_size && "Capacity should always be more or equal as size.");

    // resize if potentially out of space
    if (mesh_cache.size + static_cast<int>(destination_length) > mesh_cache.container_size)
    {
        Debug::print("Reallocating mesh container.");
        mesh_cache.container_size += MESH_CACHE_DATA_SIZE_FACTOR < static_cast<int>(destination_length) ? static_cast<int>(destination_length) : MESH_CACHE_DATA_SIZE_FACTOR;
        mesh_cache.data = (Bytef*)std::realloc(mesh_cache.data, static_cast<std::size_t>(mesh_cache.container_size));
    }

    Bytef * destination = mesh_cache.data + mesh_cache.size;

    auto result = compress2(destination, &destination_length, reinterpret_cast<const Bytef *>(mesh.data()), static_cast<uLong>(mesh.size() * sizeof(mesh[0])), Z_BEST_SPEED);

    assert(result == Z_OK && "Error compressing mesh.");
    assert(destination_length <= compressBound(static_cast<uLong>(mesh.size() * sizeof(mesh[0]))) && "ZLib lied about the maximum possible size of compressed data.");

    mesh_info.compressed_size = static_cast<int>(destination_length);
    mesh_info.vertex_count = static_cast<int>(mesh.size());
    mesh_info.offset = mesh_cache.size;

    mesh_cache.size += static_cast<int>(destination_length);
    mesh_cache.needs_save = true;
}

//==============================================================================
std::vector<Vertex> World::loadMesh(const iVec3 mesh_position)
{
    const auto mesh_cache_position = floorDiv(mesh_position, MESH_REGION_SIZES);

    const auto & mesh_cache = m_mesh_caches[mesh_cache_position];
    const auto & mesh_info = mesh_cache.infos[mesh_position];

    if (!all(mesh_cache.position == mesh_cache_position))
        loadMeshCache(mesh_cache_position);

    // empty mesh
    assert(mesh_info.vertex_count > 0 && "Function should not be called for empty chunks.");

    std::vector<Vertex> mesh{ static_cast<size_t>(mesh_info.vertex_count) };

    uLongf destination_length = static_cast<uLongf>(mesh_info.vertex_count * sizeof(mesh[0]));

    const auto * source = mesh_cache.data + mesh_info.offset;

    auto result = uncompress(
            reinterpret_cast<Bytef *>(mesh.data()), &destination_length,
            source, static_cast<uLongf>(mesh_info.compressed_size)
    );

    assert(result == Z_OK && destination_length == mesh_info.vertex_count * sizeof(mesh[0]) && "Error in decompression.");

    return mesh;
}

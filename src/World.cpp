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
#include <stdlib.h>
#include <glm/gtc/noise.hpp>

static constexpr i32Vec3 INITIAL_CENTER_CHUNK{ 0, 0, 0 };

//==============================================================================
constexpr char World::WORLD_ROOT[];
constexpr char World::MESH_CACHE_ROOT[];
constexpr i32Vec3 World::CHUNK_SIZES;
constexpr i32Vec3 World::CHUNK_CONTAINER_SIZES;
constexpr i32Vec3 World::CHUNK_REGION_CONTAINER_SIZES;
constexpr i32Vec3 World::MESH_REGION_CONTAINER_SIZES;
constexpr i32Vec3 World::MESH_REGION_SIZES;
constexpr i32Vec3 World::CHUNK_REGION_SIZES;
constexpr int World::MESH_BORDER_REQUIRED_SIZE;
constexpr int World::REGION_DATA_SIZE_FACTOR;
constexpr unsigned char World::SHADDOW_STRENGTH;
constexpr i32Vec3 World::MESH_CONTAINER_SIZES;
constexpr i32Vec3 World::MESH_SIZES;
constexpr i32Vec3 World::MESH_OFFSETS;
constexpr i32Vec3 World::chunk_container_size;
constexpr int World::SLEEP_MS;
constexpr int World::STALL_SLEEP_MS;
constexpr i32Vec3 World::BLOCKS_RADIUSES;

//==============================================================================
World::World() :
//        m_reference_center{ 0, 0, 0 },
        //m_center{ { 0, 0, 0 } },
        m_center_mesh{ INITIAL_CENTER_CHUNK }, // TODO: update to correct position before first use in meshLoader
        m_loader_center{ INITIAL_CENTER_CHUNK },
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
#ifndef NEW_REGION_FORMAT
        for (auto & i2 : i.metas) i2 = { 0, 0 };
#else
        for (auto & i2 : i.metas)
        {
            // i2 = { 0, CType::NOWHERE, 0 };
            i2.location = nullptr;
            i2.size = 0;
            i2.loc = CType::NOWHERE;
            i2.offset_n = 0;
        }
#endif
        for (auto & i3 : i.mesh_statuses) i3 = Region::MStatus::UNKNOWN;

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
#ifndef NEW_REGION_FORMAT
            saveChunkToRegionOld(chunk_status.position);
#else
            // TODO: implement !!!!!!!!!
            //saveChunkToRegionNew(chunk_status.position);
            (void)0;
#endif
    // TODO: refactor
    // save all valid regions
    bool first_deelete_that = true;
    for (const auto & region : m_regions)
    {
        if (first_deelete_that)
        {
            first_deelete_that = false;

            if (all(region.position == i32Vec3{ 1, 0, 0 }))
                continue;
            else
#ifdef NEW_REGION_FORMAT
                saveRegionToDriveNew(region.position);
#else
                saveRegionToDriveOld(region.position);
#endif
        }
        else
        {
            if (!all(region.position == i32Vec3{ 0, 0, 0 }))
#ifdef NEW_REGION_FORMAT
                saveRegionToDriveNew(region.position);
#else
                saveRegionToDriveOld(region.position);
#endif
        }
    }

    // save all mesh caches to drive
    bool first_TODO_delete = true;
    for (const auto & mesh_cache : m_mesh_caches)
    {
//        if (mesh_cache.needs_save)
//            saveMeshCacheToDrive(mesh_cache.position);

        /*if (first_TODO_delete)
        {
            first_TODO_delete = false;
            saveMeshCacheToDrive(mesh_cache, { 0, 0, 0 });
        }
        else
        {
            if (all(mesh_cache.position == i32Vec3{ 0, 0, 0 }))
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
        auto & mesh_data = i->mesh;

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
void World::loadRegionNew(const i32Vec3 region_position)
{
    auto & region = m_regions[region_position];

    // return if already loaded
    if (all(region_position == region.position))
        return;

    if (region.needs_save)
        saveRegionToDriveNew(region.position);

    const std::string file_name = WORLD_ROOT + to_string(region_position);
    std::ifstream file{ file_name, std::ifstream::binary };

    if (file.good())
    {
        Debug::print("Loading region ", to_string(region_position));

        // get rid of old data
        std::free(region.data);
        region.data_memory.reset();

        region.position = region_position;

        file.read(reinterpret_cast<char *>(&region.size), sizeof(region.size));
        file.read(reinterpret_cast<char *>(region.metas.begin()), sizeof(region.metas));
        file.read(reinterpret_cast<char *>(region.mesh_statuses.begin()), sizeof(region.mesh_statuses));

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
        Debug::print("Initialize new region ", to_string(region_position));

        // get rid of old data
        std::free(region.data);
        region.data_memory.reset();

        // create region file
        region.position = region_position;
        region.size = 0;
        region.data = nullptr;
        region.container_size = 0;
        for (auto & i : region.metas) i = { 0, CType::NOWHERE, { 0, nullptr } };
        for (auto & i : region.mesh_statuses) i = Region::MStatus::UNKNOWN;
    }

    region.needs_save = false;
}

//==============================================================================
void World::loadChunkToChunkContainerNew(const i32Vec3 chunk_position, Block * const chunk, i32Vec3 * const chunk_container_meta)
{
//    Debug::print("Load chunk: ", to_string(chunk_position));

    // if already loaded
    if (all(*chunk_container_meta == chunk_position))
        return;

    const auto region_position = floor_div(chunk_position, CHUNK_REGION_SIZES);
    const auto & chunk_region = m_regions[region_position];
    assert(all(chunk_region.position == region_position) && "Assuming that correct region is already loaded.");

    const auto & chunk_meta = chunk_region.metas[chunk_position];

    // update mesh status // TODO: do this when sure that chunk is present and can be loaded
    *chunk_container_meta = chunk_position;

    // chunk must exist
    assert(chunk_meta.size != 0 && "Want to load nonexisting chunk.");

    // load chunk from region

    // TODO: no need for locking if everything is correctly implemented (aka. realloc is removed)
    // std::unique_lock<std::mutex>{ chunk_region.write_lock };

    if (chunk_meta.loc == CType::FILE)
    {
        // address of first block
        auto * beginning_of_chunk = chunk;

        // load chunk from region
        uLongf destination_length = static_cast<uLongf>(CHUNK_DATA_SIZE);

        const auto * source = chunk_region.data + chunk_meta.offset_n;

        auto result = uncompress(
            reinterpret_cast<Bytef *>(beginning_of_chunk), &destination_length,
            source, static_cast<uLongf>(chunk_meta.size)
        );

        assert(result == Z_OK && destination_length == CHUNK_DATA_SIZE && "Error in decompression.");
    }
    else if (chunk_meta.loc == CType::MEMORY)
    {
        //assert(false && "to do: impelment.");

        // address of first block
        auto * beginning_of_chunk = chunk;

        // load chunk from region
        uLongf destination_length = static_cast<uLongf>(CHUNK_DATA_SIZE);

        const auto * source = chunk_meta.location;

        auto result = uncompress(
            reinterpret_cast<Bytef *>(beginning_of_chunk), &destination_length,
            source, static_cast<uLongf>(chunk_meta.size)
        );

        assert(result == Z_OK && destination_length == CHUNK_DATA_SIZE && "Error in decompression.");
    }
    else
    {
        assert(false && "Loading chunk that does not exist.");
    }
}

//==============================================================================
std::vector<Vertex> World::generateMeshNew(const i32Vec3 mesh_position, /*const i32Vec3 chunk_container_size,*/ Block * const chunks, i32Vec3 * const chunk_metas)
{
    const auto from_block = mesh_position * CHUNK_SIZES + MESH_OFFSETS;
    const auto to_block = from_block + CHUNK_SIZES;

    // copy-paste from load chunk range

    assert(all(from_block < to_block) && "From values must be lower than to values.");

    const auto chunk_position_from = floor_div(from_block, CHUNK_SIZES);
    const auto chunk_position_to = floor_div(to_block - 1, CHUNK_SIZES);

    i32Vec3 position;

    for (position[2] = chunk_position_from[2]; position[2] <= chunk_position_to[2]; ++position[2])
        for (position[1] = chunk_position_from[1]; position[1] <= chunk_position_to[1]; ++position[1])
            for (position[0] = chunk_position_from[0]; position[0] <= chunk_position_to[0]; ++position[0])
            {
                //loadChunkToChunkContainer(position);
                const auto index = position_to_index(position, chunk_container_size) * CHUNK_SIZE;
                auto * this_chunk = chunks + index;
                auto * this_chunk_meta = chunk_metas + index;

                loadChunkToChunkContainerNew(position, this_chunk, this_chunk_meta);
            }

    class Getttter
    {
    public:
        Getttter(const Block * const w) : worldd{ w } {}
        // TODO: fix this position_to_index is not the correct function (more processing of block_position needed. See getBlock() )
        const Block & operator () (const i32Vec3 block_position)
        {

            const auto block_index = position_to_index(block_position, World::CHUNK_SIZES);

            const auto chunk_position = floor_div(block_position, World::CHUNK_SIZES);
            const auto chunk_index = position_to_index(chunk_position, World::chunk_container_size);

            return worldd[chunk_index * World::CHUNK_SIZE + block_index];
        }

    private:
        const Block * const worldd;

    };
    return generateMesh(from_block, to_block, Getttter{ chunks });

    // TODO: for debug: zero out (or magic number) const Block * const chunks after using
}

//==============================================================================
template<typename GetBlock>
std::vector<Vertex> World::generateMesh(const i32Vec3 from_block, const i32Vec3 to_block, GetBlock blockGet)
{
  i32Vec3 position;
    std::vector<Vertex> mesh;

    for (position[2] = from_block[2]; position[2] < to_block[2]; ++position[2])
        for (position[1] = from_block[1]; position[1] < to_block[1]; ++position[1])
            for (position[0] = from_block[0]; position[0] < to_block[0]; ++position[0])
            {
                auto block = blockGet(position);

                if (block.isEmpty()) continue;

                // X + 1
                if (blockGet({ position[0] + 1, position[1], position[2] }).isEmpty())
                {
                    const bool aos[8]{
                            !blockGet({ position[0] + 1, position[1] - 1, position[2]     }).isEmpty(),
                            !blockGet({ position[0] + 1, position[1]    , position[2] - 1 }).isEmpty(),
                            !blockGet({ position[0] + 1, position[1] + 1, position[2]     }).isEmpty(),
                            !blockGet({ position[0] + 1, position[1]    , position[2] + 1 }).isEmpty(),

                            !blockGet({ position[0] + 1, position[1] - 1, position[2] - 1 }).isEmpty(),
                            !blockGet({ position[0] + 1, position[1] - 1, position[2] + 1 }).isEmpty(),
                            !blockGet({ position[0] + 1, position[1] + 1, position[2] + 1 }).isEmpty(),
                            !blockGet({ position[0] + 1, position[1] + 1, position[2] - 1 }).isEmpty()
                    };

                    const u8Vec4 ao_result = static_cast<unsigned char>(UCHAR_MAX) - u8Vec4{
                            vertexAO(aos[0], aos[3], aos[5]),
                            vertexAO(aos[2], aos[3], aos[6]),
                            vertexAO(aos[0], aos[1], aos[4]),
                            vertexAO(aos[2], aos[1], aos[7])
                    } * SHADDOW_STRENGTH;

#ifdef REL_CHUNK
                    const auto vert_pos = floor_mod(position - MESH_OFFSETS, MESH_SIZES);
                    //const i8Vec3 vert_pos = floor_mod(position - MESH_OFFSETS, MESH_SIZES); <= TODO: should be casted to char vector
#else
                    const auto vert_pos = position;
#endif
                    mesh.push_back({ { vert_pos[0] + 1, vert_pos[1]    , vert_pos[2] + 1 }, block.get(), ao_result });
                    mesh.push_back({ { vert_pos[0] + 1, vert_pos[1]    , vert_pos[2]     }, block.get(), ao_result });
                    mesh.push_back({ { vert_pos[0] + 1, vert_pos[1] + 1, vert_pos[2]     }, block.get(), ao_result });
                    mesh.push_back({ { vert_pos[0] + 1, vert_pos[1] + 1, vert_pos[2] + 1 }, block.get(), ao_result });
                }

                // X - 1
                if (blockGet({ position[0] - 1, position[1], position[2] }).isEmpty())
                {
                    const bool aos[8]{
                            !blockGet({ position[0] - 1, position[1] - 1, position[2]     }).isEmpty(),
                            !blockGet({ position[0] - 1, position[1]    , position[2] - 1 }).isEmpty(),
                            !blockGet({ position[0] - 1, position[1] + 1, position[2]     }).isEmpty(),
                            !blockGet({ position[0] - 1, position[1]    , position[2] + 1 }).isEmpty(),

                            !blockGet({ position[0] - 1, position[1] - 1, position[2] - 1 }).isEmpty(),
                            !blockGet({ position[0] - 1, position[1] - 1, position[2] + 1 }).isEmpty(),
                            !blockGet({ position[0] - 1, position[1] + 1, position[2] + 1 }).isEmpty(),
                            !blockGet({ position[0] - 1, position[1] + 1, position[2] - 1 }).isEmpty()
                    };

                    const u8Vec4 ao_result = static_cast<unsigned char>(UCHAR_MAX) - u8Vec4{
                            vertexAO(aos[0], aos[1], aos[4]),
                            vertexAO(aos[2], aos[1], aos[7]),
                            vertexAO(aos[0], aos[3], aos[5]),
                            vertexAO(aos[2], aos[3], aos[6])
                    } * SHADDOW_STRENGTH;

#ifdef REL_CHUNK
                    const auto vert_pos = floor_mod(position - MESH_OFFSETS, MESH_SIZES);
#else
                    const auto vert_pos = position;
#endif
                    mesh.push_back({ { vert_pos[0]    , vert_pos[1]    , vert_pos[2]     }, block.get(), ao_result });
                    mesh.push_back({ { vert_pos[0]    , vert_pos[1]    , vert_pos[2] + 1 }, block.get(), ao_result });
                    mesh.push_back({ { vert_pos[0]    , vert_pos[1] + 1, vert_pos[2] + 1 }, block.get(), ao_result });
                    mesh.push_back({ { vert_pos[0]    , vert_pos[1] + 1, vert_pos[2]     }, block.get(), ao_result });
                }

                // Y + 1
                if (blockGet({ position[0], position[1] + 1, position[2] }).isEmpty())
                {
                    const bool aos[8]{
                            !blockGet({ position[0] - 1, position[1] + 1, position[2]     }).isEmpty(),
                            !blockGet({ position[0]    , position[1] + 1, position[2] - 1 }).isEmpty(),
                            !blockGet({ position[0] + 1, position[1] + 1, position[2]     }).isEmpty(),
                            !blockGet({ position[0]    , position[1] + 1, position[2] + 1 }).isEmpty(),

                            !blockGet({ position[0] - 1, position[1] + 1, position[2] - 1 }).isEmpty(),
                            !blockGet({ position[0] - 1, position[1] + 1, position[2] + 1 }).isEmpty(),
                            !blockGet({ position[0] + 1, position[1] + 1, position[2] + 1 }).isEmpty(),
                            !blockGet({ position[0] + 1, position[1] + 1, position[2] - 1 }).isEmpty()
                    };

                    const u8Vec4 ao_result = static_cast<unsigned char>(UCHAR_MAX) - u8Vec4{
                            vertexAO(aos[2], aos[1], aos[7]),
                            vertexAO(aos[2], aos[3], aos[6]),
                            vertexAO(aos[0], aos[1], aos[4]),
                            vertexAO(aos[0], aos[3], aos[5])
                    } * SHADDOW_STRENGTH;

#ifdef REL_CHUNK
                    const auto vert_pos = floor_mod(position - MESH_OFFSETS, MESH_SIZES);
#else
                    const auto vert_pos = position;
#endif
                    mesh.push_back({ { vert_pos[0] + 1, vert_pos[1] + 1, vert_pos[2]     }, block.get(), ao_result });
                    mesh.push_back({ { vert_pos[0]    , vert_pos[1] + 1, vert_pos[2]     }, block.get(), ao_result });
                    mesh.push_back({ { vert_pos[0]    , vert_pos[1] + 1, vert_pos[2] + 1 }, block.get(), ao_result });
                    mesh.push_back({ { vert_pos[0] + 1, vert_pos[1] + 1, vert_pos[2] + 1 }, block.get(), ao_result });
                }

                // Y - 1
                if (blockGet({ position[0], position[1] - 1, position[2] }).isEmpty())
                {
                    const bool aos[8]{
                            !blockGet({ position[0] - 1, position[1] - 1, position[2]     }).isEmpty(),
                            !blockGet({ position[0]    , position[1] - 1, position[2] - 1 }).isEmpty(),
                            !blockGet({ position[0] + 1, position[1] - 1, position[2]     }).isEmpty(),
                            !blockGet({ position[0]    , position[1] - 1, position[2] + 1 }).isEmpty(),

                            !blockGet({ position[0] - 1, position[1] - 1, position[2] - 1 }).isEmpty(),
                            !blockGet({ position[0] - 1, position[1] - 1, position[2] + 1 }).isEmpty(),
                            !blockGet({ position[0] + 1, position[1] - 1, position[2] + 1 }).isEmpty(),
                            !blockGet({ position[0] + 1, position[1] - 1, position[2] - 1 }).isEmpty()
                    };

                    const u8Vec4 ao_result = static_cast<unsigned char>(UCHAR_MAX) - u8Vec4{
                            vertexAO(aos[0], aos[1], aos[4]),
                            vertexAO(aos[0], aos[3], aos[5]),
                            vertexAO(aos[2], aos[1], aos[7]),
                            vertexAO(aos[2], aos[3], aos[6])
                    } * SHADDOW_STRENGTH;

#ifdef REL_CHUNK
                    const auto vert_pos = floor_mod(position - MESH_OFFSETS, MESH_SIZES);
#else
                    const auto vert_pos = position;
#endif
                    mesh.push_back({ { vert_pos[0]    , vert_pos[1]    , vert_pos[2]     }, block.get(), ao_result });
                    mesh.push_back({ { vert_pos[0] + 1, vert_pos[1]    , vert_pos[2]     }, block.get(), ao_result });
                    mesh.push_back({ { vert_pos[0] + 1, vert_pos[1]    , vert_pos[2] + 1 }, block.get(), ao_result });
                    mesh.push_back({ { vert_pos[0]    , vert_pos[1]    , vert_pos[2] + 1 }, block.get(), ao_result });
                }

                // Z + 1
                if (blockGet({ position[0], position[1], position[2] + 1 }).isEmpty())
                {
                    const bool aos[8]{
                            !blockGet({ position[0] - 1, position[1]    , position[2] + 1 }).isEmpty(),
                            !blockGet({ position[0]    , position[1] - 1, position[2] + 1 }).isEmpty(),
                            !blockGet({ position[0] + 1, position[1]    , position[2] + 1 }).isEmpty(),
                            !blockGet({ position[0]    , position[1] + 1, position[2] + 1 }).isEmpty(),

                            !blockGet({ position[0] - 1, position[1] - 1, position[2] + 1 }).isEmpty(),
                            !blockGet({ position[0] - 1, position[1] + 1, position[2] + 1 }).isEmpty(),
                            !blockGet({ position[0] + 1, position[1] + 1, position[2] + 1 }).isEmpty(),
                            !blockGet({ position[0] + 1, position[1] - 1, position[2] + 1 }).isEmpty()
                    };

                    const u8Vec4 ao_result = static_cast<unsigned char>(UCHAR_MAX) - u8Vec4{
                            vertexAO(aos[0], aos[1], aos[4]),
                            vertexAO(aos[0], aos[3], aos[5]),
                            vertexAO(aos[2], aos[1], aos[7]),
                            vertexAO(aos[2], aos[3], aos[6])
                    } * SHADDOW_STRENGTH;

#ifdef REL_CHUNK
                    const auto vert_pos = floor_mod(position - MESH_OFFSETS, MESH_SIZES);
#else
                    const auto vert_pos = position;
#endif
                    mesh.push_back({ { vert_pos[0]    , vert_pos[1]    , vert_pos[2] + 1 }, block.get(), ao_result });
                    mesh.push_back({ { vert_pos[0] + 1, vert_pos[1]    , vert_pos[2] + 1 }, block.get(), ao_result });
                    mesh.push_back({ { vert_pos[0] + 1, vert_pos[1] + 1, vert_pos[2] + 1 }, block.get(), ao_result });
                    mesh.push_back({ { vert_pos[0]    , vert_pos[1] + 1, vert_pos[2] + 1 }, block.get(), ao_result });
                }

                // Z - 1
                if (blockGet({ position[0], position[1], position[2] - 1 }).isEmpty())
                {
                    const bool aos[8]{
                            !blockGet({ position[0] - 1, position[1]    , position[2] - 1 }).isEmpty(),
                            !blockGet({ position[0]    , position[1] - 1, position[2] - 1 }).isEmpty(),
                            !blockGet({ position[0] + 1, position[1]    , position[2] - 1 }).isEmpty(),
                            !blockGet({ position[0]    , position[1] + 1, position[2] - 1 }).isEmpty(),

                            !blockGet({ position[0] - 1, position[1] - 1, position[2] - 1 }).isEmpty(),
                            !blockGet({ position[0] - 1, position[1] + 1, position[2] - 1 }).isEmpty(),
                            !blockGet({ position[0] + 1, position[1] + 1, position[2] - 1 }).isEmpty(),
                            !blockGet({ position[0] + 1, position[1] - 1, position[2] - 1 }).isEmpty()
                    };

                    const u8Vec4 ao_result = static_cast<unsigned char>(UCHAR_MAX) - u8Vec4{
                            vertexAO(aos[2], aos[1], aos[7]),
                            vertexAO(aos[2], aos[3], aos[6]),
                            vertexAO(aos[0], aos[1], aos[4]),
                            vertexAO(aos[0], aos[3], aos[5])
                    } * SHADDOW_STRENGTH;

#ifdef REL_CHUNK
                    const auto vert_pos = floor_mod(position - MESH_OFFSETS, MESH_SIZES);
#else
                    const auto vert_pos = position;
#endif
                    mesh.push_back({ { vert_pos[0] + 1, vert_pos[1]    , vert_pos[2]     }, block.get(), ao_result });
                    mesh.push_back({ { vert_pos[0]    , vert_pos[1]    , vert_pos[2]     }, block.get(), ao_result });
                    mesh.push_back({ { vert_pos[0]    , vert_pos[1] + 1, vert_pos[2]     }, block.get(), ao_result });
                    mesh.push_back({ { vert_pos[0] + 1, vert_pos[1] + 1, vert_pos[2]     }, block.get(), ao_result });
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
void World::generateChunkNew(Block *destination, const i32Vec3 from_block, const i32Vec3 to_block, const WorldType world_type)
{
    switch(world_type)
    {
        case WorldType::SINE: sineChunkNew(destination, from_block, to_block); break;
        case WorldType::SIMPLEX_2D: simplex2DChunkNew(destination, from_block, to_block); break;
        case WorldType::EMPTY: emptyChunkNew(destination, from_block, to_block); break;
        default: throw "Not implemented."; break;
    }
}

//==============================================================================
void World::emptyChunkNew(Block * destination, const i32Vec3 from_block, const i32Vec3 to_block)
{
    i32Vec3 position;
    int i = 0;

    for (position[2] = from_block[2]; position[2] < to_block[2]; ++position[2])
        for (position[1] = from_block[1]; position[1] < to_block[1]; ++position[1])
            for (position[0] = from_block[0]; position[0] < to_block[0]; ++position[0])
                destination[i++] = Block{ 0 };
}

//==============================================================================
void World::simplex2DChunkNew(Block * destination, const i32Vec3 from_block, const i32Vec3 to_block)
{
    i32Vec3 position;
    int i = 0;

    for (position[2] = from_block[2]; position[2] < to_block[2]; ++position[2])
        for (position[1] = from_block[1]; position[1] < to_block[1]; ++position[1])
            for (position[0] = from_block[0]; position[0] < to_block[0]; ++position[0])
            {
                auto & block = destination[i++];
                const glm::vec2 f_position{ position[0], position[2] };
                const float y_position = position[1];
                const auto res = glm::simplex(f_position * 0.03f);
                if (res * 5.0f > y_position)
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
void World::sineChunkNew(Block * destination, const i32Vec3 from_block, const i32Vec3 to_block)
{
    i32Vec3 position;
    int i = 0;

    for (position[2] = from_block[2]; position[2] < to_block[2]; ++position[2])
        for (position[1] = from_block[1]; position[1] < to_block[1]; ++position[1])
            for (position[0] = from_block[0]; position[0] < to_block[0]; ++position[0])
            {
                auto & block = destination[i++];

                if (std::sin(position[0] * 0.1f) * std::sin(position[2] * 0.1f) * 10.0f > static_cast<float>(position[1]))
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
bool World::removeOutOfRangeMeshes(const i32Vec3 center_mesh)
{
    // remove out of range meshes
    auto count = m_loaded_meshes.size();

    bool completed = true;

    for (std::size_t i = 0; i < count;)
    {
        const auto mesh_position = m_loaded_meshes[i].position;

        const auto mesh_index = position_to_index(mesh_position, MESH_CONTAINER_SIZES);

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
// TODO: refactor, redo synchronization
void World::multiThreadMeshLoader(const int thread_id)
{
    std::unique_ptr<Block[]> container{ std::make_unique<Block[]>(CHUNK_SIZE) }; // TODO: combine 'chunks' and 'container'
    static_assert(CSIZE == 16 && MSIZE == 16 && MOFF == 8, "Temporary.");
    constexpr size_t SZEE = CHUNK_SIZE * product_constexpr(chunk_container_size);
    std::unique_ptr<i32Vec3[]> chunk_positions{ std::make_unique<i32Vec3[]>(SZEE) }; // TODO: correct algorithm for determining needed size
    std::unique_ptr<Block[]> chunks{ std::make_unique<Block[]>(SZEE) }; // TODO: correct algorithm for determining needed size

    // initialize this stuff
    for (std::size_t i = 0; i < SZEE; ++i)
        chunk_positions[i] = { 0, 0, 0 };
    chunk_positions[0] = { 1, 0, 0 };

    i32Vec3 center_chunk = m_loader_center.load();
    i32Vec3 center_mesh = center_chunk;

    while (!m_quit)
    {
      //Debug::print("Thread ", thread_id, " new loop.");
        auto current_index = m_iterator_index.fetch_add(1);

//      if (current_index > 15500)
//      Debug::print("Thread ", thread_id, " with current_index: ", current_index);
        if (m_moved_center_mesh == true)
        {
            // gather all threads
            /*auto r = m_waiting_threads.fetch_add(1);
            Debug::print("T: ", thread_id, " r: ", r);
            while (true)
            {
                m_barrier.wait();
                if (m_waiting_threads == THREAD_COUNT) break;
            }
*/
//          if (current_index > 15500)
//            Debug::print("Thread ", thread_id, " waiting with current_index: ", current_index);
            m_ugly_hacky_thingy.wait(m_barrier); // TODO: fix deadlock: threads can wait on this or on exit barrier and get stuck

            if (thread_id == 0)
            {
//                Debug::print("Reset iterator -----------------------");

                // You are the 0 thread. I've got bad news for you. You'll have to do all the serial work.
                m_iterator_index = 0; // reset iterator
//                m_waiting_threads = 0;
                // the following two lines should be executed atomically, but without it, the program is still correct
                m_moved_center_mesh = false;
                m_loader_center = m_center_mesh.load();

                // TODO: figure out if this is serializing too much. (probably debends on renderer command execution speed and buffer size)
                // remove all out of range meshes
                while (!removeOutOfRangeMeshes(center_mesh))
                    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // buffer stall
            }

            m_barrier.wait();

            // get the new center
            center_chunk = m_loader_center;
            center_mesh = center_chunk;

            continue;
        }

      auto task = m_iterator.m_points[current_index]; // this has been moved to after "if (m_moved_center_mesh == true)" because otherwise, there is a data race. TODO: need to rework the whole thing

//        Debug::print("Thread: ", thread_id, " | Task ID: ", current_index, " | Task: ", to_string(task.position), " - ", static_cast<int>(task.task));

      if (current_index >= m_iterator.m_points.size())
      {
        Debug::print("Thread ", thread_id, " woops. Wrong iterator index.");
        Debug::print("current_index: ", current_index, " m_iterator.m_points.size(): ", m_iterator.m_points.size());
        //break;
        throw 1;
      }

        assert(current_index < m_iterator.m_points.size() && "Out of bounds access.");

        switch (task.task)
        {
            case SphereIterator<RDISTANCE, THREAD_COUNT>::Task::SYNC:
            {
                m_barrier.wait();
            }
            break;
            case SphereIterator<RDISTANCE, THREAD_COUNT>::Task::LAST_SYNC_AND_LOAD_REGION:
            {
                // TODO: figure out how to paralelize region loading
                const auto from_chunk = ((task.position * -1) + 1) + center_chunk;
                const auto to_chunk = task.position + center_chunk;

                const auto from_region = floor_div(from_chunk, CHUNK_REGION_SIZES);
                const auto to_region = floor_div(to_chunk, CHUNK_REGION_SIZES);

                i32Vec3 position;

                for (position[2] = from_region[2]; position[2] <= to_region[2]; ++position[2])
                    for (position[1] = from_region[1]; position[1] <= to_region[1]; ++position[1])
                        for (position[0] = from_region[0]; position[0] <= to_region[0]; ++position[0])
                            loadRegionNew(position);

                m_barrier.wait();
            }
            break;
            case SphereIterator<RDISTANCE, THREAD_COUNT>::Task::GENERATE_CHUNK:
            {
                const i32Vec3 chunk_position{ task.position + center_chunk }; // warning when copy pasting center_chunk and center_mesh
                const i32Vec3 from{ chunk_position * CHUNK_SIZES };
                const i32Vec3 to{ from + CHUNK_SIZES };

                const auto region_position = floor_div(chunk_position, CHUNK_REGION_SIZES);
                const auto & chunk_region = m_regions[region_position];
                const auto & chunk_meta = chunk_region.metas[chunk_position];

                if (chunk_meta.loc == CType::NOWHERE) // TODO: use the other container when chunk is inside it to avoid 1 memory copy
                {
                    generateChunkNew(container.get(), from, to, WorldType::SIMPLEX_2D);
                    saveChunkToRegionNew(container.get(), chunk_position);
                }

                if (all(abs(chunk_position - center_chunk) <= BLOCKS_RADIUSES))
                { // load to storage
                    const auto index = position_to_index(chunk_position, CHUNK_CONTAINER_SIZES) * CHUNK_SIZE;
                    auto * this_chunk = m_blocks + index;
                    auto & this_chunk_meta = m_chunk_statuses[chunk_position];

                    Debug::print("Load chunk to container: ", to_string(chunk_position));
                    loadChunkToChunkContainerNew(chunk_position, this_chunk, &(this_chunk_meta.position));
                }
            }
            break;
            case SphereIterator<RDISTANCE, THREAD_COUNT>::Task::GENERATE_MESH:
            {
                // TODO: at least remember if mesh is empty, so that chunks won't need to de loaded if empty
                const auto current_mesh_position = task.position + center_mesh;

                // TODO: assert in range

                // no need for locking ?
                if (m_mesh_loaded[current_mesh_position] != Status::UNLOADED)
                    continue;

                // assert chunk ~ mesh
                const auto region_position = floor_div(current_mesh_position, CHUNK_REGION_SIZES);
                auto & chunk_region = m_regions[region_position];
                auto & mesh_status = chunk_region.mesh_statuses[current_mesh_position];

                std::vector<Vertex> mesh;
                if (mesh_status != Region::MStatus::EMPTY)
                {
                    mesh = generateMeshNew(current_mesh_position,
                        //chunk_container_size,
                       chunks.get(), chunk_positions.get());
                }

                if (mesh.size() != 0)
                {
                    std::unique_lock<std::mutex> lock { m_ring_buffer_lock };

                    auto * command = m_commands.initPush();

                    // buffer is full
                    while (command == nullptr) // TODO: fix that: exit + buffer_stall = deadlock because worker threads are waiting for queue to have space and render thread will not empty the queue
                    {
                        std::cout << "Buffer stall. Sleeping" << std::endl;
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        command = m_commands.initPush();
                    }

                    command->type = Command::Type::UPLOAD;
                    command->index = position_to_index(current_mesh_position, MESH_CONTAINER_SIZES);
                    command->position = current_mesh_position;
                    command->mesh = mesh;

                    m_commands.commitPush();

                    if (mesh_status == Region::MStatus::UNKNOWN)
                        mesh_status = Region::MStatus::NON_EMPTY;
                }
                else
                {
                    if (mesh_status == Region::MStatus::UNKNOWN)
                        mesh_status = Region::MStatus::EMPTY;
                }

                // update mesh state
                // no need for locking ?
                m_mesh_loaded[current_mesh_position] = Status::LOADED;
                std::unique_lock<std::mutex> lock{ m_loaded_meshes_lock };
                m_loaded_meshes.push_back({current_mesh_position, mesh.size() == 0});
            }
            break;
            case SphereIterator<RDISTANCE, THREAD_COUNT>::Task::END_MARKER:
            {
                assert (current_index == m_iterator.m_points.size() - 1 && "End marker should only appear at the end of the hardcoded iterator.");

                m_iterator_index = 0;

                std::this_thread::sleep_for(std::chrono::milliseconds(200)); // TODO: replace with condition variable, that signals when the need to start workers exists

                m_barrier.wait();
            }
            break;
            default:
            {
                assert(false && "Invalid command.");
            }
            break;
        }
    }

  //Debug::print("Thread ", thread_id, " waiting for exit.");

    // mechanism for exiting worker threads
    m_exited_threads.fetch_add(1); // this works but is not reusable
    while (true)
    {
        m_barrier.wait(); // TODO: fix deadlock: threads can wait on this or on center sync barrier and get stuck

        if (m_exited_threads == THREAD_COUNT)
            break;
    }

  //Debug::print("Thread ", thread_id, " exiting.");
}

//==============================================================================
bool World::inRange(const i32Vec3 center, const i32Vec3 position, const int max_square_distance)
{
    const i32Vec3 distances = position - center;

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
                const auto & mesh_data = m_meshes.get_entry(command->index).mesh;
#if 1
                assert(mesh_data.VBO && mesh_data.VAO && "Should not be 0.");
                m_unused_buffers.push({ mesh_data.VAO, mesh_data.VBO });
#else
                glDeleteBuffers(1, &mesh_data.VBO);
                glDeleteVertexArrays(1, &mesh_data.VAO);
#endif
                m_meshes.delete_entry(command->index);
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
#ifdef REL_CHUNK
                    glVertexAttribIPointer(0, 3, GL_BYTE, sizeof(Vertex), (GLvoid *) (0));
                    glVertexAttribIPointer(1, 1, GL_BYTE, sizeof(Vertex), (GLvoid *) (sizeof(Vertex::position)));
#else
                    glVertexAttribIPointer(0, 3, GL_INT, sizeof(Vertex), (GLvoid *) (0));
                    glVertexAttribIPointer(1, 1, GL_INT, sizeof(Vertex), (GLvoid *) (sizeof(Vertex::position)));
#endif
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

                m_meshes.add_entry(command->index, { { VAO, VBO, EBO_size }, command->position });

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
void World::draw(const i32Vec3 new_center, const f32Vec4 frustum_planes[6], const GLint offset_uniform)
{
    const auto center_mesh = floor_div(new_center - MESH_OFFSETS, MESH_SIZES);
    const auto old_center_mesh = m_center_mesh.exchange(center_mesh);

    if (!all(old_center_mesh == center_mesh))
    {
        //std::cout << "Old: " << to_string(old_center_mesh) << std::endl;
        //std::cout << "New: " << to_string(center_mesh) << std::endl;
        m_moved_center_mesh = true;
    }

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
        if (!meshInFrustum(frustum_planes, m.position * MESH_SIZES + MESH_OFFSETS))
            continue;

        const auto & mesh_data = m.mesh;

        assert(mesh_data.size <= QuadEBO::size() && mesh_data.size > 0);
        assert(mesh_data.VAO != 0 && mesh_data.VBO != 0 && "VAO and/or VBO not loaded.");
#ifdef REL_CHUNK
        const auto & pos = m.position * MESH_SIZES + MESH_OFFSETS;
        glUniform3f(offset_uniform, static_cast<float>(pos[0]), static_cast<float>(pos[1]), static_cast<float>(pos[2]));
#endif
        glBindVertexArray(mesh_data.VAO);
        glDrawElements(GL_TRIANGLES, mesh_data.size, QuadEBO::type(), 0);
        glBindVertexArray(0);
    }
}

//==============================================================================
void World::exitLoaderThread()
{

    //assert(m_loader_thread.joinable() && "Loader thread is not joinable.");

    //m_quit = true;

    //if (!m_loader_thread.joinable())
        //return;

    //m_loader_thread.join();


    m_quit = true;

    for (std::size_t i = 0; i < THREAD_COUNT; ++i)
    {
        assert(m_workers[i].joinable() && "Why is this not joinable?");
        m_workers[i].join();
    }
}

//==============================================================================
bool World::meshInFrustum(const f32Vec4 planes[6], const i32Vec3 mesh_offset)
{
    const f32Vec3 from{ static_cast<float>(mesh_offset[0]), static_cast<float>(mesh_offset[1]), static_cast<float>(mesh_offset[2]) };
    const f32Vec3 to{ from + f32Vec3{ static_cast<float>(MESH_SIZES[0]), static_cast<float>(MESH_SIZES[1]), static_cast<float>(MESH_SIZES[2]) } };

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

        if (dot(planes[i], f32Vec4{ from[0], from[1], from[2], 1 }) < 0.0f) ++out;
        else ++in;
        if (dot(planes[i], f32Vec4{ from[0], from[1], to[2], 1 }) < 0.0f) ++out;
        else ++in;
        if (dot(planes[i], f32Vec4{ from[0], to[1], from[2], 1 }) < 0.0f) ++out;
        else ++in;
        if (dot(planes[i], f32Vec4{ from[0], to[1], to[2], 1 }) < 0.0f) ++out;
        else ++in;
        if (dot(planes[i], f32Vec4{ to[0], from[1], from[2], 1 }) < 0.0f) ++out;
        else ++in;
        if (dot(planes[i], f32Vec4{ to[0], from[1], to[2], 1 }) < 0.0f) ++out;
        else ++in;
        if (dot(planes[i], f32Vec4{ to[0], to[1], from[2], 1 }) < 0.0f) ++out;
        else ++in;
        if (dot(planes[i], f32Vec4{ to[0], to[1], to[2], 1 }) < 0.0f) ++out;
        else ++in;

        // If all corners are out
        if (in == 0) return false;
#endif
    }

    return true;
}

//==============================================================================
void World::saveChunkToRegionNew(const Block * const source, const i32Vec3 chunk_position)
{
    const auto region_position = floor_div(chunk_position, CHUNK_REGION_SIZES);
    auto & region = m_regions[region_position];

//    Debug::print("Save chunk: ", to_string(chunk_position));
    std::unique_lock<std::mutex> lock{ region.write_lock }; // TODO: figure something out. this lock is serializing too much. compress2() is probably taking a lot of time


    // check if region was changed during locking. Assuming,  that it will not be changed, while this function is executing (and also shouldn't if everything is implemented correctly)
    assert(all(region.position == region_position) && "Incorrect region loaded.");

    uLong destination_length = compressBound(static_cast<uLong>(CHUNK_DATA_SIZE));

#ifndef NEW_REGION_FORMAT
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
#else
    Bytef * const destination = reinterpret_cast<Bytef *>(region.data_memory.getBlock(destination_length));
#endif

    const auto result = compress2(destination, &destination_length, reinterpret_cast<const Bytef *>(source), static_cast<uLong>(CHUNK_DATA_SIZE), Z_BEST_SPEED);

    assert(result == Z_OK && "Error compressing chunk.");
    assert(destination_length <= compressBound(static_cast<uLong>(CHUNK_DATA_SIZE)) && "ZLib lied about the maximum possible size of compressed data.");

    auto & chunk_meta = region.metas[chunk_position];

#ifdef NEW_REGION_FORMAT
    region.data_memory.increaseUsedSpace(destination_length);

    chunk_meta.size = static_cast<int>(destination_length);
    chunk_meta.location = destination;
    assert(chunk_meta.loc == CType::NOWHERE && "Chunk must not already exist when saving it.");
    chunk_meta.loc = CType::MEMORY;

    // region.size += destination_length; <- should be computed at saving time?
#else
    chunk_meta.size = static_cast<int>(destination_length);
    chunk_meta.offset = region.size;

    region.size += static_cast<int>(destination_length);
#endif

    region.needs_save = true;
}

//==============================================================================
void World::saveRegionToDriveNew(const i32Vec3 region_position)
{
    //const
    auto & region = m_regions[region_position];

    if (!region.needs_save)
        return;

    assert(all(region.position == region_position) && "Chunk was probably never initialized. Control flow should have never reached this.");

    const auto from_chunk = region_position * CHUNK_REGION_SIZES; // opposite of floor_div( ... , ... )
    const auto to_chunk = from_chunk + CHUNK_REGION_SIZES;

    i32Vec3 position;
    std::size_t total_size_of_chunks_in_memory{ 0 };
    std::vector<ChunkMeta *> to_save; // TODO: flat preallocated array instead of vector

    // find out what chunks need to be saved
    for (position[2] = from_chunk[2]; position[2] <= to_chunk[2]; ++position[2])
        for (position[1] = from_chunk[1]; position[1] <= to_chunk[1]; ++position[1])
            for (position[0] = from_chunk[0]; position[0] <= to_chunk[0]; ++position[0])
            {
                auto & chunk_meta = region.metas[position];
                if (chunk_meta.loc == CType::MEMORY)
                {
                    assert(chunk_meta.size != 0 && chunk_meta.location != nullptr && chunk_meta.offset_n == 0 && "Data structure is broken.");
                    total_size_of_chunks_in_memory += chunk_meta.size;
                    //const auto * test = &chunk_meta;
                    to_save.push_back(&(region.metas[position])); // pointer to referenced object ?
                    //to_save.push_back(&chunk_meta);
                    chunk_meta.loc = CType::FILE;
                }
            }

    // fix file size
    if (region.size + static_cast<int>(total_size_of_chunks_in_memory) > region.container_size)
    {
        //Debug::print("Reallocating region container.");
        const auto new_container_size = region.size + static_cast<int>(total_size_of_chunks_in_memory);
        auto * const new_data = (Bytef*)std::realloc(region.data, static_cast<std::size_t>(new_container_size));
        if (new_data == nullptr) throw 0;
        region.container_size = new_container_size;
        region.data = new_data;
    }

    // move to file
    for (auto & chunk_meta : to_save)
    {
        Bytef * destination = region.data + region.size;
        chunk_meta->offset_n = region.size;
        region.size += chunk_meta->size;
        std::memcpy(destination, chunk_meta->location, chunk_meta->size);
        chunk_meta->location = nullptr; // don't worry the memory will be recycled automatically
        chunk_meta->loc = CType::FILE;
    }

    assert(region.size <= region.container_size && "Oops, buffer overflow.");

    // save only if valid
    assert(all(region_position == region.position) && "Trying to save invalid region.");

    // save region
    Debug::print("Saving region ", to_string(region.position));
    assert((region.size == 0 && region.data == nullptr || region.size > 0 && region.data != nullptr) && "Data structure is broken.");

    const std::string file_name = WORLD_ROOT + to_string(region.position);
    std::ofstream file{ file_name, std::ofstream::out | std::ofstream::binary | std::ofstream::trunc };

    file.write(reinterpret_cast<const char *>(&region.size), sizeof(region.size));
    file.write(reinterpret_cast<const char *>(region.metas.begin()), sizeof(region.metas));
    file.write(reinterpret_cast<const char *>(region.mesh_statuses.begin()), sizeof(region.mesh_statuses));
    if (region.size > 0)
        file.write(reinterpret_cast<const char *>(region.data), region.size);

    if (!file.good()) std::runtime_error("Writing file failed.");
}

//==============================================================================
void World::tick(const std::size_t tick)
{
    //Debug::print("Tick ", tick);
}

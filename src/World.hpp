#pragma once

#define RESET_QUEUE

#include "RingBufferSingleProducerSingleConsumer.hpp"
#include "SparseMap.hpp"
#include "TinyAlgebra.hpp"
#include "Block.hpp"
#include "SphereIterator.hpp"
#include <string>
#include <vector>
#include <GL/gl3w.h>
#include <zlib.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <stack>
#include <queue>
#include "ModTable.hpp"

// TODO: abstract and reuse repetitive data structures like 3D mod table, or m_mesh_cache_infos and m_regions

// TODO: expand
// TODO: char instead of int position and type
struct Vertex { iVec3 position; int type; ucVec4 shaddow; };

struct Mesh { GLuint VAO; GLuint VBO; GLsizei size; };
struct UnusedBuffer { GLuint VAO; GLuint VBO; };
struct MeshMeta { iVec3 position; bool empty; };

struct ChunkMeta { int size; int offset; };

enum class Status : char { UNLOADED, LOADED, CHECKED };

struct Command
{
    enum class Type : int { REMOVE, UPLOAD };
    Type type;
    int index;
    iVec3 position;
    std::vector<Vertex> mesh;
};

struct MeshWPos { Mesh mesh; iVec3 position; };

//==============================================================================
class World
{
public:
    World();
    ~World();
    void draw(const iVec3 new_center, const fVec4 frustum_planes[6]);

private:
    //==============================================================================
    // constants

    // only edit following line / no need to tinker with the rest
    static constexpr int
            RDISTANCE{ 130 },
            REDISTANCE{ RDISTANCE * 2 },
            CSIZE{ 16 },
            MSIZE{ 16 },
            //MCSIZE{ 48 },
            MCSIZE{ (REDISTANCE * 2) / MSIZE + 8 },
            MESH_BORDER_REQUIRED_SIZE{ 1 },
            MOFF{ MESH_BORDER_REQUIRED_SIZE + 3 }, // or maybe do chunk_size / 2
            //MOFF{ 0 },
            RSIZE{ 512 / CSIZE },
            MRSIZE{ RSIZE },
            CCSIZE{ 16 },
            RCSIZE{ (MSIZE * MCSIZE + MOFF) / (CSIZE * RSIZE) + 3 }, // round up + 2 instead of + 3 would be prettier
            MRCSIZE{ RCSIZE };

    static_assert(CSIZE > 0 && MSIZE > 0 && MCSIZE > 0 && MESH_BORDER_REQUIRED_SIZE >= 0 && RSIZE > 0 && RCSIZE > 0, "Parameters must be positive.");
    static_assert((RDISTANCE * 2) / MSIZE < MCSIZE, "Mesh container too small for the render distance.");
    static_assert((REDISTANCE * 2) / MSIZE < MCSIZE, "Mesh container too small for the loaded distance.");
    static_assert(((MESH_BORDER_REQUIRED_SIZE * 2 + MSIZE) + (CSIZE - 1)) / CSIZE <= CCSIZE, "Chunk container size too small.");

    static constexpr int CHUNK_SIZE_X{ CSIZE }, CHUNK_SIZE_Y{ CSIZE }, CHUNK_SIZE_Z{ CSIZE };
    static constexpr int CHUNK_CONTAINER_SIZE_X{ CCSIZE }, CHUNK_CONTAINER_SIZE_Y{ CCSIZE }, CHUNK_CONTAINER_SIZE_Z{ CCSIZE };
    static constexpr int MESH_SIZE_X{ MSIZE }, MESH_SIZE_Y{ MSIZE }, MESH_SIZE_Z{ MSIZE };
    static constexpr int MESH_CONTAINER_SIZE_X{ MCSIZE }, MESH_CONTAINER_SIZE_Y{ MCSIZE }, MESH_CONTAINER_SIZE_Z{ MCSIZE };
    static constexpr int MESH_OFFSET_X{ MOFF }, MESH_OFFSET_Y{ MOFF }, MESH_OFFSET_Z{ MOFF };
    static constexpr int REGION_SIZE_X{ RSIZE }, REGION_SIZE_Y{ RSIZE }, REGION_SIZE_Z{ RSIZE };
    static constexpr int MESH_REGION_SIZE_X{ MRSIZE }, MESH_REGION_SIZE_Y{ MRSIZE }, MESH_REGION_SIZE_Z{ MRSIZE };
    static constexpr int REGION_CONTAINER_SIZE_X{ RCSIZE }, REGION_CONTAINER_SIZE_Y{ RCSIZE }, REGION_CONTAINER_SIZE_Z{ RCSIZE };
    static constexpr int MESH_REGION_CONTAINER_SIZE_X{ MRCSIZE }, MESH_REGION_CONTAINER_SIZE_Y{ MRCSIZE }, MESH_REGION_CONTAINER_SIZE_Z{ MRCSIZE };
    static constexpr int RENDER_DISTANCE_X{ RDISTANCE }, RENDER_DISTANCE_Y{ RDISTANCE }, RENDER_DISTANCE_Z{ RDISTANCE };
    static constexpr int REMOVE_DISTANCE_X{ REDISTANCE }, REMOVE_DISTANCE_Y{ REDISTANCE }, REMOVE_DISTANCE_Z{ REDISTANCE };

    static constexpr int CHUNK_SIZE{ CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z };
    static constexpr int CHUNK_CONTAINER_SIZE{ CHUNK_CONTAINER_SIZE_X * CHUNK_CONTAINER_SIZE_Y * CHUNK_CONTAINER_SIZE_Z };
    static constexpr int REGION_CONTAINER_SIZE{ REGION_CONTAINER_SIZE_X * REGION_CONTAINER_SIZE_Y * REGION_CONTAINER_SIZE_Z };
    static constexpr int MESH_REGION_CONTAINER_SIZE{ MESH_REGION_CONTAINER_SIZE_X * MESH_REGION_CONTAINER_SIZE_Y * MESH_REGION_CONTAINER_SIZE_Z };
    static constexpr int MESH_CONTAINER_SIZE{ MESH_CONTAINER_SIZE_X * MESH_CONTAINER_SIZE_Y * MESH_CONTAINER_SIZE_Z };
    static constexpr int REGION_SIZE{ REGION_SIZE_X * REGION_SIZE_Y * REGION_SIZE_Z };
    static constexpr int MESH_REGION_SIZE{ MESH_REGION_SIZE_X * MESH_REGION_SIZE_Y * MESH_REGION_SIZE_Z };

    static constexpr unsigned char SHADDOW_STRENGTH{ 60 };

    static constexpr int SQUARE_RENDER_DISTANCE{ RENDER_DISTANCE_X * RENDER_DISTANCE_X + RENDER_DISTANCE_Y * RENDER_DISTANCE_Y + RENDER_DISTANCE_Z * RENDER_DISTANCE_Z };
    static constexpr int SQUARE_REMOVE_DISTANCE{ REMOVE_DISTANCE_X * REMOVE_DISTANCE_X + REMOVE_DISTANCE_Y * REMOVE_DISTANCE_Y + REMOVE_DISTANCE_Z * REMOVE_DISTANCE_Z };

    static_assert(sizeof(Bytef) == sizeof(char), "Assuming that.");
    static constexpr int SOURCE_LENGTH{ sizeof(Block) * CHUNK_SIZE };
    static constexpr int REGION_DATA_SIZE_FACTOR{ SOURCE_LENGTH * 128 };

    static constexpr char WORLD_ROOT[]{ "world/" };
    static constexpr char MESH_CACHE_ROOT[]{ "mesh_cache/" };

    static constexpr int META_DATA_SIZE{ REGION_SIZE * sizeof(ChunkMeta) };

    static constexpr int SQUARE_LOAD_RESET_DISTANCE{ MSIZE * 2 };

    static constexpr iVec3 CHUNK_SIZES{ CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z };
    static constexpr iVec3 REGION_SIZES{ REGION_SIZE_X, REGION_SIZE_Y, REGION_SIZE_Z };
    static constexpr iVec3 CHUNK_CONTAINER_SIZES{ CHUNK_CONTAINER_SIZE_X, CHUNK_CONTAINER_SIZE_Y, CHUNK_CONTAINER_SIZE_Z };
    static constexpr iVec3 REGION_CONTAINER_SIZES{ REGION_CONTAINER_SIZE_X, REGION_CONTAINER_SIZE_Y, REGION_CONTAINER_SIZE_Z };
    static constexpr iVec3 MESH_REGION_CONTAINER_SIZES{ MESH_REGION_CONTAINER_SIZE_X, MESH_REGION_CONTAINER_SIZE_Y, MESH_REGION_CONTAINER_SIZE_Z };
    static constexpr iVec3 MESH_REGION_SIZES{ MESH_REGION_SIZE_X, MESH_REGION_SIZE_Y, MESH_REGION_SIZE_Z };

    static constexpr iVec3 MESH_CONTAINER_SIZES{ MESH_CONTAINER_SIZE_X, MESH_CONTAINER_SIZE_Y, MESH_CONTAINER_SIZE_Z };
    static constexpr iVec3 MESH_SIZES{ MESH_SIZE_X, MESH_SIZE_Y, MESH_SIZE_Z };
    static constexpr iVec3 MESH_OFFSETS{ MESH_OFFSET_X, MESH_OFFSET_Y, MESH_OFFSET_Z };

    static constexpr int COMMAND_BUFFER_SIZE{ 128 };
    static constexpr int SLEEP_MS{ 100 };
    static constexpr int MAX_COMMANDS_PER_FRAME{ 4 };
    static constexpr int MESHES_TO_LOAD_PER_LOOP{ 32 };

    static_assert(MAX_COMMANDS_PER_FRAME > 0, "Can't do anything without command execution.");

    // static constexpr int MAX_MESH_DATA_SIZE{ CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z * 4 * 6 * sizeof(Vertex) };
    static constexpr int MESH_CACHE_DATA_SIZE_FACTOR{ 4096 * 64 };

    //==============================================================================
    // variables

    // loader thread data
    std::thread m_loader_thread;
    Block m_blocks[CHUNK_SIZE * CHUNK_CONTAINER_SIZE];
    iVec3 m_chunk_positions[CHUNK_CONTAINER_SIZE];
    bool m_needs_save[CHUNK_CONTAINER_SIZE];
    iVec3 m_mesh_positions[MESH_CONTAINER_SIZE];
    // TODO: more space efficient format than current (3 states only needed)
    Status m_mesh_loaded[MESH_CONTAINER_SIZE];

    static constexpr int MAX_RENDER_DISTANCE{ std::max(std::max(RENDER_DISTANCE_X, RENDER_DISTANCE_Y), RENDER_DISTANCE_Z) };
    static constexpr int MAX_CHUNK_SIZE{ std::max(std::max(CHUNK_SIZE_X, CHUNK_SIZE_Y), CHUNK_SIZE_Z) };
    SphereIterator<(MAX_RENDER_DISTANCE + MAX_CHUNK_SIZE - 1) / MAX_CHUNK_SIZE> m_iterator;

    // TODO: Maybe replace by array and size counter. Max possible size should be equal to MESH_CONTAINER_SIZE_X * MESH_CONTAINER_SIZE_Y * MESH_CONTAINER_SIZE_Z, but is overkill.
    std::vector<MeshMeta> m_loaded_meshes; // contains all loaded meshes
    struct Region
    {
        iVec3 position;
        ChunkMeta metas[REGION_SIZE];
        Bytef * data; // TODO: replace pointer with RAII mechanism
        int size, container_size; // TODO: rename container_size to capacity
        bool needs_save;
    } m_regions[REGION_CONTAINER_SIZE];

    struct MeshCache
    {
        iVec3 position;
        enum class Status : char { UNKNOWN, EMPTY, NON_EMPTY }; // could be reduced to bitmap (2 bits per mesh)
        int size, container_size; // TODO: rename container_size to capacity
        bool needs_save;
        // TODO: maybe interlive the following 3 / 4 aka.: struct{ int decompressed_size, compressed_size, offset; }
        ModTable<Status, int, MESH_REGION_SIZE_X, MESH_REGION_SIZE_Y, MESH_REGION_SIZE_Z> statuses;
        ModTable<int, int, MESH_REGION_SIZE_X, MESH_REGION_SIZE_Y, MESH_REGION_SIZE_Z> decompressed_size;
        ModTable<int, int, MESH_REGION_SIZE_X, MESH_REGION_SIZE_Y, MESH_REGION_SIZE_Z> compressed_size;
        ModTable<int, int, MESH_REGION_SIZE_X, MESH_REGION_SIZE_Y, MESH_REGION_SIZE_Z> offset;
        Bytef * data; // TODO: replace pointer with RAII mechanism
    } m_mesh_cache_infos[MESH_REGION_CONTAINER_SIZE];

    // renderer thread data
    std::stack<UnusedBuffer> m_unused_buffers;
    iVec3 m_reference_center;
    SparseMap<MeshWPos, MESH_CONTAINER_SIZE> m_meshes;

    // shared / synchronization data
    RingBufferSingleProducerSingleConsumer<Command, COMMAND_BUFFER_SIZE> m_commands;
    std::atomic<iVec3> m_center;
    std::atomic_bool m_quit;
    std::atomic_bool m_moved_far;

    //==============================================================================
    // functions

    Block & getBlock(const iVec3 block_position);
    int loadChunkRange(const iVec3 from_block, const iVec3 to_block);
    int loadChunk(const iVec3 chunk_position);
    std::vector<Vertex> generateMesh(const iVec3 from_block, const iVec3 to_block);
    static unsigned char vertAO(const bool side_a, const bool side_b, const bool corner);
    void generateChunk(const iVec3 from_block);
    void sineChunk(const iVec3 from_block);
    void debugChunk(const iVec3 from_block);
    //void meshLoaderOld();
    void meshLoader();
    bool inRange(const iVec3 center_block, const iVec3 position_block, const int square_max_distance);
    void exitLoaderThread();
    static bool meshInFrustum(const fVec4 planes[6], const iVec3 mesh_offset);
    void loadRegion(const iVec3 region_position);
    void loadMeshCache(const iVec3 mesh_cache_position);
    void saveChunkToRegion(const int chunk_index);
    void saveRegionToDrive(const int region_index);
    void saveMeshCacheToDrive(const int mesh_cache_index);
    MeshCache::Status meshStatus(const iVec3 mesh_position);
    //void setMeshStatus(const iVec3 mesh_position, const MeshCache::Status new_status);
    void saveMeshToMeshCache(const iVec3 mesh_position, const std::vector<Vertex> & mesh);
    std::vector<Vertex> loadMesh(const iVec3 mesh_position);

};

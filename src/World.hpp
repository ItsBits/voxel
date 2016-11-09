#pragma once

/*
 * TODO: player position should be chunk relative for float accuracy instead of absolute
 */

/*
 * 2 Threads:
 *     render
 *     loader
 *
 * loader:
 *    reset free task list (m_tasks[c])
 *
 *    iterate over internal list (m_loaded_meshes)
 *        if out of range add to remove (m_tasks[c].remove.push_back(mesh))
 *        else add to render (m_tasks[c].render.push_back(mesh))
 *    iterate over all meshes (m_mesh_positions)
 *        if not loaded
 *            load mesh
 *                add to internal list (m_loaded_meshes)
 *                add to upload list (m_tasks[c].upload.push_back(mesh))
 *    hand over task list to renderer
 *
 * render:
 *    iterate over upload list (m_tasks[(c + 1) % 2].upload)
 *        upload mesh to GPU
 *        ADD TO RENDER LIST
 *    iterate over remove list (m_tasks[(c + 1) % 2].remove)
 *        free resources on GPU (VAO, VBO) and recycle them
 *    iterate over render list (m_tasks[(c + 1) % 2].render)
 *        draw elements from list
 *        (optional) overwrite index with VAO to reduce memory indirections in the next iteration
 *    ***
 *    To prevent noticeable stutter, first and second loop in render must not finish,
 *    but render must then not draw not-uploaded meshes.
 *    ***
 *
 *    if upload list done and remove list done and "enough time passed"
 *        request new task list
 *    else
 *        do not request new task list !!!
 */


#include "TinyAlgebra.hpp"
#include "Block.hpp"
#include <string>
#include <vector>
#include <GL/gl3w.h>
#include <zlib.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <stack>


// TODO: expand
struct Vertex { iVec3 position; int type; ucVec4 shaddow; };

// TODO: try pointers instead of index (what is faster?)
struct Render { int index; iVec3 position; };
struct Remove { int index; };
// TODO: replace vertex vector by custom memory allocation (1 idea: have a large buffer for all meshes. once buffer full, hand over to render thread)
struct Upload { int index; iVec3 position; std::vector<Vertex> mesh; };

struct Mesh { GLuint VAO; GLuint VBO; GLsizei size; };
struct UnusedBuffer { GLuint VAO; GLuint VBO; };
struct MeshMeta { iVec3 position; bool empty; };

struct ChunkMeta { int size; int offset; };

enum class Status : char { UNLOADED, LOADED, CHECKED };

struct Tasks
{
    // TODO: replace by array and size counter if max needed size is not too large
    std::vector<Remove> remove;
    std::vector<Render> render;
    std::vector<Upload> upload;
};

//==============================================================================
class World
{
public:
    World(const char * location);
    ~World();
    void draw(const iVec3 new_center, const fVec4 frustum_planes[6]);

private:
    //==============================================================================
    // constants

    // only edit following line / no need to tinker with the rest
    static constexpr int
            RDISTANCE{ 100 },
            REDISTANCE{ RDISTANCE * 2 },
            CSIZE{ 16 },
            MSIZE{ 16 },
            MCSIZE{ 60 },
            MESH_BORDER_REQUIRED_SIZE{ 1 },
            MOFF{ MESH_BORDER_REQUIRED_SIZE + 3 }, // or maybe do chunk_size / 2
            //MOFF{ 0 },
            RSIZE{ 512 / CSIZE },
            CCSIZE{ 16 },
            RCSIZE{ (MSIZE * MCSIZE + MOFF) / (CSIZE * RSIZE) + 3 }; // round up + 2 instead of + 3 would be prettier

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
    static constexpr int REGION_CONTAINER_SIZE_X{ RCSIZE }, REGION_CONTAINER_SIZE_Y{ RCSIZE }, REGION_CONTAINER_SIZE_Z{ RCSIZE };
    static constexpr int RENDER_DISTANCE_X{ RDISTANCE }, RENDER_DISTANCE_Y{ RDISTANCE }, RENDER_DISTANCE_Z{ RDISTANCE };
    static constexpr int REMOVE_DISTANCE_X{ REDISTANCE }, REMOVE_DISTANCE_Y{ REDISTANCE }, REMOVE_DISTANCE_Z{ REDISTANCE };

    static constexpr int CHUNK_SIZE{ CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z };
    static constexpr int CHUNK_CONTAINER_SIZE{ CHUNK_CONTAINER_SIZE_X * CHUNK_CONTAINER_SIZE_Y * CHUNK_CONTAINER_SIZE_Z };
    static constexpr int REGION_CONTAINER_SIZE{ REGION_CONTAINER_SIZE_X * REGION_CONTAINER_SIZE_Y * REGION_CONTAINER_SIZE_Z };
    static constexpr int MESH_CONTAINER_SIZE{ MESH_CONTAINER_SIZE_X * MESH_CONTAINER_SIZE_Y * MESH_CONTAINER_SIZE_Z };
    static constexpr int REGION_SIZE{ REGION_SIZE_X * REGION_SIZE_Y * REGION_SIZE_Z };

    static constexpr unsigned char SHADDOW_STRENGTH{ 60 };

    static constexpr int SQUARE_RENDER_DISTANCE{ RENDER_DISTANCE_X * RENDER_DISTANCE_X + RENDER_DISTANCE_Y * RENDER_DISTANCE_Y + RENDER_DISTANCE_Z * RENDER_DISTANCE_Z };
    static constexpr int SQUARE_REMOVE_DISTANCE{ REMOVE_DISTANCE_X * REMOVE_DISTANCE_X + REMOVE_DISTANCE_Y * REMOVE_DISTANCE_Y + REMOVE_DISTANCE_Z * REMOVE_DISTANCE_Z };

    static_assert(sizeof(Bytef) == sizeof(char), "Assuming that.");
    static constexpr int SOURCE_LENGTH{ sizeof(Block) * CHUNK_SIZE };
    static constexpr int REGION_DATA_SIZE_FACTOR{ SOURCE_LENGTH * 128 };

    static constexpr char WORLD_ROOT[]{ "world/" };

    static constexpr int META_DATA_SIZE{ REGION_SIZE * sizeof(ChunkMeta) };

    static constexpr int SQUARE_LOAD_RESET_DISTANCE{ MSIZE * MSIZE };
    static constexpr int MESH_COUNT_NEEDED_FOR_RESET{ 16 };

    static constexpr iVec3 CHUNK_SIZES{ CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z };
    static constexpr iVec3 REGION_SIZES{ REGION_SIZE_X, REGION_SIZE_Y, REGION_SIZE_Z };
    static constexpr iVec3 CHUNK_CONTAINER_SIZES{ CHUNK_CONTAINER_SIZE_X, CHUNK_CONTAINER_SIZE_Y, CHUNK_CONTAINER_SIZE_Z };
    static constexpr iVec3 REGION_CONTAINER_SIZES{ REGION_CONTAINER_SIZE_X, REGION_CONTAINER_SIZE_Y, REGION_CONTAINER_SIZE_Z };

    static constexpr iVec3 MESH_CONTAINER_SIZES{ MESH_CONTAINER_SIZE_X, MESH_CONTAINER_SIZE_Y, MESH_CONTAINER_SIZE_Z };
    static constexpr iVec3 MESH_SIZES{ MESH_SIZE_X, MESH_SIZE_Y, MESH_SIZE_Z };
    static constexpr iVec3 MESH_OFFSETS{ MESH_OFFSET_X, MESH_OFFSET_Y, MESH_OFFSET_Z };

    //==============================================================================
    // variables

    // loader thread data
    std::thread m_loader_thread;
    std::string m_data_location;
    Block m_blocks[CHUNK_SIZE * CHUNK_CONTAINER_SIZE];
    iVec3 m_chunk_positions[CHUNK_CONTAINER_SIZE];
    bool m_needs_save[CHUNK_CONTAINER_SIZE];
    iVec3 m_mesh_positions[MESH_CONTAINER_SIZE];
    // TODO: more space efficient format than current (3 states only needed)
    Status m_mesh_loaded[MESH_CONTAINER_SIZE];
    // TODO: Maybe replace by array and size counter. Max possible size should be equal to MESH_CONTAINER_SIZE_X * MESH_CONTAINER_SIZE_Y * MESH_CONTAINER_SIZE_Z, but is overkill.
    std::vector<MeshMeta> m_loaded_meshes; // contains all loaded meshes
    struct Region
    {
        iVec3 position;
        ChunkMeta metas[REGION_SIZE];
        Bytef * data; // TODO: replace pointer with RAII mechanism
        int size, container_size;
    } m_regions[REGION_CONTAINER_SIZE];

    // renderer thread data
    Mesh m_meshes[MESH_CONTAINER_SIZE]; // kind of mirrors m_mesh_positions
    std::stack<UnusedBuffer> m_unused_buffers;

    // shared / synchronization data
    Tasks m_tasks[2]; // double buffering
    iVec3 m_center[2];
    int m_back_buffer;
    std::atomic_bool m_quit;
    std::mutex m_lock;
    std::condition_variable m_cond_var;
    bool m_swap;
    std::atomic_bool m_loader_waiting;
    std::atomic_bool m_moved_far;
    std::atomic_bool m_loader_finished;

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
    void meshLoader();
    bool inRange(const iVec3 center_block, const iVec3 position_block, const int square_max_distance);
    void exitLoaderThread();
    static bool meshInFrustum(const fVec4 planes[6], const iVec3 mesh_offset);
    void loadRegion(const iVec3 region_position);
    void saveChunkToRegion(const int chunk_index);
    void saveRegionToDrive(const int region_index);
};

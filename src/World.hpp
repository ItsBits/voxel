#pragma once

/*
 * TODO:
 * upload and render chunks
 */





/* TODO:
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

/*
 * UPDATE:
 * loader should have a container that is reset in every buffer swap.
 * it should be used for flood fill mesh loading
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


// TODO: expand
struct Vertex { int x, y, z, type; ucVec4 shaddow; };

// TODO: try pointers instead of index (what is faster?)
union Render { int index; struct { GLuint VAO; GLint size; }; }; // VAO for renderer to overwrite it to reduce memory indirections
struct Remove { int index; };
// TODO: replace vertex vector by custom memory allocation (1 idea: have a large buffer for all meshes. once buffer full, hand over to render thread)
struct Upload { int index; std::vector<Vertex> mesh; };

struct Mesh { GLuint m_VAO; GLuint m_VBO; GLsizei size; };
struct MeshMeta { iVec3 position; int size; };

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
    // TODO: pass center and frustum to the function
    void draw();

private:
    //==============================================================================
    // constants

    // only edit following line / no need to tinker with the rest
    static constexpr int
            RDISTANCE{ 100 },
            CSIZE{ 32 },
            MIN_CCSIZE{ 0 },
            MSIZE{ 16 },
            MCSIZE{ 16 },
            MESH_BORDER_REQUIRED_SIZE{ 1 },
            MOFF{ MESH_BORDER_REQUIRED_SIZE },
            RSIZE{ 512 / CSIZE },
            RCSIZE{ (MSIZE * MCSIZE + MOFF) / (CSIZE * RSIZE) + 3}; // round up + 2 instead of + 3 would be prettier

    static_assert(CSIZE > 0 && MSIZE > 0 && MCSIZE > 0 && MESH_BORDER_REQUIRED_SIZE >= 0 && RSIZE > 0 && RCSIZE > 0, "Parameters must be positive.");
    static_assert((RDISTANCE * 2) / MSIZE < MCSIZE, "Mesh container too small for the render distance.");

    static constexpr int CCSIZE{ maxC(((MESH_BORDER_REQUIRED_SIZE + MSIZE) + (CSIZE - 1)) / CSIZE, MIN_CCSIZE) }; // round up and pick max so meshes can actually load
    static constexpr int CHUNK_SIZE_X{ CSIZE }, CHUNK_SIZE_Y{ CSIZE }, CHUNK_SIZE_Z{ CSIZE };
    static constexpr int CHUNK_CONTAINER_SIZE_X{ CCSIZE }, CHUNK_CONTAINER_SIZE_Y{ CCSIZE }, CHUNK_CONTAINER_SIZE_Z{ CCSIZE };
    static constexpr int MESH_SIZE_X{ MSIZE }, MESH_SIZE_Y{ MSIZE }, MESH_SIZE_Z{ MSIZE };
    static constexpr int MESH_CONTAINER_SIZE_X{ MCSIZE }, MESH_CONTAINER_SIZE_Y{ MCSIZE }, MESH_CONTAINER_SIZE_Z{ MCSIZE };
    static constexpr int MESH_OFFSET_X{ MOFF }, MESH_OFFSET_Y{ MOFF }, MESH_OFFSET_Z{ MOFF };
    static constexpr int REGION_SIZE_X{ RSIZE }, REGION_SIZE_Y{ RSIZE }, REGION_SIZE_Z{ RSIZE };
    static constexpr int REGION_CONTAINER_SIZE_X{ RCSIZE }, REGION_CONTAINER_SIZE_Y{ RCSIZE }, REGION_CONTAINER_SIZE_Z{ RCSIZE };
    static constexpr int RENDER_DISTANCE_X{ RDISTANCE }, RENDER_DISTANCE_Y{ RDISTANCE }, RENDER_DISTANCE_Z{ RDISTANCE };

    //==============================================================================
    // variables

    // loader thread data
    std::thread m_loader_thread;
    std::string m_data_location;
    Block m_blocks[CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z * CHUNK_CONTAINER_SIZE_X * CHUNK_CONTAINER_SIZE_Y * CHUNK_CONTAINER_SIZE_Z];
    iVec3 m_chunk_positions[CHUNK_CONTAINER_SIZE_X * CHUNK_CONTAINER_SIZE_Y * CHUNK_CONTAINER_SIZE_Z];
    iVec3 m_mesh_positions[MESH_CONTAINER_SIZE_X * MESH_CONTAINER_SIZE_Y * MESH_CONTAINER_SIZE_Z];
    // TODO: more space efficient format than current (3 states only needed)
    Status m_mesh_loaded[MESH_CONTAINER_SIZE_X * MESH_CONTAINER_SIZE_Y * MESH_CONTAINER_SIZE_Z];
    // TODO: Maybe replace by array and size counter. Max possible size should be equal to MESH_CONTAINER_SIZE_X * MESH_CONTAINER_SIZE_Y * MESH_CONTAINER_SIZE_Z, but is overkill.
    std::vector<MeshMeta> m_loaded_meshes; // contains all loaded meshes
    struct Region
    {
        iVec3 position;
        ChunkMeta metas[REGION_SIZE_X * REGION_SIZE_Y * REGION_SIZE_Z];
        Bytef * data; // TODO: replace pointer with RAII mechanism
        int size;
    } m_regions[REGION_CONTAINER_SIZE_X * REGION_CONTAINER_SIZE_Y * REGION_CONTAINER_SIZE_Z];

    // renderer thread data
    Mesh m_meshes[MESH_CONTAINER_SIZE_X * MESH_CONTAINER_SIZE_Y * MESH_CONTAINER_SIZE_Z]; // kind of mirrors m_mesh_positions

    // shared / synchronization data
    Tasks m_tasks[2]; // double buffering
    iVec3 m_center[2];
    int m_back_buffer;
    std::atomic_bool m_quit;
    std::mutex m_lock;
    std::condition_variable m_cond_var;
    bool m_swap;
    bool m_loader_waiting;

    //==============================================================================
    // functions

    Block & getBlock(const iVec3 block_position);
    void loadChunkRange(const iVec3 from_block, const iVec3 to_block);
    void loadChunk(const iVec3 chunk_position);
    std::vector<Vertex> generateMesh(const iVec3 from_block, const iVec3 to_block);
    static unsigned char vertAO(const bool side_a, const bool side_b, const bool corner);
    void generateChunk(const iVec3 from_block);
    void meshLoader();
    bool inRenderRange(const iVec3 center_block, const iVec3 position_block);
    void exitLoaderThread();

};

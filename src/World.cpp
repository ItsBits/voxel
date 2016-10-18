#include "World.hpp"
#include "QuadEBO.hpp"
#include <queue>
#include <cassert>

//==============================================================================
World::World(const char * location) :
        m_data_location{ location },
        m_center{ { 0, 0, 0 }, { 0, 0, 0 } },
        m_back_buffer{ 0 },
        m_quit{ false },
        m_swap{ false },
        m_loader_waiting{ false }
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
    }
    m_regions[0].position = { 1, 0, 0 };

    m_loader_thread = std::thread{ &World::meshLoader, this };
}

//==============================================================================
World::~World()
{
    exitLoaderThread();

    // TODO: save changes

    // cleanup
    for (auto & i : m_regions) delete [] i.data;
}

//==============================================================================
Block & World::getBlock(const iVec3 block_position)
{
    constexpr iVec3 CHUNK_SIZE{ CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z };
    constexpr iVec3 CHUNK_CONTAINER_SIZE{ CHUNK_CONTAINER_SIZE_X, CHUNK_CONTAINER_SIZE_Y, CHUNK_CONTAINER_SIZE_Z };

    const auto relative_position = floorMod(block_position, CHUNK_SIZE);
    const auto chunk_position = floorDiv(block_position, CHUNK_SIZE);
    const auto chunk_relative = floorMod(chunk_position, CHUNK_CONTAINER_SIZE);

    const auto block_index = toIndex(relative_position, CHUNK_SIZE);

    const auto chunk_index = toIndex(chunk_relative, CHUNK_CONTAINER_SIZE);

    return m_blocks[chunk_index * CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z + block_index];
}

//==============================================================================
void World::loadChunkRange(const iVec3 from_block, const iVec3 to_block)
{
    constexpr iVec3 CHUNK_SIZE{ CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z };

    const auto chunk_position_from = floorDiv(from_block, CHUNK_SIZE);
    const auto chunk_position_to = floorDiv(to_block - 1, CHUNK_SIZE);

    iVec3 it;

    for (it(2) = chunk_position_from(2); it(2) <= chunk_position_to(2); ++it(2))
        for (it(1) = chunk_position_from(1); it(1) <= chunk_position_to(1); ++it(1))
            for (it(0) = chunk_position_from(0); it(0) <= chunk_position_to(0); ++it(0))
            {
                loadChunk(it);
            }
}

//==============================================================================
void World::loadChunk(const iVec3 chunk_position)
{
    constexpr iVec3 CHUNK_CONTAINER_SIZE{ CHUNK_CONTAINER_SIZE_X, CHUNK_CONTAINER_SIZE_Y, CHUNK_CONTAINER_SIZE_Z };
    constexpr iVec3 REGION_CONTAINER_SIZE{ REGION_CONTAINER_SIZE_X, REGION_CONTAINER_SIZE_Y, REGION_CONTAINER_SIZE_Z };
    constexpr iVec3 REGION_SIZE{ REGION_SIZE_X, REGION_SIZE_Y, REGION_SIZE_Z };
    constexpr iVec3 CHUNK_SIZE{ CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z };

    const auto chunk_relative = floorMod(chunk_position, CHUNK_CONTAINER_SIZE);

    const auto chunk_index = toIndex(chunk_relative, CHUNK_CONTAINER_SIZE);

    // if already loaded
    if (all(m_chunk_positions[chunk_index] == chunk_position)) return;

    const auto region_position = floorDiv(chunk_position, REGION_SIZE);
    const auto region_relative = floorMod(region_position, REGION_CONTAINER_SIZE);

    const auto region_index = toIndex(region_relative, REGION_CONTAINER_SIZE);
    const auto chunk_in_region_relative = floorMod(chunk_position, REGION_SIZE);
    const auto chunk_in_region_index = toIndex(chunk_in_region_relative, REGION_SIZE);

    // load region file
    if (any(m_regions[region_index].position != region_position))
    {
        // TODO: load / create region file
    }

    // generate new chunk
    if (m_regions[region_index].metas[chunk_in_region_index].size == 0)
    {
        generateChunk(chunk_position * CHUNK_SIZE);
        m_chunk_positions[chunk_index] = chunk_position;
    }
    // load chunk from region
    else
    {
        // TODO: load chunk from region
    }
}

//==============================================================================
std::vector<Vertex> World::generateMesh(const iVec3 from_block, const iVec3 to_block)
{
    constexpr unsigned char SHADDOW_STRENGTH{ 60 };
    constexpr decltype(MESH_BORDER_REQUIRED_SIZE) BORDER{ MESH_BORDER_REQUIRED_SIZE }; // hack to prevent undefined reference // TODO: research why this happens
    loadChunkRange(from_block - BORDER, to_block + BORDER);

    iVec3 position;
    std::vector<Vertex> mesh;

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
    // TODO: pre calculate chunk offset to improve performance (getBlock() recalculates chunk offset every time it is called)

    constexpr iVec3 CHUNK_SIZE{ CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z };

    const iVec3 to_block = from_block + CHUNK_SIZE;

    iVec3 position;

    for (position(2) = from_block(2); position(2) < to_block(2); ++position(2))
        for (position(1) = from_block(1); position(1) < to_block(1); ++position(1))
            for (position(0) = from_block(0); position(0) < to_block(0); ++position(0))
            {
                getBlock(position) = Block{ std::rand() % 300 == 0 };
            }
}

//==============================================================================
void World::meshLoader()
{
    while (!m_quit)
    {
        Tasks & tasks = m_tasks[m_back_buffer];
        const iVec3 center = m_center[m_back_buffer];

        // reset
        tasks.remove.clear();
        tasks.render.clear();
        tasks.upload.clear();

        for (auto & i : m_mesh_loaded) i = Status::UNLOADED;

        constexpr iVec3 MESH_CONTAINER_SIZE{ MESH_CONTAINER_SIZE_X, MESH_CONTAINER_SIZE_Y, MESH_CONTAINER_SIZE_Z };
        constexpr iVec3 MESH_SIZE{ MESH_SIZE_X, MESH_SIZE_Y, MESH_SIZE_Z };
        constexpr iVec3 MESH_OFFSET{ MESH_OFFSET_X, MESH_OFFSET_Y, MESH_OFFSET_Z };

        // update remove and render in task list
        std::size_t count = m_loaded_meshes.size();
        for (std::size_t i = 0; i < count;)
        {
            const iVec3 mesh_center = m_loaded_meshes[i].position * MESH_SIZE + MESH_OFFSET + (MESH_SIZE / 2);

            const auto mesh_relative = floorMod(m_loaded_meshes[i].position, MESH_CONTAINER_SIZE);
            const auto mesh_index = toIndex(mesh_relative, MESH_CONTAINER_SIZE);

            if (!inRenderRange(center, mesh_center))
            {
                tasks.remove.push_back({ mesh_index });
                m_loaded_meshes[i] = m_loaded_meshes[--count]; // TODO: could check if in range and only swap with first that is in range from back
            }
            else
            {
                if (m_loaded_meshes[i].size > 0)
                    tasks.render.push_back({ mesh_index, m_loaded_meshes[i].position });
                m_mesh_loaded[mesh_index] = Status::LOADED;
                ++i;
            }

        }
        m_loaded_meshes.resize(count);

        // find meshes to generate and generate them
        std::queue<iVec3> check_list;
        // add center mesh
        const auto center_mesh = floorDiv(center - MESH_OFFSET, MESH_SIZE);
        check_list.push(center_mesh);

        // breadth first search finds all borders of loaded are and loads it
        while (!check_list.empty())
        {
            const auto current = check_list.front();
            check_list.pop();
            const auto current_index = absoluteToIndex(current, MESH_CONTAINER_SIZE);

            if (m_mesh_loaded[current_index] == Status::CHECKED)
            {
                continue;
            }
            // load
            else if (m_mesh_loaded[current_index] == Status::UNLOADED)
            {
                const auto from_block = current * MESH_SIZE + MESH_OFFSET;
                const auto to_block = from_block + MESH_SIZE;
                const auto mesh = generateMesh(from_block, to_block);

                if (mesh.size() > 0) tasks.upload.push_back({ current_index, current, mesh });

                m_mesh_loaded[current_index] = Status::CHECKED;
                m_loaded_meshes.push_back({ current, static_cast<int>(mesh.size()) });
            }
            // push neighbours that are in render radius
            else if (m_mesh_loaded[current_index] == Status::LOADED)
            {
                const iVec3 neighbours[6]{
                        center + iVec3{ 1, 0, 0 }, center + iVec3{ 0, 1, 0 }, center + iVec3{ 0, 0, 1 },
                        center - iVec3{ 1, 0, 0 }, center - iVec3{ 0, 1, 0 }, center - iVec3{ 0, 0, 1 },
                };

                for (const iVec3 * pos = neighbours; pos < neighbours + 6; ++pos)
                    if (inRenderRange(center, *pos * MESH_SIZE + MESH_OFFSET + (MESH_SIZE / 2)))
                        check_list.push(*pos);
            }
            else
            {
               assert(0);
            }
            m_mesh_loaded[current_index] = Status::CHECKED;
        }

        {
            // request task buffer swap wait for it
            std::unique_lock<std::mutex> lock{m_lock};
            m_loader_waiting = true;
            m_cond_var.wait(lock, [this] { return m_swap || m_quit; });
            m_swap = false;
        }
    }
}

//==============================================================================
bool World::inRenderRange(const iVec3 center_block, const iVec3 position_block)
{
    constexpr int SQUARE_RENDER_DISTANCE{
            RENDER_DISTANCE_X * RENDER_DISTANCE_X +
            RENDER_DISTANCE_Y * RENDER_DISTANCE_Y +
            RENDER_DISTANCE_Z * RENDER_DISTANCE_Z
    };

    const iVec3 distances = position_block - center_block;
    const int square_distance = dot(distances, distances);

    return square_distance <= SQUARE_RENDER_DISTANCE;
}

//==============================================================================
void World::draw()
{
    Tasks & tasks = m_tasks[(m_back_buffer + 1) % 2];
    const iVec3 center = m_center[(m_back_buffer + 1) % 2];

    // remove
    if (!tasks.remove.empty())
    {
      const Remove & task = tasks.remove.back();

      auto & mesh_data = m_meshes[task.index];
      m_unused_buffers.push({ mesh_data.VAO, mesh_data.VBO });
      mesh_data.VAO = 0;
      mesh_data.VBO = 0;

      // TODO: resize buffer to 0 to save memory

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
        const int EBO_size = (static_cast<int>(task.mesh.size() >> 2) + task.mesh.size());
        m_meshes[task.index].size = EBO_size;
        QuadEBO::resize(EBO_size);

        tasks.upload.pop_back();
    }

    // render
    for (auto & m : tasks.render)
    {
        if (inFrustum(/*m_position*/)) // TODO: frustum culling
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
        std::unique_lock<std::mutex> lock{ m_lock };
        if (m_loader_waiting)
        {
            m_cond_var.notify_all();
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    m_loader_thread.join();
}

//==============================================================================
bool World::inFrustum()
{
    // TODO: implement (needs AABB and frustum data passed in to compute)
    return true;
}

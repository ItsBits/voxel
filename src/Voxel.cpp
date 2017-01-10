#include "Voxel.hpp"
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include "Mouse.hpp"
#include "TinyAlgebraExtensions.hpp"
#include "Debug.hpp"
#include "Profiler.hpp"
#include "Keyboard.hpp"
#include <glm/gtx/string_cast.hpp>

//==============================================================================
static const std::vector<TextureArray::Source> BLOCK_TEXTURE_SOURCE
{
        { "assets/blocks/grass.png", 1 },
        { "assets/blocks/sand.png", 2 },
        { "assets/blocks/gosh.png", 3 }
};
static const Texture::Filtering BLOCK_TEXTURE_FILTERING
{
        Texture::FarFiltering::LINEAR_TEXEL_LINEAR_MIPMAP,
        Texture::CloseFiltering::LINEAR_TEXEL, 500.0f
};

static const Texture::Filtering MC_BLOCK_TEXTURE_FILTERING
{
    Texture::FarFiltering::NEAREST_TEXEL_LINEAR_MIPMAP,
    Texture::CloseFiltering::NEAREST_TEXEL, 0.0f // mesa-driver bug: can not have anisotropy filtering and nearest texel interpolation at the same time (works on windows though)
};

//==============================================================================
std::vector<TextureArray::Source> readMCBlocks()
{
    const std::string info_file_location{ "assets/mc_blocks.txt" };
    const std::string texture_folder_location{ MC_TEXTURE_LOC };
    const std::string texture_file_suffix{ ".png" };
    const std::string missing_texture{ "assets/missing_16.png" };

    std::vector<TextureArray::Source> data;

    std::ifstream info{ info_file_location };

    while(!info.eof())
    {
        GLsizei i{ -1 };
        std::string name{ "dummy_lalala" };

        info >> std::hex >> i;
        info >> name;

        if (i == -1 || name == std::string{ "dummy_lalala" })
            break;

        data.push_back({ texture_folder_location + name + texture_file_suffix, i });
    }

    for (GLsizei i = 0; i < 256; ++i)
    {
        bool exists = false;

        for (const auto & e : data)
        {
            if (e.index == i)
            {
                exists = true;
                break;
            }
        }

        if (exists) continue;

        data.push_back({ missing_texture, i });
    }

    return data;
}

//==============================================================================
Voxel::Voxel(const std::string & name) :
    m_window{ Window::Hints{ 3, 1, MSAA_SAMPLES, nullptr, name, 0.9f, 0.9f, 0.6f, 1.0f, V_SYNC, RESOLUTION_X, RESOLUTION_Y } },
    m_block_shader{
            {
                    { "shader/block.vert", GL_VERTEX_SHADER },
                    { "shader/block.frag", GL_FRAGMENT_SHADER }
            }
    },
//    m_block_textures{ BLOCK_TEXTURE_SOURCE, 64, GL_TEXTURE0, MC_BLOCK_TEXTURE_FILTERING, GL_CLAMP_TO_EDGE }, // TODO: make dynamic texture unit allocation
    m_block_textures{ readMCBlocks(), 16, GL_TEXTURE0, MC_BLOCK_TEXTURE_FILTERING, GL_CLAMP_TO_EDGE }, // TODO: make dynamic texture unit allocation
    m_text_shader{
            {
                    { "shader/text.vert", GL_VERTEX_SHADER },
                    { "shader/text.geom", GL_GEOMETRY_SHADER },
                    { "shader/text.frag", GL_FRAGMENT_SHADER }
            }
    }
{
    m_block_shader.use();
    m_block_VP_matrix_location = glGetUniformLocation(m_block_shader.id(), "VP_matrix");
    GLint block_texture_array_location = glGetUniformLocation(m_block_shader.id(), "block_texture_array");
    glUniform1i(block_texture_array_location, 0);
    m_block_light_location = glGetUniformLocation(m_block_shader.id(), "light");
    m_block_lighting_location = glGetUniformLocation(m_block_shader.id(), "lighting");
    m_chunk_position_location = glGetUniformLocation(m_block_shader.id(), "offset");

    m_text_shader.use();
    m_text_ratio_location = glGetUniformLocation(m_text_shader.id(), "ratio");
    m_font_size_location = glGetUniformLocation(m_text_shader.id(), "font_size");
    GLint font_texture_array_location = glGetUniformLocation(m_text_shader.id(), "font_texture_array");
    glUniform1i(font_texture_array_location, 1);

// TODO: refactor past here

    //================================================================================================
    const std::string pre{ "assets/dejavu_sans_mono_stretched_and_my/" };
    const std::string post{ ".png" };

    std::vector<TextureArray::Source> TEXT_TEXTURE_SOURCE;

    for (auto i = 0; i < 128; i++)
    {
        assert(i >= 0 && i <= 127);

        GLsizei index = i;
        if (index < 32 || index == 127) index = 0; // No representation
        // std::string, GLsizei
        // Can't use emplace_back because there is no constructor defined for TextureArray::Source?
      TEXT_TEXTURE_SOURCE.push_back(TextureArray::Source{ std::string{ pre + std::to_string(index) + post }, i });
    }


  //const GLsizei TEXT_TEXTURE_SIZE{ 8 };
  const GLsizei TEXT_TEXTURE_SIZE{ 16 };
  //const GLsizei TEXT_TEXTURE_SIZE{ 256 };

  const Texture::Filtering TEXT_TEXTURE_FILTERING
          {
                  Texture::FarFiltering::LINEAR_TEXEL_LINEAR_MIPMAP,
                  Texture::CloseFiltering::LINEAR_TEXEL, 0.0f
          };

  m_font_textures.reload(
          TEXT_TEXTURE_SOURCE,
          TEXT_TEXTURE_SIZE,
          GL_TEXTURE1,
          TEXT_TEXTURE_FILTERING,
          GL_CLAMP_TO_EDGE
  );

  // TODO: https://lambdacube3d.wordpress.com/2014/11/12/playing-around-with-font-rendering/
}

//==============================================================================
void Voxel::run()
{
    m_window.makeContextCurrent();
    m_window.unlockMouse();

    double last_time = glfwGetTime();

    int frame_counter = 0;
    double last_fps_update = last_time;

    while (!m_window.exitRequested())
    {
        const double current_time = glfwGetTime();
        double delta_time = current_time - last_time; // TODO: separate framerate from user input, introtuce fixed timestamp for user updates (except maybe head rotation)
        last_time = current_time;

        // update FPS counter
        if (current_time - last_fps_update > 1.0 / FRAME_RATE_UPDATE_RATE)
        {
            const double frame_rate = static_cast<double>(frame_counter) / (current_time - last_fps_update);
            Debug::print("FPS: ", frame_rate);
            frame_counter = 0;
            last_fps_update = current_time;

#if 1
            const auto pos = m_player.getPosition();
            const auto int_pos = intFloor(fVec3{ pos.x, pos.y, pos.z });
            const auto current_settings = m_settings.current();
            const auto current_settings_val = m_settings.getInt(current_settings);
            m_screen_text.update("FPS: " + std::to_string(static_cast<int>(frame_rate + 0.5)) + "\n" +
                                 std::to_string(int_pos(0)) + "|" +
                                 std::to_string(int_pos(1)) + "|" +
                                 std::to_string(int_pos(2)) + "\n" +
                                 "Settings:" + std::to_string(current_settings) + " => " + std::to_string(current_settings_val)
            );
#else // demo
            m_screen_text.update(
                    "!\"#$%&\\'()*+,-./:;<=>?@[]^_`{|}~\n"
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZ\n"
                    "abcdefghijklmnopqrstuvwxyz\n"
                    "0123456789"
            );
#endif
        }
        ++frame_counter;

        glfwPollEvents();

        updateSettings();

        const auto scroll = Mouse::getScrollMovement()(1);
        if (scroll > 0.1) m_window.unlockMouse();
        else if (scroll < -0.1) m_window.lockMouse();

        // update position and stuff
        m_player.updateSpeed(m_settings.get(SPD_P));
        m_player.updateCameraAndItems();
        m_player.updateVelocity(static_cast<float>(delta_time));
        m_player.applyVelocity(static_cast<float>(delta_time));
        m_camera.updateAspectRatio(static_cast<float>(m_window.aspectRatio()));
        m_camera.update(m_player.getPosition()
#if 0
                        + glm::vec3{ 0, 150, 0 }
#endif
                , m_player.getYaw(), m_player.getPitch());

        // render blocks
        m_block_shader.use();
        const glm::mat4 VP_matrix = m_camera.getViewProjectionMatrix();
        glUniformMatrix4fv(m_block_VP_matrix_location, 1, GL_FALSE, glm::value_ptr(VP_matrix));

        //const auto light = (static_cast<float>(std::sin(current_time)) + 1) / 2.0;
        const auto light = static_cast<float>(m_settings.get(LIGHT_L));
        const auto r = static_cast<float>(m_settings.get(RED_L));
        const auto g = static_cast<float>(m_settings.get(GRE_L));
        const auto b = static_cast<float>(m_settings.get(BLU_L));
        glUniform1f(m_block_light_location, light);
        glUniform3f(m_block_lighting_location, r, g, b);

        const auto center = m_player.getPosition();
        fVec4 frustum_planes[6];
        matrixToFrustums(VP_matrix, frustum_planes);
        m_world.draw(intFloor(fVec3{ center.x, center.y, center.z }), frustum_planes, m_chunk_position_location);

        // render text
        m_text_shader.use();
        glUniform1f(m_text_ratio_location, static_cast<GLfloat>(m_window.aspectRatio()));
        glUniform1f(m_font_size_location, 0.07f);
        m_screen_text.draw();

        // TODO: render sky box

        m_window.swapResizeClearBuffer();

        // limit frame rate
        const auto time_after_render = glfwGetTime();
        const auto sleep_time = 1.0 / TARGET_FRAME_RATE - (time_after_render - current_time);
        if (sleep_time > 0.0)
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int64_t>(sleep_time * 1000.0)));

        { const GLenum r = glGetError(); assert(r == GL_NO_ERROR); }

    }

    m_window.unlockMouse();
    m_window.swapResizeClearBuffer();
}

//==============================================================================
void Voxel::updateSettings()
{
    // assuming number keys are consecutive (which they are in my GLFW3 version)
    for (int i = 0; i < 10; ++i)
        if (Keyboard::getKey(GLFW_KEY_0 + i) == Keyboard::Status::PRESSED)
        {
            m_settings.change(i);
            break;
        }

    if (Keyboard::getKey(GLFW_KEY_W) == Keyboard::Status::PRESSED)
        m_settings.increment();
    else if (Keyboard::getKey(GLFW_KEY_S) == Keyboard::Status::PRESSED)
        m_settings.decrement();

}

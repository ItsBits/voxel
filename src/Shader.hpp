//==============================================================================
// Shader.hpp
//
// Author: Tomaž Vöröš
//==============================================================================
//
#pragma once

#include <vector>
#include <string>

#include <GL/gl3w.h>


class Shader
{
public:
    // Shader source input format
    struct Source
    {
        std::string file_name;
        GLenum type;
    };

    // Constructor
    Shader(const std::vector<Shader::Source> & shader_source);
    Shader() { m_id = 0; };
    Shader(const Shader &) = delete;

    // Destructor
    ~Shader();

    // Reload shader
    void reload(const std::vector<Shader::Source> & shader_source);

    // Use shader
    void use() const { glUseProgram(m_id); }

    // Get shader native handle
    GLuint id() const { return m_id; }

private:
    GLuint m_id{ 0 };

    // TODO: inline error checking
    enum class Process { COMPILING, LINKING };
    static const constexpr GLsizei MAX_ERROR_LOG_LENGTH = 1024;
    bool compileLinkSuccess(GLuint shader, Shader::Process type, std::string file_name = std::string(""));

    void load(const std::vector<Shader::Source> & shader_source);

};
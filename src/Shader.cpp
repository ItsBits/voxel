//==============================================================================
// Shader.hpp
//
// Author: Tomaž Vöröš
//==============================================================================
//
#include "Shader.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cassert>

//==============================================================================
Shader::Shader(const std::vector<Shader::Source> & shader_source)
{
  load(shader_source);
}

//==============================================================================
Shader::~Shader()
{
  glDeleteProgram(m_id);
  m_id = 0;
}

//==============================================================================
void Shader::reload(const std::vector<Shader::Source>& shader_source)
{
  glDeleteProgram(m_id);
  m_id = 0;
  load(shader_source);
}

//==============================================================================
void Shader::load(const std::vector<Shader::Source>& shader_source)
{
  std::vector<GLuint> shader_object_ids;

  // Create shader program
  m_id = 0;
  m_id = glCreateProgram();

  // Check if error occured in shader creation
  if (m_id == 0)
  {
    return;
  }

  for (const auto & i_shader : shader_source)
  {
    // Check if shader type is valid
    if (i_shader.type != GL_VERTEX_SHADER &&
        i_shader.type != GL_GEOMETRY_SHADER &&
        i_shader.type != GL_FRAGMENT_SHADER)
    {
      break;
    }

    // Open file
    std::ifstream shader_file(i_shader.file_name, std::ifstream::in | std::ifstream::binary);

    if (shader_file.is_open() == false)
    {
      break;
    }

    // Read file in
    std::string source((std::istreambuf_iterator<char>(shader_file)), std::istreambuf_iterator<char>());

    // Close file
    shader_file.close();

    // Create shader object
    GLuint object_id{ 0 };
    object_id = glCreateShader(i_shader.type);

    // Check if error occured in shader object creation
    if (object_id == 0)
    {
      break;
    }
    // Load shader source
    const GLchar * c_code = source.c_str();
    glShaderSource(object_id, 1, &c_code, nullptr);

    // Compile shader
    glCompileShader(object_id);

    // Check for errors during compilation
    if (compileLinkSuccess(object_id, Process::COMPILING, i_shader.file_name) == false)
    {
      glDeleteShader(object_id);
      break;
    }

    // Add shader object to program
    glAttachShader(m_id, object_id);

    // Flag shader for deletion
    glDeleteShader(object_id);

    // Add shader object to list
    shader_object_ids.push_back(object_id);
  }

  // Link shader program
  glLinkProgram(m_id);

  // Check for linking errors
  if (compileLinkSuccess(m_id, Process::LINKING) == false)
  {
    glDeleteProgram(m_id);
    m_id = 0;
  }

  // Detach the shaders which will delete them
  for (const auto& i : shader_object_ids)
    glDetachShader(m_id, i);

}

//==============================================================================
bool Shader::compileLinkSuccess(GLuint shader, Shader::Process type, std::string file_name)
{
  assert(type == Process::COMPILING || type == Process::LINKING);

  GLint success;
  GLchar infoLog[Shader::MAX_ERROR_LOG_LENGTH];

  if (type == Process::COMPILING)
  {
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success == GL_FALSE)
    {
      glGetShaderInfoLog(shader, Shader::MAX_ERROR_LOG_LENGTH, nullptr, infoLog);
      std::cout << "ERROR: Shader Compilation Error in File: " << file_name << std::endl
                << infoLog
                << "------------------------------------------------------------" << std::endl;
      return false;
    }
  }
  else if (type == Process::LINKING)
  {
    glGetProgramiv(shader, GL_LINK_STATUS, &success);
    if (success == GL_FALSE)
    {
      glGetProgramInfoLog(shader, Shader::MAX_ERROR_LOG_LENGTH, nullptr, infoLog);
      std::cout << "ERROR: Linking Shader:" << std::endl
                << infoLog
                << "------------------------------------------------------------" << std::endl;
      return false;
    }
  }

  return true;
}
#pragma once

#include <vector>
#include <string>
#include "Texture.hpp"

class TextureArray : public Texture
{
public:  
  // Texture array source data format
  struct Source
  {
    std::string file_name;
    GLsizei index;
  };

  // Constructor
  TextureArray(
    const std::vector<TextureArray::Source> & texture_source,
    GLsizei texture_size,
    GLenum texture_unit,
    Texture::Filtering filtering
  );

  // Placement constructor
  inline void reload(
    const std::vector<TextureArray::Source> & texture_source,
    GLsizei texture_size,
    GLenum texture_unit,
    Texture::Filtering filtering
  )
  {
    this->~TextureArray();
    new (this) TextureArray(texture_source, texture_size, texture_unit, filtering);
  }
  
  // Default constructor contains no OpenGL texture
  TextureArray() {}

  // Destructor
  ~TextureArray();

  // Bind texture to texture unit. No bounds checking
  void bind(GLenum texture_unit);

private:
  GLuint m_id{ 0 };
  std::size_t m_tex_per_row{ 0 };

};

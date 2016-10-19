//------------------------------------------------------------------------------
// TextureArray.cpp
//
// Author: Tomaž Vöröš
//------------------------------------------------------------------------------
//
#include "TextureArray.hpp"
#include <SOIL/SOIL.h>
#include <iostream>
#include <glm/gtx/string_cast.hpp>

//------------------------------------------------------------------------------
TextureArray::TextureArray(
  const std::vector<TextureArray::Source>& texture_source,
  GLsizei texture_size,
  GLenum texture_unit,
  Texture::Filtering filtering
)
{
  // Does texture unit exist
  if (!checkTextureUnit(texture_unit))
    throw std::runtime_error("Invalid texture image unit in texture array construction.");

  // Check max texture resolution
  if (texture_size > getMaxTextureSize())
    throw std::runtime_error("Texture array resolution too high. Max is: " + getMaxTextureSize());

  // Find max index
  GLsizei max_index = 0;

  for (const auto& i : texture_source)
    if (i.index > max_index)
      max_index = i.index;
    else if (i.index < 0)
      throw std::runtime_error("Negative texture array index is invalid.");

  // Check max texture layers
  if (max_index + 1 > getMaxTextureArrayLayers())
    throw std::runtime_error("Too many texture array layers.");

  // Generate texture id
  glGenTextures(1, &m_id);

  // Select texture unit
  glActiveTexture(texture_unit);

  // Bind texture
  glBindTexture(GL_TEXTURE_2D_ARRAY, m_id);

  // Allocate memory for textures
  glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, texture_size, texture_size,
    max_index + 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

  // Set texture parameters
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  setFiltering(GL_TEXTURE_2D_ARRAY, filtering);

  for (const auto & i : texture_source)
  {
    int width = 0, height = 0;
    unsigned char* image = nullptr;

    // Load texture
    image = SOIL_load_image(i.file_name.c_str(), &width, &height, nullptr, SOIL_LOAD_RGBA);

    // Check for errors
    if (image == nullptr) throw std::runtime_error("Can't load file: " + i.file_name);
    if (width != texture_size || height != texture_size) throw std::runtime_error("Mismatching texture resolution in texture array.");

    // Flip y coordinate because of OpenGL texture coordinates
    flipImageY(image, texture_size, texture_size, 4);

    // Upload texture to GPU
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, i.index, texture_size, texture_size, 1, GL_RGBA, GL_UNSIGNED_BYTE, image);

    // Delete texture from main memory
    SOIL_free_image_data(image);
  }

  // Generate Mip-Maps
  generateMipMapsIfNeeded(GL_TEXTURE_2D_ARRAY, filtering);
}

//==============================================================================
TextureArray::~TextureArray()
{
  glDeleteTextures(1, &m_id);
}

//==============================================================================
void TextureArray::bind(GLenum texture_unit)
{
  glActiveTexture(texture_unit);
  glBindTexture(GL_TEXTURE_2D_ARRAY, m_id);
}

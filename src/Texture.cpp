#include "Texture.hpp"

#include <cassert>
#include <memory>
#include <stdexcept>
#include <cstring>

#include <glm/common.hpp>

//------------------------------------------------------------------------------
void Texture::setFiltering(GLenum target, Texture::Filtering filtering)
{
  switch (filtering.far_filter)
  {
  case Texture::FarFiltering::NEAREST_TEXEL:
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    break;
  case Texture::FarFiltering::NEAREST_TEXEL_NEAREST_MIPMAP:
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    break;
  case Texture::FarFiltering::NEAREST_TEXEL_LINEAR_MIPMAP:
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    break;
  case Texture::FarFiltering::LINEAR_TEXEL:
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    break;
  case Texture::FarFiltering::LINEAR_TEXEL_NEAREST_MIPMAP:
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
    break;
  case Texture::FarFiltering::LINEAR_TEXEL_LINEAR_MIPMAP:
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    break;
  }

  switch (filtering.close_filter)
  {
  case Texture::CloseFiltering::NEAREST_TEXEL:
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    break;
  case Texture::CloseFiltering::LINEAR_TEXEL:
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    break;
  }

  if (filtering.anisotropy <= 1.0f)
    return;

  /*
  #define GL_TEXTURE_MAX_ANISOTROPY_EXT     0x84FE
  #define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
  */

  GLfloat max_anisotropy;
  glGetFloatv(0x84FF, &max_anisotropy);
  glTexParameterf(GL_TEXTURE_2D_ARRAY, 0x84FE, glm::clamp(filtering.anisotropy, 1.0f, max_anisotropy));
  if(glGetError() != GL_NO_ERROR)
    throw std::runtime_error("Anisotropic filtering is maybe/probably not supported.");
}

//------------------------------------------------------------------------------
void Texture::generateMipMapsIfNeeded(GLenum target, Texture::Filtering filtering)
{
  if (
    filtering.far_filter != Texture::FarFiltering::NEAREST_TEXEL &&
    filtering.far_filter != Texture::FarFiltering::LINEAR_TEXEL
    )
    glGenerateMipmap(target);
}

//------------------------------------------------------------------------------
bool Texture::checkTextureUnits(GLenum first, GLenum last)
{
  GLint texture_image_units;

  glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &texture_image_units);

  for (GLenum i_unit = first; i_unit <= last; i_unit++)
  {
    if (i_unit < GL_TEXTURE0 || i_unit - GL_TEXTURE0 > static_cast<unsigned int>(texture_image_units - 1))
      return false;
  }

  return true;
}

//------------------------------------------------------------------------------
bool Texture::checkTextureUnit(GLenum texture_unit)
{
  GLint texture_image_units;

  glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &texture_image_units);

  if (texture_unit < GL_TEXTURE0 || texture_unit - GL_TEXTURE0 > static_cast<unsigned int>(texture_image_units - 1))
    return false;
  else
    return true;
}

//------------------------------------------------------------------------------
GLint Texture::getMaxTextureSize()
{
  GLint max_texture_size;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
  return max_texture_size;
}

//----------------------------------------------------------------------------
GLint Texture::getMaxTextureArrayLayers()
{
  GLint max_layers;
  glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &max_layers);
  return max_layers;
}

//----------------------------------------------------------------------------
void Texture::flipImageY(unsigned char *image, int texture_width, int texture_height, int pixel_size)
{
  assert(texture_width > 0 && texture_height > 0 && pixel_size > 0);
  assert(texture_width % 2 == 0 && texture_height % 2 == 0);

  std::unique_ptr<unsigned char[]> temp_line(new unsigned char[texture_width * pixel_size]);

  const auto line_size = texture_width * pixel_size;

  for (auto i = 0; i < texture_height / 2; i++)
  {
    std::memcpy(temp_line.get(), &image[i * line_size], line_size);
    std::memcpy(&image[i * line_size], &image[(texture_height - 1 - i) * line_size], line_size);
    std::memcpy(&image[(texture_height - 1 - i) * line_size], temp_line.get(), line_size);
  }

}

//----------------------------------------------------------------------------
void Texture::flipImageX(unsigned char *image, int texture_width, int texture_height, int pixel_size)
{
  assert(texture_width > 0 && texture_height > 0 && pixel_size > 0);
  assert(texture_width % 2 == 0 && texture_height % 2 == 0);

  for (auto i_h = 0; i_h < texture_height; i_h++)
    for (auto i_w = 0; i_w < texture_width / 2; i_w++)
    {
      const auto line = i_h * texture_width;
      swapPixels(
        &image[pixel_size * (line + i_w)],
        &image[pixel_size * (line + (texture_width - 1 - i_w))],
        pixel_size
        );
    }

}

//------------------------------------------------------------------------------
void Texture::swapPixels(unsigned char * first, unsigned char * second, int count)
{
  assert(count > 0);

  for (auto i = 0; i < count; i++)
  {
    auto tmp = *first;
    *first = *second;
    *second = tmp;
    ++first;
    ++second;
  }

}

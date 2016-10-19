#pragma once

#include <GL/gl3w.h>

class Texture
{
public:
    // Filtering when texture is far away
    enum class FarFiltering
    {
      NEAREST_TEXEL, NEAREST_TEXEL_NEAREST_MIPMAP, NEAREST_TEXEL_LINEAR_MIPMAP,
      LINEAR_TEXEL,  LINEAR_TEXEL_NEAREST_MIPMAP,  LINEAR_TEXEL_LINEAR_MIPMAP
    };

    // Filtering when texture is near
    enum class CloseFiltering { NEAREST_TEXEL, LINEAR_TEXEL };

    struct Filtering
    {
      Texture::FarFiltering far_filter;
      Texture::CloseFiltering close_filter;
      GLfloat anisotropy;
    };

protected:
    Texture() {}

    void setFiltering(GLenum target, Texture::Filtering filtering);
    void generateMipMapsIfNeeded(GLenum target, Texture::Filtering filtering);

    bool checkTextureUnits(GLenum first, GLenum last);
    bool checkTextureUnit(GLenum texture_unit);
    GLint getMaxTextureSize();
    GLint getMaxTextureArrayLayers();

    static void swapPixels(unsigned char* first, unsigned char* second, int count);
    static void flipImageY(unsigned char* image, int texture_width, int texture_height, int pixel_size);
    static void flipImageX(unsigned char* image, int texture_width, int texture_height, int pixel_size);

};
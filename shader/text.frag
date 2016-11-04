#version 330 core

in vec3 texture_coord;

uniform sampler2DArray font_texture_array;

out vec4 output_color;

void main()
{
  // TODO: feed in signed distance field textures
  
  vec4 color = texture(font_texture_array, texture_coord);

  if (color.a > 0.55f)
    color = vec4(0.0f, 0.0f, 0.0f, 1.0f);
  else if (color.a < 0.45f)
    color = vec4(1.0f, 1.0f, 1.0f, 0.6f);
  else
  {
    vec3 col = 1.0f - vec3((color.r - 0.45f) * 20.0f);
    color = vec4(col, 0.6f);
  }

  output_color = color;
}

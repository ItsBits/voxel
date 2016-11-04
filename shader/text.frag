#version 330 core

in vec3 texture_coord;

uniform sampler2DArray font_texture_array;

out vec4 output_color;

void main()
{
  vec4 color = texture(font_texture_array, texture_coord);

  if (color.a < 0.5f)
    color = vec4(0.9f, 0.9f, 0.9f, 0.85f);

  output_color = color;
}

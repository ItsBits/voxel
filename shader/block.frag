#version 330 core

in vec3 texture_coord;
flat in vec4 ao_colors;
in vec2 texture_offset;

uniform sampler2DArray block_texture_array;

uniform vec3 lighting;

out vec4 color;

float biLerp(float a, float b, float c, float d, float s, float t)
{
  float x = mix(a, b, t);
  float y = mix(c, d, t);
  return mix(x, y, s);
}

void main()
{
  float interpolated_shade = biLerp(ao_colors.x,ao_colors.y,ao_colors.z,ao_colors.w,texture_coord.s, texture_coord.t ); // texture_coord does not work correctly with non 1x1x1 size blocks

  // this could be done in vertex chader
  vec2 texture_pos = (vec2(texture_coord.x, texture_coord.y) + texture_offset) / 4.0f; // TODO: replace magic number

  vec4 color_t = texture(block_texture_array, vec3(texture_pos, texture_coord.z));

  color = vec4(vec3(color_t.r, color_t.g, color_t.b) * interpolated_shade * lighting, color_t.a);
}

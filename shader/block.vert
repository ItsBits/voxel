#version 330 core

layout(location = 0) in ivec3 Position;
layout(location = 1) in int Type;
layout(location = 2) in uvec4 AO_Color;
layout(location = 3) in uvec2 Texture_Offset;

uniform mat4 VP_matrix;
uniform vec3 offset;

out vec3 texture_coord;
flat out vec4 ao_colors;
out vec2 texture_offset;

uniform float light;

void main()
{
  gl_Position = VP_matrix * vec4(vec3(Position) + offset, 1.0f);

  ao_colors = AO_Color / 255.0f * light;
  
  uvec2 i_tex_rel = uvec2((gl_VertexID & 1) ^ ((gl_VertexID >> 1) & 1), (gl_VertexID >> 1) & 1);

  texture_offset = vec2(Texture_Offset) / 256.0f; // not / 255 because it must be multiple of number divided by 256 (if that makes sense)
  texture_coord = vec3(vec2(i_tex_rel), float(Type));

  // could be solved with centroid interpolation ?
  if (i_tex_rel.x == 1u) texture_coord.x -= 1.0f / 128.0f; // TODO: replace magic numbers
  else                   texture_coord.x += 1.0f / 128.0f;
  if (i_tex_rel.y == 1u) texture_coord.y -= 1.0f / 128.0f;
  else                   texture_coord.y += 1.0f / 128.0f;
}

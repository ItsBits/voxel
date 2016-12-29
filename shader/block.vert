#version 330 core

#define FLOAT_VERT

layout(location = 0) in ivec3 Position;
layout(location = 1) in int Type;
layout(location = 2) in uvec4 AO_Color;

uniform mat4 VP_matrix;

out vec3 texture_coord;
flat out vec4 ao_colors;

uniform float light;

void main()
{
  gl_Position = VP_matrix * vec4(Position, 1.0f);

  ao_colors = AO_Color / 255.0f * light;
  
  uvec2 i_tex_rel = uvec2((gl_VertexID & 1) ^ ((gl_VertexID >> 1) & 1), (gl_VertexID >> 1) & 1);
  texture_coord = vec3(vec2(i_tex_rel), float(Type));
}

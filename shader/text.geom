#version 330 core

layout (points) in;
layout (triangle_strip, max_vertices = 4) out;

uniform float ratio;
uniform float font_size;

out vec3 texture_coord;

in float letter[];

void main()
{
// TODO: pre-calculate
  float width = font_size / ratio;
  float height = -font_size;

  texture_coord = vec3(0.0f, 0.0f, letter[0]);
  gl_Position = gl_in[0].gl_Position + vec4(0.0f, height, 0.0f, 0.0f);
  EmitVertex();

  texture_coord = vec3(1.0f, 0.0f, letter[0]);
  gl_Position = gl_in[0].gl_Position + vec4(width, height, 0.0f, 0.0f);
  EmitVertex();

  texture_coord = vec3(0.0f, 1.0f, letter[0]);
  gl_Position = gl_in[0].gl_Position + vec4(0.0f, 0.0f, 0.0f, 0.0f);
  EmitVertex();

  texture_coord = vec3(1.0f, 1.0f, letter[0]);
  gl_Position = gl_in[0].gl_Position + vec4(width, 0.0f, 0.0f, 0.0f);
  EmitVertex();

  EndPrimitive();
}

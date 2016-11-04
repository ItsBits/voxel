#version 330 core

layout(location = 0) in uvec3 Data;

uniform float ratio;
uniform float font_size;

out float letter;

void main()
{
  letter = Data.z;

  vec2 position = vec2(float(Data.x) / ratio, Data.y) * font_size - 1.0f;
  position.y = -position.y;

  gl_Position = vec4(position, 0.0f, 1.0f);
}

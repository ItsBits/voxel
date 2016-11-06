#version 330 core

in vec3 texture_coord;

uniform sampler2DArray font_texture_array;

out vec4 output_color;

void main()
{
  float color = 1.0f - texture(font_texture_array, texture_coord).r; // because

  const float center = 0.52f;
  const float range = 0.05f;

  if (color > center + range / 2.0f)
    color = 1.0f;
  else if (color < center - range / 2.0f)
    color = 0.0f;
  else
  {
    color = color - (center - range / 2.0f);
    color = color * 1.0f / range;
  }

  float alpha = color < center ? 0.6f : 1.0f;
  output_color = vec4(vec3(1.0f - color), alpha);
}

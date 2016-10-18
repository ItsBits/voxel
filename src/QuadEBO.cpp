#include "QuadEBO.hpp"

#include <memory>
#include <cassert>
#include <iostream>

//==============================================================================
GLuint QuadEBO::s_EBO{ 0 };
GLsizeiptr QuadEBO::s_indices{ 0 };

//==============================================================================
void QuadEBO::bind()
{
  if (s_EBO == 0) glGenBuffers(1, &s_EBO);
  assert(s_EBO != 0 && "QuadEBO buffer was not generated.");
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_EBO);
}

//==============================================================================
void QuadEBO::resize(GLsizeiptr new_element_count)
{
  // resize
  if (new_element_count <= s_indices) return;
  // get next multiple of 6
  new_element_count = ((new_element_count + 5) / 6) * 6;
  // increase by a fixed multiple of 6 amount
  new_element_count += 2 * 6;

  //std::string{ "Resizing QuadVBO from " + std::to_string(s_indices) + " to " + std::to_string(new_element_count) + "\n" };
  std::cout << "Resizing QuadVBO from " + std::to_string(s_indices) + " to " + std::to_string(new_element_count) + "\n";

  std::unique_ptr<GLuint[]> indices{ std::make_unique<GLuint[]>(new_element_count) };

  // fill
  GLuint a = 0;
  for (std::size_t i = 0; i < static_cast<GLuint>(new_element_count); a += 4)
  {
    indices[i++] = a + 0;
    indices[i++] = a + 1;
    indices[i++] = a + 2;

    indices[i++] = a + 2;
    indices[i++] = a + 3;
    indices[i++] = a + 0;
  }

  // bind
  bind();

  // upload
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(GLuint) * new_element_count, indices.get(), GL_STATIC_DRAW);

  // save new indice count
  s_indices = new_element_count;
}

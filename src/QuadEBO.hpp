#pragma once

#include <GL/gl3w.h>

class QuadEBO
{
public:
    QuadEBO() = delete;
    static void bind();
    static void resize(GLsizeiptr new_element_count);
    static GLenum type() { return GL_UNSIGNED_INT; }
    static GLsizeiptr size() { return s_indices; }

private:
    static GLsizeiptr s_indices;
    static GLuint s_EBO;

};
#pragma once

#include <GL/gl3w.h>
#include <climits>
#include <string>

//==================================================================================================
class Text
{
public:
    Text();
    ~Text();

    void draw() const;
    void update(const std::string & text);

private:
    GLuint m_VAO{ 0 };
    GLuint m_VBO{ 0 };
    GLsizei m_length{ 0 };

    static constexpr auto MAX_LINES_PLUS_1 = UCHAR_MAX;
    static constexpr auto MAX_CHARS_PER_LINE_PLUS_1 = UCHAR_MAX;

};
#include "Text.hpp"

#include <cassert>
#include <vector>

#include "TinyAlgebra.hpp"

//==================================================================================================
Text::Text()
{
    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);

    assert(m_VAO != 0 && m_VBO != 0);

    glBindVertexArray(m_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);

    glBufferData(GL_ARRAY_BUFFER, MAX_LINES_PLUS_1 * MAX_CHARS_PER_LINE_PLUS_1 * sizeof(GLubyte) * 3, nullptr, GL_STATIC_DRAW);

    glVertexAttribIPointer(0, 3, GL_UNSIGNED_BYTE, sizeof(GLubyte) * 3, static_cast<GLvoid*>(0));
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
}

//==================================================================================================
Text::~Text()
{
    glDeleteVertexArrays(1, &m_VAO);
    glDeleteBuffers(1, &m_VBO);
}

//==================================================================================================
void Text::draw() const
{
    assert(m_VAO != 0);

    if (m_length != 0)
    {
        glBindVertexArray(m_VAO);
        glDrawArrays(GL_POINTS, 0, m_length);
        glBindVertexArray(0);
    }
}

//==================================================================================================
void Text::update(const std::string & text)
{
    std::vector<ucVec3> out_text;

    GLubyte pos{ 0 };
    GLubyte line{ 0 };

    for (const auto c : text)
    {
        // if new line
        if (c == '\n')
        {
            ++line;
            pos = 0;
        }
        // if other symbol
        else
        {
            out_text.push_back({ pos, line, static_cast<GLubyte>(c) });
            ++pos;
        }
        // word wrap
        if (pos == MAX_CHARS_PER_LINE_PLUS_1)
        {
            ++line;
            pos = 0;
        }
        // stop if all lines full
        if (line == MAX_LINES_PLUS_1)
            break;

    }

    // upload new text
    glBindVertexArray(m_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(out_text[0]) * out_text.size(), out_text.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);

    m_length = static_cast<GLsizei>(out_text.size());
}
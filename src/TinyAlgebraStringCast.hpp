#pragma once

#include "TinyAlgebra.hpp"
#include <string>
#include <sstream>

template<typename T, int S>
std::string toString(Vec<T, S> vector)
{
    std::ostringstream s_stream;

    s_stream << '(' << vector(0);

    for (int i = 1; i < S; ++i) s_stream << ',' << vector(i);

    s_stream << ')';

    return s_stream.str();
};
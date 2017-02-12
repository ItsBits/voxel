#pragma once

#include <climits>
#include <cstdint>
#include <cstring>
#include <cassert>

//==============================================================================
template<std::size_t N>
class BitMap
{
public:
    BitMap() { std::memset(m_bytes, 0, (N + 7) / 8); }
    BitMap(const BitMap &) = default;

    bool get(const std::size_t i) const
    {
        assert(i < N && "Out of bounds.");
        return (m_bytes[i / 8] >> (7 - i % 8)) & std::uint8_t{ 1 };
    }

    void set(const std::size_t i)
    {
        assert(i < N && "Out of bounds.");
        m_bytes[i / 8] |= std::uint8_t{ 1 } << (7 - i % 8);
    }

    void clear(const std::size_t i)
    {
        assert(i < N && "Out of bounds.");
        m_bytes[i / 8] &= ~(std::uint8_t{ 1 } << (7 - i % 8));
    }


private:
    std::uint8_t m_bytes[(N + 7) / 8];

};
#pragma once

#include <climits>
#include <cassert>
#include <sys/user.h>

// warning not thread safe

//==============================================================================
template<std::size_t SIZE>
class MemoryBlockUnit
{
    static_assert(SIZE > 0 && SIZE < INT_MAX, "Value must be in this range.");

public:
    std::size_t getAmountOfFreeSpace() const { return SIZE - m_end; }

    char * getBlock(const std::size_t max_needed_size)
    {
        assert(m_end + max_needed_size <= SIZE && "Not enough space in memory block.");
        return m_block + m_end;
    }

    void increaseUsedSpace(const std::size_t increase_by)
    {
        assert(m_end + increase_by <= SIZE && "Can not increase past maximum capacity.");
        m_end += increase_by;
    }

    constexpr static std::size_t recommendSize(const std::size_t approximate)
    {
        const auto size = (approximate + PAGE_SIZE - 1) / PAGE_SIZE * PAGE_SIZE;
        const auto subtract = sizeof(m_end) + sizeof(m_next);

        if
            (size < subtract) return PAGE_SIZE; // TODO: fix that. This is not what I wanted.
        else
            return size - subtract;
    }

    void updateNext(MemoryBlockUnit<SIZE> * const next)
    {
        m_next = next;
    }

    auto getNext() { return m_next; }

private:
    std::size_t m_end{ 0 };
    MemoryBlockUnit<SIZE> * m_next{ nullptr };
    char m_block[SIZE];

};
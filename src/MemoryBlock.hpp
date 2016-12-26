#pragma once

#include "MemoryBlockUnit.hpp"

// Warning: Calling member functions is not thread safe. Accessing stored data is thread safe (will not be moved)

// TODO: maybe replace the current head-tail structure (MemoryBlock-MemoryBlockUnit) by single class
//==============================================================================
template<int SIZE = MemoryBlockUnit<1337>::recommendSize(1024 * 1024 - PAGE_SIZE + 1)>
class MemoryBlock
{
public:
    MemoryBlock() {}

    void reset()
    {
        this->~MemoryBlock();
        new (this) MemoryBlock;
    }

    char * getBlock(const std::size_t size)
    {
#ifndef NDEBUG
        assert(m_pending_update == false && "Cant return new block as long as there is still a pending update.");
#endif
        if (m_first == nullptr)
            m_first = new MemoryBlockUnit<SIZE>{};

        const auto space = m_first->getAmountOfFreeSpace();

        if (size > space)
        {
            if (size > SIZE) throw 1;

            auto * tmp = new MemoryBlockUnit<SIZE>{};
            tmp->updateNext(m_first);
            m_first = tmp;
        }
#ifndef NDEBUG
        m_pending_update = true;
#endif
        return m_first->getBlock(size);
    }

    void increaseUsedSpace(const std::size_t increase_by)
    {
#ifndef NDEBUG
        assert(m_pending_update == true && "Can not finish update if there is a no pending update.");
#endif
        m_first->increaseUsedSpace(increase_by);

#ifndef NDEBUG
        m_pending_update = false;
#endif
    }

    ~MemoryBlock()
    {
        // TODO: verify that this cleans up all the memory
        auto * current = m_first;

        while (current != nullptr)
        {
            auto * tmp = m_first->getNext();
            delete current;
            current = tmp;
        }
    }

private:
    MemoryBlockUnit<SIZE> * m_first{ nullptr };

#ifndef NDEBUG
    bool m_pending_update{ false };
#endif

};
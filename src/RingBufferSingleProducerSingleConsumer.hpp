#pragma once
#include <atomic>
#include <cassert>

//==============================================================================
// at most S-1 elements of type T can be stored at once
template<typename T, int S>
class RingBufferSingleProducerSingleConsumer
{
    static_assert(S > 1, "Ring buffer size must be bigger than 1.");
public:
    RingBufferSingleProducerSingleConsumer() {}

    // return pointer to where to store data
    // if nullptr is returned, no room for more data
    T * initPush();

    // reverts initPush();
    void discardPush();

    // will commit the data to queue
    void commitPush();

    // get next element
    // if nullptr is returned, buffer is empty
    T * initPop();

    // reverts initPop();
    void discardPop();

    // pop element
    void commitPop();

private:
    T m_buffer[S]; // TODO: does this need to be an atomic pointer? (just formality)
    std::atomic_int m_front{ 0 };
    std::atomic_int m_end{ 0 };

#ifndef NDEBUG
    std::atomic_bool m_push_pending{ false };
    std::atomic_bool m_pop_pending{ false };
#endif

};

//==============================================================================
template<typename T, int S>
T * RingBufferSingleProducerSingleConsumer<T, S>::initPush()
{
#ifndef NDEBUG
    assert(!m_push_pending && "Previous push not yet committed.");
#endif

    const auto end = m_end.load();
    const auto next_end = (m_end.load() + 1) % S;
    const auto front = m_front.load();

    if (next_end == front)
        return nullptr;
    else
    {
#ifndef NDEBUG
        m_push_pending = true;
#endif
        return m_buffer + end;
    }
}

//==============================================================================
template<typename T, int S>
void RingBufferSingleProducerSingleConsumer<T, S>::discardPush()
{
#ifndef NDEBUG
    assert(m_push_pending && "No push to discard.");
    m_push_pending = false;
#endif
};

//==============================================================================
template<typename T, int S>
void RingBufferSingleProducerSingleConsumer<T, S>::commitPush()
{
#ifndef NDEBUG
    assert(m_push_pending && "Nothing to commit.");
    m_push_pending = false;
#endif

    const auto new_end = (m_end.load() + 1) % S;
    m_end.store(new_end);
}

//==============================================================================
template<typename T, int S>
T * RingBufferSingleProducerSingleConsumer<T, S>::initPop()
{
#ifndef NDEBUG
    assert(!m_pop_pending && "Previous pop not committed.");
#endif

    const auto front = m_front.load();
    const auto end = m_end.load();

    if (front == end)
        return nullptr;
    else
    {
#ifndef NDEBUG
        m_pop_pending = true;
#endif
        return m_buffer + front;
    }
}

//==============================================================================
template<typename T, int S>
void RingBufferSingleProducerSingleConsumer<T, S>::discardPop()
{
#ifndef NDEBUG
  assert(m_pop_pending && "No pop to discard.");
  m_pop_pending = false;
#endif
};

//==============================================================================
template<typename T, int S>
void RingBufferSingleProducerSingleConsumer<T, S>::commitPop()
{
#ifndef NDEBUG
    assert(m_pop_pending && "Nothing to commit.");
    m_pop_pending = false;
#endif

    const auto new_front = (m_front.load() + 1) % S;
    m_front.store(new_front);
}

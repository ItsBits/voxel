#pragma once

#include <atomic>

//==============================================================================
/*
 * this class might not look like it is thread safe at all, but trust me,
 * IT IS as long as only one thread is calling consume() and
 * only one thread is calling produce()
 *
 * produce() returns the index of data to be written to
 *           and submits previous index for consumption
 * consume() returns the index of the latest data for using.
 *           note that consume() can skip data produced
 *           if called slower than produce()
 *
 * NOTE: it is assumed that the data with index 0 and 1 is
 *       ready for consumption
 */
class TripleBuffer
{
  // TODO: verify correctness

public:
  unsigned int consume()
  {
    if (m_back.load() & UPDATE_BIT)
      m_cons = m_back.exchange(m_cons) & CLEAR_BITS;

    return m_cons;
  }

  unsigned int produce()
  {
    m_prod = m_back.exchange(m_prod & UPDATE_BIT) & CLEAR_BITS;

    return m_prod;
  }

private:
  unsigned int     m_cons{ 0 };
  unsigned int     m_prod{ 1 };
  std::atomic_uint m_back{ 2 };

  static constexpr unsigned int UPDATE_BIT{ 0b100 };
  static constexpr unsigned int CLEAR_BITS{ 0b011 };

};

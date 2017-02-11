#pragma once

#include <cassert>
#include <type_traits>

// minimal implementation
#if 0
namespace m
{
  template<typename T, typename I, I N>
  class SparseMap
  {
  public:
    SparseMap() : m_end{ 0 } {}

    void add_entry(const I position, const T & data)
    {
      m_map[position] = m_end;
      m_vec[m_end] = data;
      m_i_vec[m_end++] = position;
    }

    void delete_entry(const I position)
    {
      m_vec[m_map[position]] = m_vec[--m_end];
      m_i_vec[m_map[position]] = m_i_vec[m_end];
      m_map[m_i_vec[m_end]] = m_map[position];
    }

    T & get_entry(const I position)
    { return m_vec[m_map[position]]; }

    T * begin() { return m_vec; }
    T * end() { return m_vec + m_end; }

  private:
    I m_map[N];
    I m_i_vec[N];
    T m_vec[N];
    I m_end;

  };
}
#endif


/*
 * TODO: verify if nothing has been broken during refactoring (delete_entry is tricky)
 */
  //============================================================================
  template<typename T, typename I, I N>
  class SparseMap
  {
    static_assert(std::is_integral<I>::value, "Iterator must be an integral.");
    static_assert(N > 0, "Size must be positive.");

  public:
    SparseMap() : m_end{ 0 }
    {
#ifndef NDEBUG
      for (auto & i : m_map) i = -1;
      for (auto & i : m_i_vec) i = -1;
#endif
      for (auto & i : m_vec) i = {};
    }

    void add_entry(const I position, const T & data)
    {
      assert(position >= 0 && position < N && "Out of bounds access.");
      assert(m_map[position] == -1 && "Adding element but it's already present in map.");
      assert(m_end < N && "Adding, but vector is full. Indicating internal error.");

      // add to map
      m_map[position] = m_end;
      // push back to vector
      m_vec[m_end] = data;
      m_i_vec[m_end++] = position;
    }

    void replace_entry(const I position, const T & data)
    {
      assert(position >= 0 && position < N && "Out of bounds access.");
      assert(m_map[position] != -1 && "Replacing nonexistent element.");
      assert(m_i_vec[m_map[position]] == position && "Data structure is broken.");

      m_vec[m_map[position]] = data;
    }

    void delete_entry(const I position)
    {
      assert(position >= 0 && position < N && "Out of bounds access.");
      assert(m_map[position] != -1 && "Deleting element but it's not present.");
      assert(m_map[position] >= 0 && m_map[position] < N && "Deleting element but data structure is broken.");
      assert(m_i_vec[m_map[position]] == position && "Deleting element but data structure is broken.");
      assert(m_end > 0 && "Removing from empty vector. Indicating internal error.");
      assert(m_end <= N && "Stored vector size too big. Internal error.");

      // swap with last and pop from vector
      m_vec[m_map[position]] = m_vec[--m_end];
      m_i_vec[m_map[position]] = m_i_vec[m_end];
      // fix position of moved element
      m_map[m_i_vec[m_end]] = m_map[position];

#ifndef NDEBUG
      m_map[position] = -1;
      m_i_vec[m_end] = -1;
#endif
    }

    void reset()
    {
      this->~SparseMap();
      new (this) SparseMap();
    }

    const T * begin() const { return m_vec; }

    const T * end() const { return m_vec + m_end; }

    const T & get_entry(const I position) const
    {
      assert(position >= 0 && position < N && "Out of bounds access.");
      assert(m_map[position] != -1 && "Accessing non existing element.");
      assert(m_i_vec[m_map[position]] == position && "Data structure is broken.");

      return m_vec[m_map[position]];
    }

  private:
    I m_map[N];
    I m_i_vec[N];
    T m_vec[N];
    I m_end;

  };

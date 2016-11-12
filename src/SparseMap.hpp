#pragma once

#include <cassert>

/*
 * Data structure with O(1) insert, O(1) remove, and cache friendly iteration
 */

//==============================================================================
template<typename T, int S>
class SparseMap
{
public:
    SparseMap();
    void add(int position, const T & data);
    void replace(int position, const T & data);
    void del(int position);
    void reset() { new (this) SparseMap(); }

    struct Node { int node_index; T data; };

    const Node * begin() const { return m_vec; }
    const Node * end() const { return m_vec + m_end; }
    const Node * get(int position) const
    {
      assert(position >= 0 && position < SIZE && "Out of bounds access.");
      assert(m_map[position] != -1 && "Accessing non existing element.");
      assert(m_vec[m_map[position]].node_index == position && "Data structure is broken.");

      return m_vec + m_map[position];
    }

private:
    static constexpr int SIZE{ S };

    // TODO: idea: move node_index out of m_vec in to separate vector (mirroring m_vec) so that the m_vec array is only data and no class internal stuff
    Node m_vec[SIZE];
    int m_map[SIZE];
    int m_end;

};

//==============================================================================
template<typename T, int S>
SparseMap<T, S>::SparseMap()
{
#ifndef NDEBUG
  for (auto & i : m_map) i = -1;
  for (auto & i : m_vec) i.node_index = -1;
#endif
  m_end = 0;
}

//==============================================================================
template<typename T, int S>
void SparseMap<T, S>::add(int position, const T & data)
{
  assert(position >= 0 && position < SIZE && "Out of bounds access.");
  assert(m_map[position] == -1 && "Adding element but it's already present in map.");
  assert(m_end < SIZE && "Adding, but vector is full. Indicating internal error.");

  // add to map
  m_map[position] = m_end;
  // push back to vector
  m_vec[m_end].data = data;
  m_vec[m_end++].node_index = position;
}

//==============================================================================
template<typename T, int S>
void SparseMap<T, S>::replace(int position, const T & data)
{
  assert(position >= 0 && position < SIZE && "Out of bounds access.");
  assert(m_map[position] != -1 && "Replacing nonexistent element.");
  assert(m_vec[m_map[position]].node_index == position && "Data structure is broken.");

  m_vec[m_map[position]].data = data;
}

//==============================================================================
template<typename T, int S>
void SparseMap<T, S>::del(int position)
{
  assert(position >= 0 && position < SIZE && "Out of bounds access.");
  assert(m_map[position] != -1 && "Deleting element but it's not present.");
  assert(m_map[position] >= 0 && m_map[position] < SIZE && "Deleting element but data structure is broken.");
  assert(m_vec[m_map[position]].node_index == position && "Deleting element but data structure is broken.");
  assert(m_end > 0 && "Removing from empty vector. Indicating internal error.");
  assert(m_end <= SIZE && "Stored vector size too big. Internal error.");

  // swap with last and pop from vector
  m_vec[m_map[position]] = m_vec[--m_end];
  // fix position of moved element
  m_map[m_vec[m_end].node_index] = m_map[position];

#ifndef NDEBUG
  m_map[position] = -1;
  m_vec[m_end].node_index = -1;
#endif
}

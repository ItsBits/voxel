#pragma once

#include <type_traits>
#include "Algebra.hpp"

//==============================================================================
template<typename T, typename I, I ... D>
class ModTable
{
  static_assert(std::is_integral<I>::value, "Iterator must be an integral.");
  static_assert(sizeof...(D) > 0, "Table can not have 0 dimensions.");
  static_assert(
    all_are(bigger_zero{}, D ...),
    "Dimension sizes must be bigger than 0."
  );

public:
//  ModTable() { for (auto & t : m_table) t = {}; }
  ModTable() {}

  ModTable(const ModTable &  other)
  { for (std::size_t i = 0; i < SIZE; ++i) m_table[i] = other.m_table[i]; }
  ModTable(const ModTable && other)
  { for (std::size_t i = 0; i < SIZE; ++i) m_table[i] = other.m_table[i]; }
  ModTable & operator = (const ModTable & other)
  {
    for (std::size_t i = 0; i < SIZE; ++i)
      m_table[i] = other.m_table[i];
    return *this;
  }
  ModTable & operator = (const ModTable && other)
  {
    for (std::size_t i = 0; i < SIZE; ++i)
      m_table[i] = other.m_table[i];
    return *this;
  }

  ~ModTable() {}

  const T & operator [] (
    Vec<I, sizeof...(D)> position) const {
    return m_table[position_to_index(position, { D ... })];
  }

  T & operator [] (Vec<I, sizeof...(D)> position) {
    return m_table[position_to_index(position, { D ... })];
  }

  T * begin() { return m_table; }
  const T * begin() const { return m_table; }
  T * end() { return m_table + SIZE; }
  const T * end() const { return m_table + SIZE; }

private:
  static constexpr I SIZE{ accumulate(multiply{}, D...) };

  T m_table[SIZE];

};
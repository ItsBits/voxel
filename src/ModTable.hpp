#pragma once

#include <type_traits>
#include "TinyAlgebra.hpp"

// TODO: alternative: get a Vec<T, S> as input instead of raw dimension sizes
//==============================================================================
template<typename T, typename I, I ... D>
class ModTable
{
    static_assert(std::is_integral<I>::value, "Iterator must be an integral.");
    static_assert(sizeof...(D) > 0, "Table can not have 0 dimensions.");
    static_assert(largerZero(Vec<I, sizeof...(D)>{ D ... }), "Each dimension size must be bigger than 0.");

public:
    const T & operator[](Vec<I, sizeof...(D)> position) const;
    T & operator[](Vec<I, sizeof...(D)> position);
    T * begin() { return m_table; }
    T * end() { return m_table + product(DIMENSIONS); }

private:
    static constexpr Vec<I, sizeof...(D)> DIMENSIONS{ D ... };

    T m_table[product(DIMENSIONS)];

};

//==============================================================================
template<typename T, typename I, I ... D>
constexpr Vec<I, sizeof...(D)> ModTable<T, I, D ...>::DIMENSIONS;

//==============================================================================
template<typename T, typename I, I ... D>
const T & ModTable<T, I, D ...>::operator[](Vec<I, sizeof...(D)> position) const
{
    const auto position_relative{ floorMod(position, DIMENSIONS) };
    const auto position_index{ toIndex(position_relative, DIMENSIONS) };

    return m_table[position_index];
}
//==============================================================================
template<typename T, typename I, I ... D>
T & ModTable<T, I, D ...>::operator[](Vec<I, sizeof...(D)> position)
{
    auto & mut_this = const_cast<const ModTable<T, I, D ...> &>(*this);

    return const_cast<T &>(mut_this.operator[](position));
}

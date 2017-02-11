#pragma once

#include <cstddef>
#include <algorithm>
#include <cassert>
#include <string>

//======================================================================================================================
template<typename T, std::size_t N>
class Vec
{
  static_assert(N > 0, "Can't have zero size vector.");

public:
  constexpr const T & operator [] (const std::size_t i) const { assert(i < N); return v[i]; }
  constexpr       T & operator [] (const std::size_t i)       { assert(i < N); return v[i]; }

#if 0 // making class trivial because constexpr. can not have both :(
  Vec() { for (std::size_t i = 0; i < N; ++i) v[i] = T{ 0 }; }
  Vec(const T value) { for (std::size_t i = 0; i < N; ++i) v[i] = value; }

  template<typename ... V>
  constexpr Vec(const V ... values)
  {
    static_assert(sizeof ... (values) == N, "Mismatched parameter count.");
    insert(0, values ...);
  }

  Vec(const Vec & other)  { for (std::size_t i = 0; i < N; ++i) v[i] = other[i]; }
  Vec(const Vec && other) { for (std::size_t i = 0; i < N; ++i) v[i] = other[i]; }
  Vec & operator = (const Vec &  other) { for (std::size_t i = 0; i < N; ++i) v[i] = other[i]; return *this; }
  Vec & operator = (const Vec && other) { for (std::size_t i = 0; i < N; ++i) v[i] = other[i]; return *this; }
  ~Vec() = default;
#endif
  T v[N]; // public because of trivial initialization

  const T * begin() const { return v; }
        T * begin()       { return v; }

  const T * end() const { return v + N; }
        T * end()       { return v + N; }

private:
  template<typename ... V>
  void insert(const std::size_t i, const T value, const V ... values)
  {
    v[i] = value;
    insert(i + 1, values ...);
  }

  void insert(const std::size_t i, const T value)
  {
    v[i] = value;
  }

};

//======================================================================================================================
template<typename T, std::size_t N>
T to_index(const Vec<T, N> & positions, const Vec<T, N> & dimensions)
{
  T result = T{ 0 };

  for (std::size_t p = 0; p < N; ++p)
  {
    T dimension = positions[p];

    for (std::size_t d = 0; d < p; ++d)
      dimension *= dimensions[d];

    result += dimension;
  }

  return result;
}

//======================================================================================================================
template<typename T, std::size_t N>
Vec<T, N> floor_mod(const Vec<T, N> & x, const Vec<T, N> & y)
{
  static_assert(std::is_integral<T>::value, "This function is designed for integers.");

  Vec<T, N> result;

  for (std::size_t i = 0; i < N; ++i)
    result[i] = (x[i] % y[i] + y[i]) % y[i];

  return result;
}

//======================================================================================================================
template<typename T, std::size_t N>
Vec<T, N> floor_div(const Vec<T, N> & x, const Vec<T, N> & y)
{
  static_assert(std::is_integral<T>::value, "This function is designed for integers.");

  Vec<T, N> result;

  for (std::size_t i = 0; i < N; ++i)
    result[i] = (x[i] + (x[i] < 0)) / y[i] - (x[i] < 0);

  return result;
}

//======================================================================================================================
template<typename T, std::size_t N>
T position_to_index(const Vec<T, N> & position, const Vec<T, N> & dimensions)
{
  return to_index(floor_mod(position, dimensions), dimensions);
}

//======================================================================================================================
template<typename T, std::size_t N>
Vec<T, N> min(const Vec<T, N> & x, const Vec<T, N> & y)
{
  Vec<T, N> result;

  for (std::size_t i = 0; i < N; ++i)
    result[i] = std::min(x[i], y[i]);

  return result;
}

//======================================================================================================================
template<typename T, std::size_t N>
Vec<T, N> max(const Vec<T, N> & x, const Vec<T, N> & y)
{
  Vec<T, N> result;

  for (std::size_t i = 0; i < N; ++i)
    result[i] = std::max(x[i], y[i]);

  return result;
}

//======================================================================================================================
template<typename T, std::size_t N>
T min(const Vec<T, N> & x)
{
  T result = x[0];

  for (std::size_t i = 1; i < N; ++i)
    result = std::min(result, x[i]);

  return result;
}

//======================================================================================================================
template<typename T, std::size_t N>
T max(const Vec<T, N> & x)
{
  T result = x[0];

  for (std::size_t i = 1; i < N; ++i)
    result = std::max(result, x[i]);

  return result;
}

//======================================================================================================================
template<typename T, std::size_t N>
Vec<T, N> abs(const Vec<T, N> & x)
{
  Vec<T, N> result;

  for (std::size_t i = 0; i < N; ++i)
    result[i] = std::abs(x[i]);

  return result;
}

//======================================================================================================================
template<typename T, std::size_t N>
std::string to_string(const Vec<T, N> & x)
{
  std::string result{ '(' };

  for (std::size_t i = 0; i < N - 1; ++i)
    result += std::to_string(x[i]) + ", ";

  result += std::to_string(x[N - 1]) + ')';

  return result;
}

//======================================================================================================================
template<std::size_t N>
bool any(const Vec<bool, N> & x)
{
  bool result = false;

  for (std::size_t i = 0; i < N; ++i)
    result = result || x[i];

  return result;
}

//======================================================================================================================
template<std::size_t N>
bool all(const Vec<bool, N> & x)
{
  bool result = true;

  for (std::size_t i = 0; i < N; ++i)
    result = result && x[i];

  return result;
}

//======================================================================================================================
template<typename T, std::size_t N>
T dot(const Vec<T, N> & x, const Vec<T, N> & y)
{
  T result = T{ 0 };

  for (std::size_t i = 0; i < N; ++i)
    result += x[i] * y[i];

  return result;
}

//======================================================================================================================
template<typename T, std::size_t N>
T product(const Vec<T, N> & x)
{
  T result = T{ 1 };

  for (std::size_t i = 0; i < N; ++i)
    result *= x[i];

  return result;
}

//======================================================================================================================
template<typename T, std::size_t N>
T sum(const Vec<T, N> & x)
{
  T result = T{ 0 };

  for (std::size_t i = 0; i < N; ++i)
    result += x[i];

  return result;
}

//======================================================================================================================
#define d(o)                                                    \
template<typename T, std::size_t N>                             \
Vec<T, N> operator o (const Vec<T, N> & x, const Vec<T, N> & y) \
{                                                               \
  Vec<T, N> result;                                             \
  for (std::size_t i = 0; i < N; ++i) result[i] = x[i] o y[i];  \
  return result;                                                \
}                                                               \
template<typename T, std::size_t N>                             \
Vec<T, N> operator o (const Vec<T, N> & x, const T s)           \
{                                                               \
  Vec<T, N> result;                                             \
  for (std::size_t i = 0; i < N; ++i) result[i] = x[i] o s;     \
  return result;                                                \
}                                                               \
template<typename T, std::size_t N>                             \
Vec<T, N> operator o (const T s, const Vec<T, N> & x)           \
{                                                               \
  Vec<T, N> result;                                             \
  for (std::size_t i = 0; i < N; ++i) result[i] = s o x[i];     \
  return result;                                                \
}
d(+) d(-) d(*) d(/) d(%) d(<<) d(>>) d(|) d(&) d(^) d(&&) d(||)
#undef d

//======================================================================================================================
#define d(o)                                                     \
template<typename T, std::size_t N>                              \
Vec<T, N> & operator o ## = (Vec<T, N> & x, const Vec<T, N> & y) \
{                                                                \
  for (std::size_t i = 0; i < N; ++i) x[i] o ## = y[i];          \
  return x;                                                      \
}                                                                \
template<typename T, std::size_t N>                              \
Vec<T, N> & operator o ## = (const Vec<T, N> & x, const T s)     \
{                                                                \
  for (std::size_t i = 0; i < N; ++i) x[i] o ## = s;             \
  return x;                                                      \
}
d(+) d(-) d(*) d(/) d(%) d(<<) d(>>) d(|) d(&) d(^)
#undef d

//======================================================================================================================
#define d(o)                                                       \
template<typename T, std::size_t N>                                \
Vec<bool, N> operator o (const Vec<T, N> & x, const Vec<T, N> & y) \
{                                                                  \
  Vec<bool, N> result;                                             \
  for (std::size_t i = 0; i < N; ++i) result[i] = x[i] o y[i];     \
  return result;                                                   \
}
d(==) d(!=) d(<) d(>) d(>=) d(<=)
#undef d

//======================================================================================================================
#define v(n, t, s) using n ## Vec ## s = Vec<t, s>;
#define w(nn, tt) v(nn, tt, 2) v(nn, tt, 3) v(nn, tt, 4)
w(i8, int8_t) w(i16, int16_t) w(i32, int32_t) w(i64, int64_t)
w(u8, uint8_t) w(u16, uint16_t) w(u32, uint32_t) w(u64, uint64_t)
w(b, bool) w(f32, float) w(f64, double)
#undef v
#undef w

//======================================================================================================================
//======================================================================================================================
// fun (?) challenge: make all functions from above constexpr. will the compiler in debug mode generate fast code?
//======================================================================================================================
//======================================================================================================================
template<typename T, std::size_t N>
constexpr T product_constexpr(const Vec<T, N> x, const std::size_t i = 0)
{
  return (i < N) ? x[i] * product_constexpr(x, i + 1) : T{ 1 };
}

//======================================================================================================================
template<typename T>
constexpr T ceil_int_div(const T x, const T y)
{
  static_assert(std::is_integral<T>::value, "This function is designed for integers.");
  return (x + y - 1) / y;
}

//======================================================================================================================
template<typename T, std::size_t N>
constexpr bool larger_zero(const Vec<T, N> x, const std::size_t i = 0)
{
  return (i < N) ? (x[i] > T{ 0 } ? larger_zero(x, i + 1) : false) : true;
}

//======================================================================================================================
template<typename T, std::size_t N>
Vec<int, N> int_floor(const Vec<T, N> x)
{
  Vec<int, N> r;

  for (std::size_t i = 0; i < N; ++i)
    r[i] = static_cast<int>(x[i]) - (x[i] < T{ 0 });

  return r;
}

//============================================================================
// accumulates arguments with operator operation,
// evaluating from right to left
template<typename O, typename T, typename ... V>
constexpr T accumulate(const O operation, const T value)
{ return value; }

template<typename O, typename T, typename ... V>
constexpr T accumulate(const O operation, const T value, const V...values)
{ return operation(value, accumulate(operation, values ...)); }

struct add
{
  constexpr auto operator () (const auto x, const auto y) const
  { return x + y; }
};

struct multiply
{
  constexpr auto operator () (const auto x, const auto y) const
  { return x * y; }
};

//============================================================================
template<typename O, typename T, typename ... V>
constexpr bool all_are(const O operation, const T value)
{ return operation(value); }

template<typename O, typename T, typename ... V>
constexpr bool all_are(const O operation, const T value, const V ... values)
{ return operation(value) && all_are(operation, values ...); }

template<typename O, typename T, typename ... V>
constexpr bool any_is(const O operation, const T value)
{ return operation(value); }

template<typename O, typename T, typename ... V>
constexpr bool any_is(const O operation, const T value, const V ... values)
{ return operation(value) || all_are(operation, values ...); }

struct bigger_zero
{
  constexpr auto operator ()(const auto x) const
  { return x > decltype(x){ 0 }; }
};
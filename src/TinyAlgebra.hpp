#pragma once

//==============================================================================
template<typename T, int S>
class Vec
{
public:
    static_assert(S > 0, "Vector size must be at least 1.");

    T v[S];

    const T & operator () (int i) const { return v[i]; }
    T & operator () (int i) { return v[i]; }
};

//==============================================================================
template<typename T, int W, int H>
class Mat
{
public:
    static_assert(W > 0 && H > 0, "Matrix size must be at least 1x1.");

    T m[W * H];

    const T & operator () (int i) const { return m[i]; }
    T & operator () (int i) { return m[i]; }
    const T & operator () (int w, int h) const { return m[h * W + w]; }
    T & operator () (int w, int h) { return m[h * W + w]; }
};

//==============================================================================
// vector3 = vector1 o vector2 (elementwise)
// vector2 = vector1 o scalar
// vector2 = scalar o vector1
// matrix3 = matrix1 o matrix2 (elementwise)
// matrix2 = matrix1 o scalar
// matrix2 = scalar o matrix1
#define d(o) \
template<typename T, int S> Vec<T, S> operator o (const Vec<T, S> & a, const Vec<T, S> & b) { Vec<T, S> r; for (int i = 0; i < S; ++i) r(i) = a(i) o b(i); return r; } \
template<typename T, int S> Vec<T, S> operator o (const Vec<T, S> & a, const T & c) { Vec<T, S> r; for (int i = 0; i < S; ++i) r(i) = a(i) o c; return r; } \
template<typename T, int S> Vec<T, S> operator o (const T & c, const Vec<T, S> & a) { Vec<T, S> r; for (int i = 0; i < S; ++i) r(i) = c o a(i); return r; } \
template<typename T, int W, int H> Mat<T, W, H> operator o (const Mat<T, W, H> & a, const Mat<T, W, H> & b) { Mat<T, W, H> r; for (int i = 0; i < W * H; ++i) r(i) = a(i) o b(i); return r; } \
template<typename T, int W, int H> Mat<T, W, H> operator o (const Mat<T, W, H> & a, const T & c) { Mat<T, W, H> r; for (int i = 0; i < W * H; ++i) r(i) = a(i) o c; return r; } \
template<typename T, int W, int H> Mat<T, W, H> operator o (const T & c, const Mat<T, W, H> & a) { Mat<T, W, H> r; for (int i = 0; i < W * H; ++i) r(i) = c o a(i); return r; }
d(+) d(-) d(*) d(/ ) d(%) d(<< ) d(>> ) d(| ) d(&) d(^) d(&&) d(|| )
#undef d

//==============================================================================
// vector2 o= vector1 (elementwise)
// vector1 o= scalar
// matrix2 o= matrix1 (elementwise)
// matrix1 o= scalar
#define d(o) \
template<typename T, int S> Vec<T, S> & operator o##= (Vec<T, S> & a, const Vec<T, S> & b) { for (int i = 0; i < S; ++i) a(i) o##= b(i); return a; } \
template<typename T, int S> Vec<T, S> & operator o## = (Vec<T, S> & a, const T & c) { for (int i = 0; i < S; ++i) a(i) o## = c; return a; } \
template<typename T, int W, int H> Mat<T, W, H> & operator o##= (Mat<T, W, H> & a, const Mat<T, W, H> & b) { for (int i = 0; i < W * H; ++i) a(i) o##= b(i); return a; } \
template<typename T, int W, int H> Mat<T, W, H> & operator o##= (Mat<T, W, H> & a, const T & c) { for (int i = 0; i < W * H; ++i) a(i) o##= c; return a; }
d(+) d(-) d(*) d(/ ) d(%) d(<< ) d(>> ) d(| ) d(&) d(^)
#undef d

//==============================================================================
// bool_vector = vector1 o vector2 (elementwise)
// bool_matrix = matrix1 o matrix2 (elementwise)
#define d(o) \
template<typename T, int S> Vec<bool, S> operator o (const Vec<T, S> & a, const Vec<T, S> & b) { Vec<bool, S> r; for (int i = 0; i < S; ++i) r(i) = a(i) o b(i); return r; } \
template<typename T, int W, int H> Mat<bool, W, H> operator o (const Mat<T, W, H> & a, const Mat<T, W, H> & b) { Mat<bool, W, H> r; for (int i = 0; i < W * H; ++i) r(i) = a(i) o b(i); return r; }
d(== ) d(!= ) d(<) d(>) d(>= ) d(<= )
#undef d

//==============================================================================
// true if ANY element is true
template<int S> bool any(const Vec<bool, S> & a) { bool r{ false }; for (int i = 0; i < S; ++i) r = r || a(i); return r; }
template<int W, int H> bool any(const Mat<bool, W, H> & a) { bool r{ false }; for (int i = 0; i < W * H; ++i) r = r || a(i); return r; }
// true if ALL elements are true
template<int S> bool all(const Vec<bool, S> & a) { bool r{ true }; for (int i = 0; i < S; ++i) r = r && a(i); return r; }
template<int S> constexpr bool allC(const Vec<bool, S> & a) { bool r{ true }; for (int i = 0; i < S; ++i) r = r && a(i); return r; }
template<int W, int H> bool all(const Mat<bool, W, H> & a) { bool r{ true }; for (int i = 0; i < W * H; ++i) r = r && a(i); return r; }

//==============================================================================
// return dot product of two vectors
template<typename T, int S> T dot(const Vec<T, S> & a, const Vec<T, S> & b)
{
    T r{ T{ 0 } };
    for (int i = 0; i < S; ++i) r += a(i) * b(i);
    return r;
}

//==============================================================================
// return cross product of two 3 dimensional vectors
template<typename T> T cross(const Vec<T, 3> & a, const Vec<T, 3> & b)
{
    Vec<T, 3> r;

    r(0) = a(1) * b(2) - a(2) * b(1);
    r(1) = a(2) * b(0) - a(0) * b(2);
    r(2) = a(0) * b(1) - a(1) * b(0);

    return r;
}

//==============================================================================
// return result of matrix-vector multiplication
template<typename T, int W, int H> Vec<T, H> mul(const Mat<T, W, H> & m, const Vec<T, W> & v)
{
    Vec<T, H> r;

    for (int i = 0; i < H; ++i) r(i) = T{ 0 };

    for (int h = 0; h < H; ++h)
        for (int w = 0; w < W; ++w)
            r(h) += m(w, h) * v(w);

    return r;
}

//==============================================================================
// return result of matrix-matrix multiplication
template<typename T, int W, int H1, int H2> Mat<T, H2, H1> mul(const Mat<T, W, H1> & a, const Mat<T, H2, W> & b)
{
    Mat<T, H2, H1> r;

    for (int i = 0; i < H1 * H2; ++i) r(i) = T{ 0 };

    for (int h1 = 0; h1 < H1; ++h1)
        for (int h2 = 0; h2 < H2; ++h2)
            for (int w = 0; w < W; ++w)
                r(h2, h1) += a(w, h1) * b(h2, w);

    return r;
}

//==============================================================================
template<int S>
Vec<int, S> floorDiv(Vec<int, S> x, Vec<int, S> y)
{
    Vec<int, S> result;

    for (int i = 0; i < S; ++i)
        result(i) = (x(i) + (x(i) < 0)) / y(i) - (x(i) < 0);

    return result;
}

//==============================================================================
template<int S>
Vec<int, S> floorMod(Vec<int, S> x, Vec<int, S> y)
{
    Vec<int, S> result;

    for (int i = 0; i < S; ++i)
        result(i) = (x(i) % y(i) + y(i)) % y(i);

    return result;
}

//==============================================================================
// elementwise min and max

// same can be defined for matrix
template<typename T, int S>
Vec<T, S> min(const Vec<T, S> & a, const Vec<T, S> & b)
{
    Vec<T, S> r;
    for (int i = 0; i < S; ++i) r(i) = a(i) < b(i) ? a(i) : b(i);
    return r;
}

template<typename T, int S>
Vec<T, S> max(const Vec<T, S> & a, const Vec<T, S> & b)
{
    Vec<T, S> r;
    for (int i = 0; i < S; ++i) r(i) = a(i) > b(i) ? a(i) : b(i);
    return r;
}

//==============================================================================
// elementwise absolute

// same can be defined for matrix
template<typename T, int S>
Vec<T, S> abs(const Vec<T, S> & a)
{
    Vec<T, S> r;
    for (int i = 0; i < S; ++i) r(i) = a(i) < T{ 0 } ? -a(i) : a(i);
    return r;
}

//==============================================================================
template<typename T> constexpr T minC(const T & a, const T & b) { return a < b ? a : b; }
template<typename T> constexpr T maxC(const T & a, const T & b) { return a < b ? b : a; }

//==============================================================================
template<typename T, int S>
int toIndex(const Vec<T, S> position, const Vec<T, S> dimensions)
{
  /*
   assert(all(position < dimensions));
   */
    int result = position(0);

    for (int i = 1; i < S; ++i)
    {
        int dim = position(i);

        for (int d = 0; d < i; ++d)
            dim *= dimensions(d);

        result += dim;
    }

    return result;
}

//==============================================================================
// absolute position to container index
template<typename T, int S>
int absoluteToIndex(const Vec<T, S> position, const Vec<T, S> dimensions)
{
    return toIndex(floorMod(position, dimensions), dimensions);
}

//==============================================================================
// generate names for common vectors and matrices
#define v(n, t, s) using n##Vec##s = Vec<t, s>; using n##Mat##s = Mat<t, s, s>;
#define w(nn, tt) v(nn, tt, 2) v(nn, tt, 3) v(nn, tt, 4)
w(c, char) w(s, short) w(i, int) w(b, bool) w(f, float) w(d, double)
w(uc, unsigned char) w(us, unsigned short) w(ui, unsigned int)
#undef v
#undef w

//==============================================================================
template<typename T, int S>
Vec<int, S> intFloor(const Vec<T, S> x)
{
    Vec<int, S> r;

    for (int i = 0; i < S; ++i) r(i) = static_cast<int>(x(i)) - (x(i) < T{ 0 });

    return r;
}

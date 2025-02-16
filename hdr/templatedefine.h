#ifndef TEMPLATEDEFINE_H
#define TEMPLATEDEFINE_H
#include <type_traits>
#include <limits>
#include <cmath>
namespace thu
{

template <typename T>
std::enable_if_t<not std::numeric_limits<T>::is_integer, bool>
equal_within_ulps(T x, T y, std::size_t n=10)
{
    const T m = std::min(std::fabs(x), std::fabs(y));
    const int exp = m < std::numeric_limits<T>::min() ? std::numeric_limits<T>::min_exponent - 1
                                                      : std::ilogb(m);
    return std::fabs(x - y) <= n * std::ldexp(std::numeric_limits<T>::epsilon(), exp);
}
}

#endif
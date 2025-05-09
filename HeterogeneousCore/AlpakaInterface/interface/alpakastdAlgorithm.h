#ifndef HeterogeneousCore_AlpakaInterface_interface_alpakastdAlgorithm_h
#define HeterogeneousCore_AlpakaInterface_interface_alpakastdAlgorithm_h

#include <algorithm>
#include <functional>
#include <utility>

#include <alpaka/alpaka.hpp>

// reimplementation of std algorithms able to work on device code

namespace alpaka_std {

  template <typename RandomIt, typename T, typename Compare = std::less<T>>
  ALPAKA_FN_HOST_ACC constexpr RandomIt lower_bound(RandomIt first, RandomIt last, const T &value, Compare comp = {}) {
    auto count = last - first;

    while (count > 0) {
      auto it = first;
      auto step = count / 2;
      it += step;
      if (comp(*it, value)) {
        first = ++it;
        count -= step + 1;
      } else {
        count = step;
      }
    }
    return first;
  }

  template <typename RandomIt, typename T, typename Compare = std::less<T>>
  ALPAKA_FN_HOST_ACC constexpr RandomIt upper_bound(RandomIt first, RandomIt last, const T &value, Compare comp = {}) {
    auto count = last - first;

    while (count > 0) {
      auto it = first;
      auto step = count / 2;
      it += step;
      if (!comp(value, *it)) {
        first = ++it;
        count -= step + 1;
      } else {
        count = step;
      }
    }
    return first;
  }

}  // namespace alpaka_std

#endif  // HeterogeneousCore_AlpakaInterface_interface_alpakastdAlgorithm_h

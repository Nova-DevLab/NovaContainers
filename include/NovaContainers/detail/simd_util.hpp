#ifndef NOVA_CONTAINERS_DETAIL_SIMD_UTIL_HPP_
#define NOVA_CONTAINERS_DETAIL_SIMD_UTIL_HPP_

#include <cstdint>
#include <cstddef>
#include <bit>

#if defined(__AVX2__) || (defined(_MSC_VER) && defined(__AVX2__))
#include <immintrin.h>
#define NOVA_HAS_AVX2 1
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#include <tmmintrin.h>
#define NOVA_HAS_SSE2 1
#endif

namespace nova::containers::detail {

// Control byte special values (MSB is set for special slots)
inline constexpr int8_t kEmptySlot = static_cast<int8_t>(0x80);     // -128
inline constexpr int8_t kDeletedSlot = static_cast<int8_t>(0xFE);   // -2

// BitMask helper wrapper
class BitMask {
 public:
  explicit BitMask(uint32_t mask) : mask_(mask) {}

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return mask_ != 0;
  }

  [[nodiscard]] constexpr uint32_t Mask() const noexcept {
    return mask_;
  }

  // Find lowest set bit index (trailing zeros)
  [[nodiscard]] constexpr uint32_t LowestSetBit() const noexcept {
    return static_cast<uint32_t>(std::countr_zero(mask_));
  }

  // Clear lowest set bit
  constexpr BitMask& ClearLowestSetBit() noexcept {
    mask_ &= (mask_ - 1);
    return *this;
  }

 private:
  uint32_t mask_;
};

// 16-byte SSE2 / AVX2 Group implementation
class GroupSse2 {
 public:
  static constexpr size_t kWidth = 16;

  explicit GroupSse2(const int8_t* pos) {
#if defined(NOVA_HAS_SSE2) || defined(NOVA_HAS_AVX2)
    ctrl_ = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pos));
#else
    for (size_t i = 0; i < kWidth; ++i) {
      ctrl_bytes_[i] = pos[i];
    }
#endif
  }

  [[nodiscard]] BitMask Match(int8_t h2) const noexcept {
#if defined(NOVA_HAS_SSE2) || defined(NOVA_HAS_AVX2)
    __m128i target = _mm_set1_epi8(static_cast<char>(h2));
    __m128i cmp = _mm_cmpeq_epi8(ctrl_, target);
    return BitMask(static_cast<uint32_t>(_mm_movemask_epi8(cmp)));
#else
    uint32_t mask = 0;
    for (size_t i = 0; i < kWidth; ++i) {
      if (ctrl_bytes_[i] == h2) {
        mask |= (1U << i);
      }
    }
    return BitMask(mask);
#endif
  }

  [[nodiscard]] BitMask MatchEmpty() const noexcept {
    return Match(kEmptySlot);
  }

  [[nodiscard]] BitMask MatchEmptyOrDeleted() const noexcept {
#if defined(NOVA_HAS_SSE2) || defined(NOVA_HAS_AVX2)
    return BitMask(static_cast<uint32_t>(_mm_movemask_epi8(ctrl_)));
#else
    uint32_t mask = 0;
    for (size_t i = 0; i < kWidth; ++i) {
      if (ctrl_bytes_[i] == kEmptySlot || ctrl_bytes_[i] == kDeletedSlot) {
        mask |= (1U << i);
      }
    }
    return BitMask(mask);
#endif
  }

 private:
#if defined(NOVA_HAS_SSE2) || defined(NOVA_HAS_AVX2)
  __m128i ctrl_;
#else
  int8_t ctrl_bytes_[kWidth];
#endif
};

using Group = GroupSse2;

}  // namespace nova::containers::detail

#endif  // NOVA_CONTAINERS_DETAIL_SIMD_UTIL_HPP_

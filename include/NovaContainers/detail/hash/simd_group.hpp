#ifndef NOVA_CONTAINERS_DETAIL_HASH_SIMD_GROUP_HPP_
#define NOVA_CONTAINERS_DETAIL_HASH_SIMD_GROUP_HPP_

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

// 특수 슬롯의 control byte 값은 최상위 비트를 설정한다.
inline constexpr int8_t k_empty_slot = static_cast<int8_t>(0x80);     // -128
inline constexpr int8_t k_deleted_slot = static_cast<int8_t>(0xFE);   // -2

// 비트 마스크의 조회와 하위 비트 제거를 제공한다.
class bit_mask {
 public:
  explicit bit_mask(uint32_t mask) : mask_(mask) {}

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return mask_ != 0;
  }

  [[nodiscard]] constexpr uint32_t mask() const noexcept {
    return mask_;
  }

  // 가장 낮은 설정 비트의 인덱스를 구한다.
  [[nodiscard]] constexpr uint32_t lowest_set_bit() const noexcept {
    return static_cast<uint32_t>(std::countr_zero(mask_));
  }

  // 가장 낮은 설정 비트를 지운다.
  constexpr bit_mask& clear_lowest_set_bit() noexcept {
    mask_ &= (mask_ - 1);
    return *this;
  }

 private:
  uint32_t mask_;
};

// SSE2 명령으로 16바이트 control group을 처리한다.
class group_sse2 {
 public:
  static constexpr size_t k_width = 16;

  explicit group_sse2(const int8_t* pos) {
#if defined(NOVA_HAS_SSE2) || defined(NOVA_HAS_AVX2)
    ctrl_ = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pos));
#else
    for (size_t i = 0; i < k_width; ++i) {
      ctrl_bytes_[i] = pos[i];
    }
#endif
  }

  [[nodiscard]] bit_mask match(int8_t h2) const noexcept {
#if defined(NOVA_HAS_SSE2) || defined(NOVA_HAS_AVX2)
    __m128i target = _mm_set1_epi8(static_cast<char>(h2));
    __m128i cmp = _mm_cmpeq_epi8(ctrl_, target);
    return bit_mask(static_cast<uint32_t>(_mm_movemask_epi8(cmp)));
#else
    uint32_t mask = 0;
    for (size_t i = 0; i < k_width; ++i) {
      if (ctrl_bytes_[i] == h2) {
        mask |= (1U << i);
      }
    }
    return bit_mask(mask);
#endif
  }

  [[nodiscard]] bit_mask match_empty() const noexcept {
    return match(k_empty_slot);
  }

  [[nodiscard]] bit_mask match_empty_or_deleted() const noexcept {
#if defined(NOVA_HAS_SSE2) || defined(NOVA_HAS_AVX2)
    return bit_mask(static_cast<uint32_t>(_mm_movemask_epi8(ctrl_)));
#else
    uint32_t mask = 0;
    for (size_t i = 0; i < k_width; ++i) {
      if (ctrl_bytes_[i] == k_empty_slot || ctrl_bytes_[i] == k_deleted_slot) {
        mask |= (1U << i);
      }
    }
    return bit_mask(mask);
#endif
  }

 private:
#if defined(NOVA_HAS_SSE2) || defined(NOVA_HAS_AVX2)
  __m128i ctrl_;
#else
  int8_t ctrl_bytes_[k_width];
#endif
};

using group_type = group_sse2;

}  // 네임스페이스 nova::containers::detail

#endif  // NOVA_CONTAINERS_DETAIL_HASH_SIMD_GROUP_HPP_

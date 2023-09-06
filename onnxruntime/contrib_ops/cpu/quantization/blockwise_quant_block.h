// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include <cstdint>

namespace onnxruntime {
namespace contrib {

#if defined(_MSC_VER)
#define FORCEINLINE __forceinline
#else
#define FORCEINLINE __attribute__((always_inline)) inline
#endif

template <typename T, int32_t block_size, int32_t bits>
struct BlockwiseQuantBlock {
  FORCEINLINE void dequant(T* dst, T scale, int32_t k_idx, int32_t K) const;
  FORCEINLINE void dequant(T* dst, T scale, uint8_t zp, int32_t k_idx, int32_t K) const;

  FORCEINLINE void quant(const T* src, T& scale, int32_t k_idx, int32_t K, int32_t N);
  FORCEINLINE void quant(const T* src, T& scale, uint8_t& zp, int32_t k_idx, int32_t K, int32_t N);
};

template <typename T, int32_t block_size>
struct BlockwiseQuantBlock<T, block_size, 3> {
  static_assert(block_size % 8 == 0);

  BlockwiseQuantBlock<T, block_size, 3>() : low_2bits(main_blob),
                                            high_1bit(main_blob + block_size / 4) {}

  uint8_t main_blob[block_size / 8 * 3];
  uint8_t* low_2bits;
  uint8_t* high_1bit;

  FORCEINLINE void dequant(T* dst, T scale, int32_t k_idx, int32_t K) const;
  FORCEINLINE void dequant(T* dst, T scale, uint8_t zp, int32_t k_idx, int32_t K) const;

  FORCEINLINE void quant(const T* src, T& scale, int32_t k_idx, int32_t K, int32_t N);
  FORCEINLINE void quant(const T* src, T& scale, uint8_t& zp, int32_t k_idx, int32_t K, int32_t N);
};

template <typename T, int32_t block_size>
struct BlockwiseQuantBlock<T, block_size, 4> {
  static_assert(block_size % 8 == 0);

  uint8_t main_blob[block_size / 2];

  FORCEINLINE void dequant(T* dst, T scale, uint8_t zp, int32_t k_idx, int32_t K) const {
    for (int i = 0; i < block_size; i += 2) {
      T zp_t = static_cast<T>(float(zp));
      if (k_idx + i < K) {
        T x0 = static_cast<T>(float(main_blob[i / 2] & 0xF));
        dst[i] = scale * (x0 - zp_t);
      }
      if (k_idx + i + 1 < K) {
        T x1 = static_cast<T>(float(main_blob[i / 2] >> 4));
        dst[i + 1] = scale * (x1 - zp_t);
      }
    }
  }

  FORCEINLINE void dequant(T* dst, T scale, int32_t k_idx, int32_t K) const {
    constexpr uint8_t zp = 8;
    dequant(dst, scale, zp, k_idx, K);
  }

  FORCEINLINE void quant(const T* src, T& scale_block, uint8_t& zp, int32_t k_idx, int32_t K, int32_t N) {
    float min = static_cast<float>(*src);
    float max = static_cast<float>(*src);
    int32_t klen = std::min(block_size, K - k_idx);
    for (int32_t kk = 0; kk < klen; kk++) {
      const float v = static_cast<float>(src[N * kk]);
      if (v < min) min = v;
      if (v > max) max = v;
    }
    min = std::min(min, 0.0f);
    max = std::max(max, 0.0f);

    const float scale = (max - min) / ((1 << 4) - 1);
    scale_block = static_cast<T>(scale);

    const float reciprocal_scale = scale ? 1.0f / scale : 0.0f;
    float zero_point_fp = min;
    if (scale != 0.0f) {
      zero_point_fp = 0.f - min / scale;
    }

    // Handle any clamping
    if (zero_point_fp < 0.0f) {
      zp = 0;
    } else if (zero_point_fp > 15.0f) {
      zp = 15;
    } else {
      zp = (uint8_t)roundf(zero_point_fp);
    }

    for (size_t kk = 0; kk < klen; kk += 2) {
      const float v0 = static_cast<float>(src[N * kk]);
      const uint8_t vi0 = (uint8_t)std::min(15.0f, std::max(0.0f, roundf(v0 * reciprocal_scale + zp)));

      const float v1 = static_cast<float>((kk + 1 < klen) ? src[N * (kk + 1)] : 0.f);
      const uint8_t vi1 = (uint8_t)std::min(15.0f, std::max(0.0f, roundf(v1 * reciprocal_scale + zp)));

      main_blob[kk / 2] = vi0 | (vi1 << 4);
    }
  }

  FORCEINLINE void quant(const T* src, T& scale_block, int32_t k_idx, int32_t K, int32_t N) {
    float amax = 0.0f;  // abs(max)
    float max = 0.0f;

    int32_t klen = std::min(block_size, K - k_idx);

    for (int32_t kk = 0; kk < klen; kk++) {
      const float v = static_cast<float>(src[N * kk]);
      if (amax < fabsf(v)) {
        amax = fabsf(v);
        max = v;
      }
    }

    const float scale = max / (-8.f);
    scale_block = static_cast<T>(scale);
    const float reciprocal_scale = scale ? 1.0f / scale : 0.0f;

    for (size_t kk = 0; kk < klen; kk += 2) {
      const float v0 = src[N * kk] * reciprocal_scale;
      const uint8_t vi0 = (uint8_t)std::min(15.0f, std::max(0.0f, v0 + 8.5f));

      const float v1 = (kk + 1 < klen) ? src[N * (kk + 1)] * reciprocal_scale : 0;
      const uint8_t vi1 = (uint8_t)std::min(15.0f, std::max(0.0f, v1 + 8.5f));

      main_blob[kk / 2] = vi0 | (vi1 << 4);
    }
  }
};

template <typename T, int32_t block_size>
struct BlockwiseQuantBlock<T, block_size, 5> {
  static_assert(block_size % 8 == 0);

  BlockwiseQuantBlock<T, block_size, 5>() : low_4bits(main_blob),
                                            high_1bit(main_blob + block_size / 2) {}

  uint8_t main_blob[block_size / 8 * 5];
  uint8_t* low_4bits;
  uint8_t* high_1bit;

  FORCEINLINE void dequant(T* dst, T scale, int32_t k_idx, int32_t K) const;
  FORCEINLINE void dequant(T* dst, T scale, uint8_t zp, int32_t k_idx, int32_t K) const;

  FORCEINLINE void quant(const T* src, T& scale, int32_t k_idx, int32_t K, int32_t N);
  FORCEINLINE void quant(const T* src, T& scale, uint8_t& zp, int32_t k_idx, int32_t K, int32_t N);
};

template <typename T, int32_t block_size>
struct BlockwiseQuantBlock<T, block_size, 6> {
  static_assert(block_size % 8 == 0);

  BlockwiseQuantBlock<T, block_size, 6>() : low_4bits(main_blob),
                                            high_2bits(main_blob + block_size / 2) {}

  uint8_t main_blob[block_size / 8 * 6];
  uint8_t* low_4bits;
  uint8_t* high_2bits;

  FORCEINLINE void dequant(T* dst, T scale, int32_t k_idx, int32_t K) const;
  FORCEINLINE void dequant(T* dst, T scale, uint8_t zp, int32_t k_idx, int32_t K) const;

  FORCEINLINE void quant(const T* src, T& scale, int32_t k_idx, int32_t K, int32_t N);
  FORCEINLINE void quant(const T* src, T& scale, uint8_t& zp, int32_t k_idx, int32_t K, int32_t N);
};

template <typename T, int32_t block_size>
struct BlockwiseQuantBlock<T, block_size, 7> {
  static_assert(block_size % 8 == 0);

  BlockwiseQuantBlock<T, block_size, 7>() : low_4bits(main_blob),
                                            middle_2bits(main_blob + block_size / 2),
                                            high_1bit(main_blob + block_size / 8 * 6) {}

  uint8_t main_blob[block_size / 8 * 7];
  uint8_t* low_4bits;
  uint8_t* middle_2bits;
  uint8_t* high_1bit;

  FORCEINLINE void dequant(T* dst, T scale, int32_t k_idx, int32_t K) const;
  FORCEINLINE void dequant(T* dst, T scale, uint8_t zp, int32_t k_idx, int32_t K) const;

  FORCEINLINE void quant(const T* src, T& scale, int32_t k_idx, int32_t K, int32_t N);
  FORCEINLINE void quant(const T* src, T& scale, uint8_t& zp, int32_t k_idx, int32_t K, int32_t N);
};

}  // namespace contrib
}  // namespace onnxruntime

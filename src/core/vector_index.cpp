// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

// SIMD headers for hardware-accelerated cosine similarity.
// These must be in the global module fragment (non-modular headers).
#if defined(__x86_64__) || defined(_M_X64)
  #if defined(__AVX2__)
    #include <immintrin.h>
    #define QC_USE_AVX2 1
  #elif defined(__SSE2__)
    #include <emmintrin.h>
    #define QC_USE_SSE2 1
  #endif
#elif defined(__aarch64__) || defined(_M_ARM64)
  #include <arm_neon.h>
  #define QC_USE_NEON 1
#endif

module quantclaw.core.vector_index;

import std;

namespace quantclaw {

void VectorIndex::Add(VectorEntry entry) {
  entries_.push_back(std::move(entry));
}

float VectorIndex::CosineSimilarity(const std::vector<float>& a,
                                    const std::vector<float>& b) {
  if (a.size() != b.size() || a.empty())
    return 0.0f;

  const std::size_t n = a.size();
  float dot = 0.0f, norm_a = 0.0f, norm_b = 0.0f;

#if QC_USE_AVX2
  // AVX2: process 8 floats per iteration
  std::size_t i = 0;
  __m256 v_dot = _mm256_setzero_ps();
  __m256 v_na = _mm256_setzero_ps();
  __m256 v_nb = _mm256_setzero_ps();
  for (; i + 8 <= n; i += 8) {
    __m256 va = _mm256_loadu_ps(&a[i]);
    __m256 vb = _mm256_loadu_ps(&b[i]);
    v_dot = _mm256_fmadd_ps(va, vb, v_dot);
    v_na = _mm256_fmadd_ps(va, va, v_na);
    v_nb = _mm256_fmadd_ps(vb, vb, v_nb);
  }
  // Horizontal sum of 8-wide vectors
  // dot
  __m128 lo = _mm256_castps256_ps128(v_dot);
  __m128 hi = _mm256_extractf128_ps(v_dot, 1);
  lo = _mm_add_ps(lo, hi);
  lo = _mm_hadd_ps(lo, lo);
  lo = _mm_hadd_ps(lo, lo);
  dot = _mm_cvtss_f32(lo);
  // norm_a
  lo = _mm256_castps256_ps128(v_na);
  hi = _mm256_extractf128_ps(v_na, 1);
  lo = _mm_add_ps(lo, hi);
  lo = _mm_hadd_ps(lo, lo);
  lo = _mm_hadd_ps(lo, lo);
  norm_a = _mm_cvtss_f32(lo);
  // norm_b
  lo = _mm256_castps256_ps128(v_nb);
  hi = _mm256_extractf128_ps(v_nb, 1);
  lo = _mm_add_ps(lo, hi);
  lo = _mm_hadd_ps(lo, lo);
  lo = _mm_hadd_ps(lo, lo);
  norm_b = _mm_cvtss_f32(lo);
  // Scalar tail
  for (; i < n; ++i) {
    dot += a[i] * b[i];
    norm_a += a[i] * a[i];
    norm_b += b[i] * b[i];
  }

#elif QC_USE_SSE2
  // SSE2: process 4 floats per iteration
  std::size_t i = 0;
  __m128 v_dot = _mm_setzero_ps();
  __m128 v_na = _mm_setzero_ps();
  __m128 v_nb = _mm_setzero_ps();
  for (; i + 4 <= n; i += 4) {
    __m128 va = _mm_loadu_ps(&a[i]);
    __m128 vb = _mm_loadu_ps(&b[i]);
    v_dot = _mm_add_ps(v_dot, _mm_mul_ps(va, vb));
    v_na = _mm_add_ps(v_na, _mm_mul_ps(va, va));
    v_nb = _mm_add_ps(v_nb, _mm_mul_ps(vb, vb));
  }
  // Horizontal sum of 4-wide vectors
  __m128 shuf;
  shuf = _mm_movehdup_ps(v_dot);
  v_dot = _mm_add_ps(v_dot, shuf);
  shuf = _mm_movehl_ps(shuf, v_dot);
  v_dot = _mm_add_ss(v_dot, shuf);
  dot = _mm_cvtss_f32(v_dot);

  shuf = _mm_movehdup_ps(v_na);
  v_na = _mm_add_ps(v_na, shuf);
  shuf = _mm_movehl_ps(shuf, v_na);
  v_na = _mm_add_ss(v_na, shuf);
  norm_a = _mm_cvtss_f32(v_na);

  shuf = _mm_movehdup_ps(v_nb);
  v_nb = _mm_add_ps(v_nb, shuf);
  shuf = _mm_movehl_ps(shuf, v_nb);
  v_nb = _mm_add_ss(v_nb, shuf);
  norm_b = _mm_cvtss_f32(v_nb);
  // Scalar tail
  for (; i < n; ++i) {
    dot += a[i] * b[i];
    norm_a += a[i] * a[i];
    norm_b += b[i] * b[i];
  }

#elif QC_USE_NEON
  // NEON (AArch64): process 4 floats per iteration
  std::size_t i = 0;
  float32x4_t v_dot = vdupq_n_f32(0.0f);
  float32x4_t v_na = vdupq_n_f32(0.0f);
  float32x4_t v_nb = vdupq_n_f32(0.0f);
  for (; i + 4 <= n; i += 4) {
    float32x4_t va = vld1q_f32(&a[i]);
    float32x4_t vb = vld1q_f32(&b[i]);
    v_dot = vfmaq_f32(v_dot, va, vb);
    v_na = vfmaq_f32(v_na, va, va);
    v_nb = vfmaq_f32(v_nb, vb, vb);
  }
  dot = vaddvq_f32(v_dot);
  norm_a = vaddvq_f32(v_na);
  norm_b = vaddvq_f32(v_nb);
  // Scalar tail
  for (; i < n; ++i) {
    dot += a[i] * b[i];
    norm_a += a[i] * a[i];
    norm_b += b[i] * b[i];
  }

#else
  // Scalar fallback
  for (std::size_t i = 0; i < n; ++i) {
    dot += a[i] * b[i];
    norm_a += a[i] * a[i];
    norm_b += b[i] * b[i];
  }
#endif

  float denom = std::sqrt(norm_a) * std::sqrt(norm_b);
  if (denom < 1e-10f)
    return 0.0f;
  return dot / denom;
}

std::vector<VectorSearchResult>
VectorIndex::Search(const std::vector<float>& query, int top_k) const {
  if (top_k <= 0)
    return {};

  std::vector<VectorSearchResult> results;
  results.reserve(entries_.size());

  for (const auto& entry : entries_) {
    // Skip entries with mismatched dimensions to avoid false 0.0 scores
    if (entry.embedding.size() != query.size())
      continue;
    float sim = CosineSimilarity(query, entry.embedding);
    results.push_back(
        {entry.id, entry.content, entry.source, entry.line_number, sim});
  }

  // Partial sort: only order the top_k elements, reducing O(N log N) to O(N log K)
  auto cmp = [](const VectorSearchResult& a, const VectorSearchResult& b) {
    return a.score > b.score;
  };
  int k = std::min(top_k, static_cast<int>(results.size()));
  std::partial_sort(results.begin(), results.begin() + k, results.end(), cmp);
  results.resize(k);
  return results;
}

}  // namespace quantclaw

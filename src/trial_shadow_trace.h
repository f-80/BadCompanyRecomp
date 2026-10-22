// Read-only, bounded capture of the original cascade calculation.
#pragma once
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <mutex>

namespace trial_shadow_trace {
inline std::atomic<int64_t> deadline_ms{0};
inline std::mutex capture_mutex;
inline int64_t next_ms = 0;
inline unsigned capture_id = 0;
inline unsigned samples = 0;

inline int64_t Now() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline bool BindingVerified(uint8_t* base) {
  // Entry and output matrix stride in this XEX revision.
  return REX_LOAD_U32(0x827D9B10) == 0x7D8802A6 &&
         REX_LOAD_U32(0x827DA5B4) == 0x3BBD0040;
}
inline void Arm() {
  std::lock_guard lock(capture_mutex);
  ++capture_id;
  samples = 0;
  next_ms = 0;
  deadline_ms.store(Now() + 10000, std::memory_order_relaxed);
  REXLOG_INFO("[trial-shadow-trace] capture={} armed for 10 seconds; read-only, at most 40 samples", capture_id);
}
template <size_t N>
inline std::array<float, N> ReadFloats(uint8_t* base, uint32_t address) {
  std::array<float, N> result{};
  for (size_t i = 0; i != N; ++i)
    result[i] = std::bit_cast<float>(REX_LOAD_U32(address + uint32_t(i * 4)));
  return result;
}
inline void Matrix(unsigned id, unsigned sample, const char* kind,
                   unsigned cascade, const std::array<float, 16>& matrix) {
  for (unsigned row = 0; row != 4; ++row) {
    const auto i = row * 4;
    REXLOG_INFO("[trial-shadow-matrix] capture={} sample={} kind={} cascade={} row={} values={:.9g},{:.9g},{:.9g},{:.9g}",
        id, sample, kind, cascade, row, matrix[i], matrix[i+1], matrix[i+2], matrix[i+3]);
  }
}
struct Sample {
  bool active = false;
  unsigned id = 0, index = 0, count = 0;
  uint32_t output = 0;
};
inline Sample Before(PPCContext& ctx, uint8_t* base) {
  const auto deadline = deadline_ms.load(std::memory_order_relaxed);
  if (!deadline) return {};
  const auto now = Now();
  std::lock_guard lock(capture_mutex);
  const auto current_deadline = deadline_ms.load(std::memory_order_relaxed);
  if (!current_deadline) return {};
  if (now >= current_deadline || samples >= 40) {
    deadline_ms.store(0, std::memory_order_relaxed);
    REXLOG_INFO("[trial-shadow-trace] capture={} finished samples={}", capture_id, samples);
    return {};
  }
  // Only the verified WorldRenderer caller, never arbitrary calls to this helper.
  if (ctx.lr != 0x827EA2BC || now < next_ms) return {};
  if (!BindingVerified(base)) {
    deadline_ms.store(0, std::memory_order_relaxed);
    REXLOG_ERROR("[trial-shadow-trace] binding mismatch; capture cancelled");
    return {};
  }
  const uint32_t count = ctx.r3.u32;
  const uint32_t stack = ctx.r1.u32;
  const uint32_t camera = REX_LOAD_U32(stack + 124);
  const uint32_t output = REX_LOAD_U32(stack + 132);
  if (!count || count > 4 || !camera || !output || !ctx.r4.u32) return {};
  next_ms = now + 250;
  Sample sample{true, capture_id, samples++, count, output};
  const auto light = ReadFloats<4>(base, ctx.r4.u32);
  REXLOG_INFO("[trial-shadow-sample] capture={} sample={} tick_ms={} mode={} count={} size={}x{} fixed={} fixed_depth={} camera={:08X} output={:08X} light={:.9g},{:.9g},{:.9g},{:.9g} params={:.9g},{:.9g},{:.9g},{:.9g},{:.9g},{:.9g}",
      sample.id, sample.index, now, trial_shadow::mode.load(std::memory_order_relaxed), count,
      ctx.r10.u32, REX_LOAD_U32(stack + 84), REX_LOAD_U8(stack + 95), REX_LOAD_U8(stack + 103),
      camera, output, light[0], light[1], light[2], light[3],
      ctx.f1.f64, ctx.f2.f64, ctx.f3.f64, ctx.f4.f64, ctx.f5.f64, ctx.f6.f64);
  // sub_822656A8 copies this transform at camera+64 for the original calculation.
  Matrix(sample.id, sample.index, "camera64", 0, ReadFloats<16>(base, camera + 64));
  return sample;
}
inline void After(const Sample& sample, uint8_t* base) {
  if (!sample.active) return;
  for (unsigned i = 0; i != sample.count; ++i)
    Matrix(sample.id, sample.index, "shadow", i,
           ReadFloats<16>(base, sample.output + i * 64));
}
}  // namespace trial_shadow_trace

REX_HOOK_RAW(sub_827D9B10) {
#if defined(_WIN32)
  const DWORD incoming_error = GetLastError();
#endif
  const auto sample = trial_shadow_trace::Before(ctx, base);
#if defined(_WIN32)
  SetLastError(incoming_error);
#endif
  __imp__sub_827D9B10(ctx, base);
#if defined(_WIN32)
  const DWORD outgoing_error = GetLastError();
#endif
  trial_shadow_trace::After(sample, base);
#if defined(_WIN32)
  SetLastError(outgoing_error);
#endif
}

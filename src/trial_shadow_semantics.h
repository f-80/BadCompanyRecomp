// Observe the engine's numeric shadow-matrix semantic, bypassing D3DX names.
#pragma once

namespace trial_shadow_semantics {
inline bool BindingVerified(uint8_t* base) {
  return REX_LOAD_U32(0x82814D70) == 0x7D8802A6 &&
         REX_LOAD_U32(0x82814DA8 + 33 * 4) == 0x82815808 &&
         REX_LOAD_U32(0x82815808) == 0x815E0080 &&
         REX_LOAD_U32(0x8281580C) == 0x816A0184;
}
struct Sample {
  unsigned capture = 0, index = 0, cascade = 0;
  uint32_t output = 0, pair = 0, cursor = 0, enabled = 0;
  std::array<float, 16> input{};
};
inline unsigned previous_capture = 0;
inline std::array<std::array<unsigned, 2>, 4> counts{};
inline std::array<std::array<int64_t, 2>, 4> next_ms{};

inline Sample Before(PPCContext& ctx, uint8_t* base) {
  if (ctx.r5.u32 != 33) return {};
  const auto deadline = trial_shadow_trace::deadline_ms.load(std::memory_order_relaxed);
  if (!deadline) return {};
  const auto now = trial_shadow_trace::Now();
  if (now >= deadline || !BindingVerified(base) || ctx.r6.u32 > 3) return {};
  const uint32_t bindings = ctx.r4.u32, pair = ctx.r3.u32;
  if (!bindings || !pair) return {};
  const uint32_t view = REX_LOAD_U32(bindings + 80);
  const uint32_t shadows = view ? REX_LOAD_U32(view + 128) : 0;
  const uint32_t output = REX_LOAD_U32(pair + 4);
  if (!shadows || !output) return {};
  const unsigned cascade = ctx.r6.u32;
  const uint32_t enabled = REX_LOAD_U32(shadows + 388);
  const unsigned flag_index = enabled != 0;
  std::lock_guard lock(trial_shadow_trace::capture_mutex);
  if (trial_shadow_trace::deadline_ms.load(std::memory_order_relaxed) != deadline) return {};
  if (previous_capture != trial_shadow_trace::capture_id) {
    previous_capture = trial_shadow_trace::capture_id;
    counts = {};
    next_ms = {};
  }
  if (counts[cascade][flag_index] >= 40 || now < next_ms[cascade][flag_index]) return {};
  next_ms[cascade][flag_index] = now + 250;
  Sample s{};
  s.capture = previous_capture;
  s.index = counts[cascade][0] + counts[cascade][1];
  ++counts[cascade][flag_index];
  s.cascade = cascade;
  s.pair = pair;
  s.cursor = REX_LOAD_U32(pair);
  s.output = output;
  s.enabled = enabled;
  if (enabled)
    s.input = trial_shadow_trace::ReadFloats<16>(base, shadows + cascade * 64);
  const int64_t staged_register = int64_t(output) - int64_t(bindings + 96);
  const int64_t register_hint = staged_register >= 0 && staged_register % 16 == 0 && staged_register < 4096
      ? staged_register / 16 : -1;
  REXLOG_INFO("[trial-shadow-semantic] capture={} sample={} cascade={} tick_ms={} enabled={} caller={:08X} view={:08X} source={:08X} output={:08X} staging_register_hint={} cursor={}",
      s.capture, s.index, cascade, now, enabled, uint32_t(ctx.lr), view,
      shadows + cascade * 64, output, register_hint, s.cursor);
  if (enabled)
    trial_shadow_trace::Matrix(s.capture, s.index, "semantic_input", cascade, s.input);
  return s;
}
inline void After(const Sample& s, uint8_t* base) {
  if (!s.capture) return;
  const auto output = trial_shadow_trace::ReadFloats<16>(base, s.output);
  bool matches = true;
  if (s.enabled) {
    for (unsigned r = 0; r != 4; ++r)
      for (unsigned c = 0; c != 4; ++c)
        matches &= std::bit_cast<uint32_t>(output[c * 4 + r]) == std::bit_cast<uint32_t>(s.input[r * 4 + c]);
  }
  trial_shadow_trace::Matrix(s.capture, s.index, "semantic_output", s.cascade, output);
  REXLOG_INFO("[trial-shadow-semantic-result] capture={} sample={} cascade={} enabled={} transpose_checked={} transpose_matches={} cursor_delta={}",
      s.capture, s.index, s.cascade, s.enabled, s.enabled != 0, matches,
      REX_LOAD_U32(s.pair) - s.cursor);
}

inline void Verify() {
  auto* rt = rex::Runtime::instance();
  auto* memory = rt ? rt->memory() : nullptr;
  uint8_t* base = rt ? rt->virtual_membase() : nullptr;
  if (!memory || !base || !BindingVerified(base)) {
    REXLOG_ERROR("[trial-shadow-semantic-test] binding unavailable");
    std::abort();
  }
  const uint32_t allocation = memory->SystemHeapAlloc(16384);
  if (!allocation) std::abort();
  const uint32_t pair = allocation, bindings = allocation + 128;
  const uint32_t view = allocation + 512, shadows = allocation + 2048;
  const uint32_t output = allocation + 4096;
  bool all_correct = true;
  REXLOG_INFO("[trial-shadow-semantic-test] synthetic_semantics_begin=true; isolated scratch memory");
  trial_shadow_trace::Arm();
  for (unsigned test = 0; test != 5; ++test) {
    std::memset(base + allocation, 0, 16384);
    std::memset(base + output - 16, 0xA5, 96);
    const unsigned cascade = test < 4 ? test : 0;
    const bool enabled = test < 4;
    const uint32_t input = shadows + cascade * 64;
    REX_STORE_U32(pair, 17);
    REX_STORE_U32(pair + 4, output);
    REX_STORE_U32(bindings + 80, view);
    REX_STORE_U32(view + 128, shadows);
    REX_STORE_U32(shadows + 388, enabled);
    std::array<uint32_t, 16> values{};
    for (unsigned i = 0; i != 16; ++i) {
      values[i] = std::bit_cast<uint32_t>(float(int(i) * 7 - 31 + int(cascade) * 11) * 0.25f);
      REX_STORE_U32(input + i * 4, values[i]);
    }
    PPCContext scratch{};
    scratch.r1.u32 = allocation + 15872;
    scratch.r3.u32 = pair;
    scratch.r4.u32 = bindings;
    scratch.r5.u32 = 33;
    scratch.r6.u32 = cascade;
    sub_82814D70(scratch, base);
    bool correct = REX_LOAD_U32(pair) == 21 && REX_LOAD_U32(pair + 4) == output;
    if (enabled) {
      for (unsigned r = 0; r != 4; ++r)
        for (unsigned c = 0; c != 4; ++c)
          correct &= REX_LOAD_U32(output + (c * 4 + r) * 4) == values[r * 4 + c];
    } else {
      // Independent expected fixed fallback; the three translations are a XEX constant.
      const uint32_t translation = REX_LOAD_U32(0x8204B7F4);
      const std::array<uint32_t, 16> expected{
          0x3F800000, 0, 0, translation, 0, 0x3F800000, 0, translation,
          0, 0, 0x3F800000, translation, 0, 0, 0, 0x3F800000};
      for (unsigned i = 0; i != 16; ++i)
        correct &= REX_LOAD_U32(output + i * 4) == expected[i];
    }
    for (unsigned i = 0; i != 16; ++i) {
      correct &= REX_LOAD_U8(output - 16 + i) == 0xA5 && REX_LOAD_U8(output + 64 + i) == 0xA5;
      correct &= REX_LOAD_U32(input + i * 4) == values[i];
    }
    REXLOG_INFO("[trial-shadow-semantic-test] cascade={} enabled={} output_and_guards={} gpu_callback=false",
                cascade, enabled, correct);
    all_correct &= correct;
  }
  trial_shadow_trace::deadline_ms.store(0, std::memory_order_relaxed);
  REXLOG_INFO("[trial-shadow-semantic-test] synthetic_semantics_end=true");
  memory->SystemHeapFree(allocation);
  if (!all_correct) std::abort();
}
}  // namespace trial_shadow_semantics

REX_HOOK_RAW(sub_82814D70) {
  if (ctx.r5.u32 != 33 || !trial_shadow_trace::deadline_ms.load(std::memory_order_relaxed)) {
    __imp__sub_82814D70(ctx, base);
    return;
  }
#if defined(_WIN32)
  const DWORD incoming_error = GetLastError();
#endif
  const auto sample = trial_shadow_semantics::Before(ctx, base);
#if defined(_WIN32)
  SetLastError(incoming_error);
#endif
  __imp__sub_82814D70(ctx, base);
#if defined(_WIN32)
  const DWORD outgoing_error = GetLastError();
#endif
  trial_shadow_semantics::After(sample, base);
#if defined(_WIN32)
  SetLastError(outgoing_error);
#endif
}

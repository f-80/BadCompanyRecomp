// Bounded, observational shadow constant capture and an isolated packing check.
#pragma once

namespace trial_shadow_upload {
inline bool BindingVerified(uint8_t* base) {
  return REX_LOAD_U32(0x827F3338) == 0x7D8802A6 &&
         REX_LOAD_U32(0x829695B8) == 0x7D8802A6 &&
         REX_LOAD_U32(0x82088468 + 84) == 0x829695B8;
}

inline void VerifyPacking() {
  auto* rt = rex::Runtime::instance();
  auto* memory = rt ? rt->memory() : nullptr;
  uint8_t* base = rt ? rt->virtual_membase() : nullptr;
  if (!memory || !base || !BindingVerified(base)) {
    REXLOG_ERROR("[trial-shadow-pack-test] binding unavailable");
    std::abort();
  }
  const uint32_t allocation = memory->SystemHeapAlloc(8192);
  if (!allocation) std::abort();
  const uint32_t node = allocation, type = allocation + 128;
  const uint32_t input = allocation + 256, output = allocation + 512;
  const uint32_t table = allocation + 1024, names = allocation + 2048;
  bool all_correct = true;
  unsigned test_index = 0;
  REXLOG_INFO("[trial-shadow-pack-test] synthetic_capture_begin=true; no game objects or GPU callbacks");
  trial_shadow_trace::Arm();
  for (const auto dims : {std::array<unsigned, 3>{3, 4, 4}, {3, 4, 3}, {2, 3, 4}}) {
    std::memset(base + allocation, 0, 8192);
    std::memset(base + output - 16, 0xA5, 96);
    const auto [matrix_class, rows, columns] = dims;
    const unsigned registers = matrix_class == 3 ? columns : rows;
    REX_STORE_U32(table, 0x82088468);
    REX_STORE_U32(table + 8, names);
    REX_STORE_U32(names + 8, 0xFFFE0300);
    std::memcpy(base + names + 32, "shadowTransform0", 17);
    REX_STORE_U8(names + 47, '0' + test_index++);
    REX_STORE_U32(node, table);
    REX_STORE_U32(node + 4, 32);
    REX_STORE_U16(node + 8, 2);  // D3DXRS_FLOAT4
    REX_STORE_U16(node + 12, registers);
    REX_STORE_U32(node + 24, type);
    REX_STORE_U32(node + 28, 0);  // Stop before any GPU callback.
    REX_STORE_U32(node + 32, output);
    REX_STORE_U32(node + 44, registers);
    REX_STORE_U32(node + 52, rows * columns);
    REX_STORE_U16(type, matrix_class);
    REX_STORE_U16(type + 2, 3);  // D3DXPT_FLOAT
    REX_STORE_U16(type + 4, rows);
    REX_STORE_U16(type + 6, columns);
    REX_STORE_U16(type + 8, 1);
    std::array<float, 16> values{};
    for (unsigned i = 0; i != 16; ++i) {
      // Distinct, asymmetric, exactly representable values reveal transposition.
      values[i] = float(int(i) * 7 - 31) * 0.25f;
      REX_STORE_U32(input + i * 4, std::bit_cast<uint32_t>(values[i]));
    }
    PPCContext scratch{};
    scratch.r1.u32 = allocation + 7936;
    scratch.r3.u32 = table;
    scratch.r5.u32 = ~node;
    scratch.r6.u32 = input;
    sub_829695B8(scratch, base);
    bool correct = scratch.r3.u32 == 0x80004005;  // Expected missing callback.
    for (unsigned r = 0; r != rows; ++r)
      for (unsigned c = 0; c != columns; ++c) {
        const unsigned packed = matrix_class == 3 ? c * 4 + r : r * 4 + c;
        correct &= REX_LOAD_U32(output + packed * 4) == std::bit_cast<uint32_t>(values[r * 4 + c]);
      }
    for (unsigned i = 0; i != 16; ++i) {
      correct &= REX_LOAD_U8(output - 16 + i) == 0xA5;
      correct &= REX_LOAD_U8(output + registers * 16 + i) == 0xA5;
      correct &= REX_LOAD_U32(input + i * 4) == std::bit_cast<uint32_t>(values[i]);
    }
    all_correct &= correct;
    REXLOG_INFO("[trial-shadow-pack-test] class={} rows={} columns={} registers={} packing_and_guards={} gpu_callback=false",
                matrix_class, rows, columns, registers, correct);
  }
  trial_shadow_trace::deadline_ms.store(0, std::memory_order_relaxed);
  REXLOG_INFO("[trial-shadow-pack-test] synthetic_capture_end=true");
  memory->SystemHeapFree(allocation);
  if (!all_correct) std::abort();
}

struct Sample {
  unsigned capture = 0, index = 0, cascade = 0;
  uint32_t node = 0, input = 0, packed = 0;
  unsigned matrix_class = 0, rows = 0, columns = 0, registers = 0;
};
inline unsigned previous_capture = 0;
inline std::array<unsigned, 4> counts{};
inline std::array<int64_t, 4> next_ms{};

inline Sample Before(PPCContext& ctx, uint8_t* base) {
  const auto deadline = trial_shadow_trace::deadline_ms.load(std::memory_order_relaxed);
  if (!deadline || trial_shadow_trace::Now() >= deadline) return {};
  if (!BindingVerified(base)) return {};
  // Handles in this verified D3DX table are complemented descriptor pointers.
  const uint32_t table = ctx.r3.u32, node = ~ctx.r5.u32;
  if (!table || !node || REX_LOAD_U32(table) != 0x82088468 ||
      REX_LOAD_U32(node) != table) return {};
  const uint32_t name_offset = REX_LOAD_U32(node + 4);
  if (!name_offset) return {};
  const uint32_t name = REX_LOAD_U32(table + 8) + name_offset;
  constexpr char prefix[] = "shadowTransform";
  for (unsigned i = 0; i != sizeof(prefix) - 1; ++i)
    if (REX_LOAD_U8(name + i) != prefix[i]) return {};
  const unsigned cascade = REX_LOAD_U8(name + 15) - '0';
  if (cascade > 3 || REX_LOAD_U8(name + 16)) return {};
  const uint32_t type = REX_LOAD_U32(node + 24);
  if (!type || REX_LOAD_U16(node + 8) != 2 || REX_LOAD_U16(type + 2) != 3) return {};
  Sample sample{};
  sample.matrix_class = REX_LOAD_U16(type);
  sample.rows = REX_LOAD_U16(type + 4);
  sample.columns = REX_LOAD_U16(type + 6);
  sample.registers = REX_LOAD_U16(node + 12);
  if ((sample.matrix_class != 2 && sample.matrix_class != 3) ||
      !sample.rows || sample.rows > 4 || !sample.columns || sample.columns > 4 ||
      !sample.registers || sample.registers > 4 || REX_LOAD_U16(type + 8) != 1) return {};
  if (sample.registers < (sample.matrix_class == 3 ? sample.columns : sample.rows)) return {};
  const auto now = trial_shadow_trace::Now();
  std::lock_guard lock(trial_shadow_trace::capture_mutex);
  if (trial_shadow_trace::deadline_ms.load(std::memory_order_relaxed) != deadline) return {};
  if (previous_capture != trial_shadow_trace::capture_id) {
    previous_capture = trial_shadow_trace::capture_id;
    counts.fill(0);
    next_ms.fill(0);
  }
  if (counts[cascade] >= 40 || now < next_ms[cascade]) return {};
  next_ms[cascade] = now + 250;
  sample.capture = previous_capture;
  sample.index = counts[cascade]++;
  sample.cascade = cascade;
  sample.node = node;
  sample.input = ctx.r6.u32;
  // The 4x4 row-major fast path passes the input directly to the GPU callback.
  sample.packed = sample.matrix_class == 2 && sample.rows == 4 && sample.columns == 4
      ? sample.input : REX_LOAD_U32(node + 32);
  REXLOG_INFO("[trial-shadow-upload] capture={} sample={} cascade={} caller={:08X} table={:08X} handle={:08X} shader_version={:08X} class={} rows={} columns={} register_start={} registers={} setter={:08X}",
      sample.capture, sample.index, cascade, uint32_t(ctx.lr), table, ctx.r5.u32,
      REX_LOAD_U32(REX_LOAD_U32(table + 8) + 8),
      sample.matrix_class, sample.rows, sample.columns, REX_LOAD_U16(node + 10),
      sample.registers, REX_LOAD_U32(node + 28));
  trial_shadow_trace::Matrix(sample.capture, sample.index, "upload_input", cascade,
      trial_shadow_trace::ReadFloats<16>(base, sample.input));
  return sample;
}
inline void After(const Sample& s, PPCContext& ctx, uint8_t* base) {
  if (!s.capture || !s.packed) return;
  bool correct = true;
  for (unsigned r = 0; r != s.rows; ++r)
    for (unsigned c = 0; c != s.columns; ++c) {
      const unsigned packed = s.matrix_class == 3 ? c * 4 + r : r * 4 + c;
      correct &= REX_LOAD_U32(s.packed + packed * 4) == REX_LOAD_U32(s.input + (r * 4 + c) * 4);
    }
  for (unsigned reg = 0; reg != s.registers; ++reg) {
    const auto v = trial_shadow_trace::ReadFloats<4>(base, s.packed + reg * 16);
    REXLOG_INFO("[trial-shadow-register] capture={} sample={} cascade={} register_offset={} values={:.9g},{:.9g},{:.9g},{:.9g}",
        s.capture, s.index, s.cascade, reg, v[0], v[1], v[2], v[3]);
  }
  REXLOG_INFO("[trial-shadow-upload-result] capture={} sample={} cascade={} hresult={:08X} packing_matches={}",
      s.capture, s.index, s.cascade, ctx.r3.u32, correct);
}
}  // namespace trial_shadow_upload

REX_HOOK_RAW(sub_829695B8) {
#if defined(_WIN32)
  const DWORD incoming_error = GetLastError();
#endif
  const auto sample = trial_shadow_upload::Before(ctx, base);
#if defined(_WIN32)
  SetLastError(incoming_error);
#endif
  __imp__sub_829695B8(ctx, base);
#if defined(_WIN32)
  const DWORD outgoing_error = GetLastError();
#endif
  trial_shadow_upload::After(sample, ctx, base);
#if defined(_WIN32)
  SetLastError(outgoing_error);
#endif
}

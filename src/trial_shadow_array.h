// Scoped, opt-in stacked-depth fetch correction for captured shader hashes.
#pragma once
#include <rex/graphics/command_processor.h>
#include <rex/graphics/pipeline/shader/shader.h>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <rex/hash.h>
#include <vector>
#include "trial_shadow_array_catalog30.h"

namespace trial_shadow_array {
using Processor = rex::graphics::CommandProcessor;
using Shader = rex::graphics::Shader;
using Loader = Shader* (*)(Processor*, uint32_t, uint32_t, const uint32_t*, uint32_t);
inline Loader loader = nullptr;
struct VariantEntry { std::vector<uint32_t> words; Shader* ready = nullptr; };
inline std::unordered_map<uint64_t, VariantEntry> variants;
inline Processor* variant_owner = nullptr;
inline uint64_t variant_cache_hits = 0, variant_sdk_loads = 0, variant_invalidations = 0;
inline void ClearReady(const char* reason) {
  for (auto& [hash, entry] : variants) entry.ready = nullptr;
  variant_owner = nullptr;
  ++variant_invalidations;
  REXLOG_INFO("[trial-shadow-cache] invalidated=true reason={} retained_payloads={}", reason, variants.size());
}
inline bool probe_done = false;
inline std::atomic<uint64_t> corrected_draws{0};
inline std::atomic<bool> enabled{true};
inline uint64_t uncovered_draws = 0, loader_fallbacks = 0;
inline std::unordered_set<uint64_t> uncovered_programs;

struct Access : Processor {
  static auto PixelMember() { return &Access::active_pixel_shader_; }
};
inline const Rule* Find(uint64_t hash) {
  const auto it = std::lower_bound(rules.begin(), rules.end(), hash,
      [](const Rule& rule, uint64_t value) { return rule.hash < value; });
  return it != rules.end() && it->hash == hash ? &*it : nullptr;
}
inline std::vector<uint32_t> Build(const Shader& shader, const Rule& rule) {
  const auto& source = shader.ucode_data();
  if (source.size() != rule.dwords || shader.ucode_data_hash() != rule.hash) return {};
  for (unsigned i = 0; i != rule.count; ++i) {
    const auto offset = offsets[rule.first + i];
    if (offset + 2 >= source.size() || (source[offset] & 31) != 1 ||
        ((source[offset] >> 20) & 31) != 0 || ((source[offset + 2] >> 14) & 3) != 1)
      return {};
  }
  auto result = source;
  for (unsigned i = 0; i != rule.count; ++i) {
    auto& word = result[offsets[rule.first + i] + 2];
    word = (word & ~(3u << 14)) | (2u << 14);
  }
  // The original SDK loader expects guest (big-endian) words.
  for (auto& word : result) word = _byteswap_ulong(word);
  return result;
}
inline Shader* Variant(Processor* processor, Shader* original, const Rule& rule,
                       bool use_cache = REXCVAR_GET(badco_shadow_variant_cache)) {
  if (variant_owner && variant_owner != processor) ClearReady("processor_changed");
  variant_owner = processor;
  auto it = variants.find(rule.hash);
  if (use_cache && it != variants.end() && it->second.ready) {
    ++variant_cache_hits;
    return it->second.ready;
  }
  bool created = false;
  if (it == variants.end()) {
    auto words = Build(*original, rule);
    if (words.empty()) {
      REXLOG_ERROR("[trial-shadow-array] source binding mismatch hash={:016X}; original retained", rule.hash);
      return original;
    }
    it = variants.emplace(rule.hash, VariantEntry{std::move(words), nullptr}).first;
    created = true;
  }
  ++variant_sdk_loads;
  const auto& words = it->second.words;
  auto* replacement = loader(processor, 1, 0, words.data(), uint32_t(words.size()));
  if (replacement) it->second.ready = replacement;
  if (created)
    REXLOG_INFO("[trial-shadow-array] variant_source={:016X} replacement={:016X} fetches={} dimension=2D_to_stacked original_bytes_preserved=true",
                rule.hash, replacement ? replacement->ucode_data_hash() : 0, rule.count);
  if (!replacement) ++loader_fallbacks;
  return replacement ? replacement : original;
}
inline void ObserveUncovered(Shader* shader) {
  if (!REXCVAR_GET(badco_shadow_array_diagnostics) || !shader->is_ucode_analyzed()) return;
  using namespace rex::graphics::xenos;
  const auto& bindings = shader->texture_bindings();
  const bool reads_point_2d = std::any_of(bindings.begin(), bindings.end(), [](const auto& binding) {
    return binding.fetch_constant == 0 && binding.fetch_instr.dimension == FetchOpDimension::k2D &&
           binding.fetch_instr.attributes.mag_filter == TextureFilter::kPoint;
  });
  if (!reads_point_2d) return;
  ++uncovered_draws;
  const auto hash = shader->ucode_data_hash();
  if (uncovered_programs.size() < 32 && uncovered_programs.insert(hash).second)
    REXLOG_INFO("[trial-shadow-coverage] uncovered_point_2d_program={:016X} words={} sampled_right_trigger={}; classification_pending=true",
                hash, shader->ucode_dword_count(), trial_input_frame::observed_right_trigger.load(std::memory_order_relaxed));
}
inline void Report() {
  if (!REXCVAR_GET(badco_shadow_array_diagnostics)) return;
  static int64_t next_ms = 0;
  static uint64_t previous_patched = 0, previous_uncovered = 0;
  const auto now = trial_shadow_trace::Now();
  if (now < next_ms) return;
  next_ms = now + 2000;
  const auto patched = corrected_draws.load(std::memory_order_relaxed);
  REXLOG_INFO("[trial-shadow-coverage] tick_ms={} enabled={} patched_draws={} uncovered_point_2d_draws={} loader_fallbacks={} sampled_right_trigger={}",
              now, enabled.load(std::memory_order_relaxed), patched - previous_patched,
              uncovered_draws - previous_uncovered, loader_fallbacks,
              trial_input_frame::observed_right_trigger.load(std::memory_order_relaxed));
  previous_patched = patched;
  previous_uncovered = uncovered_draws;
}
inline void PreparePipelines() {
  const auto folder = REXCVAR_GET(badco_prepare_shadow_pipelines);
  if (folder.empty()) return;
  const auto root = std::filesystem::path(folder);
  std::ifstream input(root / "source.xpso", std::ios::binary | std::ios::ate);
  const auto length = input.tellg();
  if (!input || length < 12 || (uint64_t(length) - 12) % 72) std::abort();
  std::vector<uint8_t> source(static_cast<size_t>(length));
  input.seekg(0);
  if (!input.read(reinterpret_cast<char*>(source.data()), source.size()) ||
      std::memcmp(source.data(), "XEPSDXRO", 8)) std::abort();
  std::unordered_map<uint64_t, uint64_t> remap;
  std::ofstream mapping(root / "variant_map.tsv");
  for (const auto& [hash, entry] : variants) {
    const uint64_t replacement = XXH3_64bits(entry.words.data(), entry.words.size() * 4);
    remap.emplace(hash, replacement);
    mapping << fmt::format("{:016X}\t{:016X}\n", hash, replacement);
  }
  std::set<std::array<uint8_t, 64>> descriptors;
  auto output = source;
  unsigned added = 0;
  for (size_t position = 12; position < source.size(); position += 72) {
    std::array<uint8_t, 64> description;
    std::memcpy(description.data(), source.data() + position + 8, 64);
    descriptors.insert(description);
    uint64_t stored_hash;
    std::memcpy(&stored_hash, source.data() + position, 8);
    if (stored_hash != XXH3_64bits(description.data(), 64)) std::abort();
  }
  for (size_t position = 12; position < source.size(); position += 72) {
    uint64_t pixel_hash;
    std::memcpy(&pixel_hash, source.data() + position + 24, 8);
    const auto it = remap.find(pixel_hash);
    if (it == remap.end()) continue;
    std::array<uint8_t, 64> description;
    std::memcpy(description.data(), source.data() + position + 8, 64);
    std::memcpy(description.data() + 16, &it->second, 8);
    if (!descriptors.insert(description).second) continue;
    const uint64_t hash = XXH3_64bits(description.data(), 64);
    const size_t end = output.size();
    output.resize(end + 72);
    std::memcpy(output.data() + end, &hash, 8);
    std::memcpy(output.data() + end + 8, description.data(), 64);
    ++added;
  }
  std::ofstream file(root / "prepared.xpso", std::ios::binary);
  file.write(reinterpret_cast<const char*>(output.data()), output.size());
  file.close();
  mapping.close();
  if (!file || !mapping) std::abort();
  REXLOG_INFO("[trial-pipeline-prepare] source_records={} added={} total={} remapped_shaders={} descriptor_hashes_verified=true staged_only=true",
              (source.size() - 12) / 72, added, (output.size() - 12) / 72, remap.size());
}
struct Scope {
  Shader*& slot;
  Shader* saved;
  Scope(Processor* processor, const rex::graphics::RegisterFile* registers)
      : slot(processor->*Access::PixelMember()), saved(slot) {
    if (!REXCVAR_GET(badco_shadow_array_fetch_fix) ||
        !enabled.load(std::memory_order_relaxed) || !saved || !loader) return;
    const auto texture = registers->GetTextureFetch(0);
    using namespace rex::graphics::xenos;
    // Restrict the experiment to the exact 3-layer depth configuration in 103.
    if (texture.type != FetchConstantType::kTexture || !texture.stacked ||
        texture.dimension != DataDimension::k2DOrStacked ||
        texture.format != TextureFormat::k_24_8 || texture.size_2d.width != 1023 ||
        texture.size_2d.height != 1023 || texture.size_2d.stack_depth != 2) return;
    const auto* rule = Find(saved->ucode_data_hash());
    if (!rule) { ObserveUncovered(saved); return; }
    slot = Variant(processor, saved, *rule);
    if (slot != saved) {
      if (!corrected_draws.fetch_add(1, std::memory_order_relaxed))
        REXLOG_INFO("[trial-shadow-array] active=true source={:016X} replacement={:016X} layers=3 size=1024 depth_format=22",
                    saved->ucode_data_hash(), slot->ucode_data_hash());
    }
  }
  ~Scope() { slot = saved; }
};

inline void Probe(Processor* processor) {
  if (probe_done || REXCVAR_GET(badco_shadow_array_probe_root).empty()) return;
  probe_done = true;
  if (!loader) std::abort();
  unsigned checked = 0, changed = 0;
  bool correct = Find(0) == nullptr, scope_verified = false;
  for (const auto& rule : rules) {
    const auto path = std::filesystem::path(REXCVAR_GET(badco_shadow_array_probe_root)) /
        fmt::format("shader_{:016X}.ucode.bin.frag", rule.hash);
    std::vector<uint32_t> normalized(rule.dwords);
    std::ifstream file(path, std::ios::binary);
    if (!file.read(reinterpret_cast<char*>(normalized.data()), normalized.size() * 4)) std::abort();
    auto big_endian = normalized;
    for (auto& word : big_endian) word = _byteswap_ulong(word);
    auto* original = loader(processor, 1, 0, big_endian.data(), rule.dwords);
    if (!original || original->ucode_data_hash() != rule.hash || original->ucode_data() != normalized)
      std::abort();
    auto* replacement = Variant(processor, original, rule);
    if (!replacement || replacement == original) std::abort();
    auto expected = normalized;
    for (unsigned i = 0; i != rule.count; ++i) {
      expected[offsets[rule.first + i] + 2] ^= 3u << 14;
      ++changed;
    }
    correct &= replacement->ucode_data() == expected && original->ucode_data() == normalized &&
               replacement->ucode_data_hash() != rule.hash;
    if (!checked) {
      // GPU-thread-only scratch state, restored before any subsequent draw.
      auto* graphics = static_cast<rex::graphics::GraphicsSystem*>(rex::Runtime::instance()->graphics_system());
      auto* registers = graphics->register_file();
      auto& pixel = processor->*Access::PixelMember();
      Shader* saved_pixel = pixel;
      std::array<uint32_t, 6> saved_fetch{};
      std::copy_n(&registers->values[0x4800], 6, saved_fetch.begin());
      constexpr std::array<uint32_t, 6> captured_fetch{
          0x88024802, 0x0932C496, 0x087FE3FF, 0x00001690, 0, 0x00000200};
      std::copy(captured_fetch.begin(), captured_fetch.end(), &registers->values[0x4800]);
      pixel = original;
      const bool saved_enabled = enabled.exchange(true, std::memory_order_relaxed);
      {
        Scope selected(processor, registers);
        scope_verified = pixel == replacement;
      }
      scope_verified &= pixel == original;
      enabled.store(false, std::memory_order_relaxed);
      { Scope disabled(processor, registers); scope_verified &= pixel == original; }
      enabled.store(true, std::memory_order_relaxed);
      registers->values[0x4801] &= ~63u;  // Wrong format must retain original.
      { Scope wrong_format(processor, registers); scope_verified &= pixel == original; }
      registers->values[0x4801] = captured_fetch[1];
      registers->values[0x4802] &= 0x03FFFFFFu;  // One layer must retain original.
      { Scope single_layer(processor, registers); scope_verified &= pixel == original; }
      pixel = saved_pixel;
      std::copy(saved_fetch.begin(), saved_fetch.end(), &registers->values[0x4800]);
      enabled.store(saved_enabled, std::memory_order_relaxed);
      correct &= scope_verified;
    }
    ++checked;
  }
  const auto& rule = rules.front();
  // Use the actual original program for the cache benchmark and invalidation.
  const auto path = std::filesystem::path(REXCVAR_GET(badco_shadow_array_probe_root)) /
      fmt::format("shader_{:016X}.ucode.bin.frag", rule.hash);
  std::vector<uint32_t> big_endian(rule.dwords);
  std::ifstream input(path, std::ios::binary);
  if (!input.read(reinterpret_cast<char*>(big_endian.data()), big_endian.size() * 4)) std::abort();
  for (auto& word : big_endian) word = _byteswap_ulong(word);
  auto* original = loader(processor, 1, 0, big_endian.data(), rule.dwords);
  constexpr unsigned iterations = 20000;
  const auto loads_before = variant_sdk_loads;
  const auto cached_start = std::chrono::steady_clock::now();
  for (unsigned i = 0; i != iterations; ++i)
    correct &= Variant(processor, original, rule, true) != original;
  const auto cached_finish = std::chrono::steady_clock::now();
  const auto cached_loads = variant_sdk_loads - loads_before;
  for (unsigned i = 0; i != iterations; ++i)
    correct &= Variant(processor, original, rule, false) != original;
  const auto uncached_finish = std::chrono::steady_clock::now();
  const auto uncached_loads = variant_sdk_loads - loads_before - cached_loads;
  const auto invalidations_before = variant_invalidations;
  processor->ClearCaches();
  const auto after_clear_loads = variant_sdk_loads;
  auto* after_clear = Variant(processor, original, rule, true);
  correct &= variant_invalidations == invalidations_before + 1 && after_clear != original &&
             variant_sdk_loads == after_clear_loads + 1;
  correct &= cached_loads == 0 && uncached_loads == iterations;
  REXLOG_INFO("[trial-shadow-cache-test] iterations={} cached_sdk_loads={} uncached_sdk_loads={} cached_ms={:.3f} uncached_ms={:.3f} original_clear_hook_and_reload={} result={}",
      iterations, cached_loads, uncached_loads,
      std::chrono::duration<double, std::milli>(cached_finish - cached_start).count(),
      std::chrono::duration<double, std::milli>(uncached_finish - cached_finish).count(),
      variant_invalidations == invalidations_before + 1, correct);
  REXLOG_INFO("[trial-shadow-array-test] original_sdk_loader=true shaders={} changed_dimensions={} payloads_and_originals_verified={} scope_restore_toggle_format_layers={} gpu_thread=true visual_fix_verified=false",
              checked, changed, correct, scope_verified);
  if (!correct) std::abort();
  PreparePipelines();
}
}  // namespace trial_shadow_array

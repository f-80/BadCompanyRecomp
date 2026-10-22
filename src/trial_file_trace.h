// Keep recent guest reads for a dirty-disc report; never change I/O results.
#pragma once
#if defined(_WIN32)
#include <deque>
#include <mutex>
#include <string>
#include <rex/system/kernel_state.h>
#include <rex/system/xfile.h>

namespace trial_file {
inline std::mutex mutex;
inline std::deque<std::string> recent;
inline uint64_t sequence = 0, failures = 0;

inline bool Read(uint8_t* base, uint32_t address, void* out, size_t size) {
  SIZE_T got = 0;
  return address && ReadProcessMemory(GetCurrentProcess(), base + address, out, size, &got) && got == size;
}
inline uint32_t Word(uint8_t* base, uint32_t address) {
  uint32_t value = 0;
  return Read(base, address, &value, 4) ? _byteswap_ulong(value) : 0xDEADDEAD;
}
inline void Append(const std::string& line) {
  HANDLE file = CreateFileW(L"logs/trial_file_io.txt", FILE_APPEND_DATA,
      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    CloseHandle(file);
  }
}
inline void Call(PPCContext& ctx, uint8_t* base, bool scatter) {
  const DWORD incoming_error = GetLastError();
  static PPCFunc* plain = trial_physical::Original("__imp__NtReadFile");
  static PPCFunc* scattered = trial_physical::Original("__imp__NtReadFileScatter");
  const uint32_t handle = ctx.r3.u32, event = ctx.r4.u32;
  const uint32_t apc = ctx.r5.u32, iosb = ctx.r7.u32, length = ctx.r9.u32;
  const uint32_t lr = static_cast<uint32_t>(ctx.lr);
  uint64_t offset_be = 0;
  const int64_t offset = Read(base, ctx.r10.u32, &offset_be, 8) ?
      static_cast<int64_t>(_byteswap_uint64(offset_be)) : -1;
  std::string path = "<invalid-or-closed-handle>";
  uint64_t file_size = 0, position_before = 0;
  // Release the extra reference before invoking the real import.
  if (auto* rt = rex::Runtime::instance()) {
    if (auto* kernel = rt->kernel_state()) {
      auto file = kernel->object_table()->LookupObject<rex::system::XFile>(handle);
      if (file) {
        path = file->path().substr(0, 768);
        file_size = file->entry()->size();
        position_before = file->position();
      }
    }
  }
  SetLastError(incoming_error);
  (scatter ? scattered : plain)(ctx, base);
  const DWORD returned_error = GetLastError();
  const uint32_t result = ctx.r3.u32;
  const uint32_t status = Word(base, iosb), bytes = Word(base, iosb ? iosb + 4 : 0);
  const bool failed = (result & 0x80000000) || (iosb && (status & 0x80000000));
  const bool short_read = result == 0 && iosb && status == 0 && bytes < length;
  {
    std::lock_guard lock(mutex);
    auto line = fmt::format("seq={} pid={} tid={} op={} handle={:08X} event={:08X} apc={:08X} lr={:08X} offset={} position_before={} requested={} file_size={} result={:08X} iosb={:08X} bytes={} path={}\r\n",
        ++sequence, GetCurrentProcessId(), GetCurrentThreadId(), scatter ? "read_scatter" : "read",
        handle, event, apc, lr, offset, position_before, length, file_size, result, status, bytes, path);
    recent.push_back(line);
    if (recent.size() > 128) recent.pop_front();
    if (failed || short_read) ++failures;
    if (sequence <= 4 || ((failed || short_read) && failures <= 128)) Append(line);
  }
  SetLastError(returned_error);
}
inline void DirtyDisc(PPCContext& ctx, uint8_t* base) {
  const DWORD incoming_error = GetLastError();
  static PPCFunc* original = trial_physical::Original("__imp__XamShowDirtyDiscErrorUI");
  {
    std::lock_guard lock(mutex);
    Append(fmt::format("=== dirty_disc pid={} tid={} parent_LR={:08X} sp={:08X} reads={} failures={} ===\r\n",
        GetCurrentProcessId(), GetCurrentThreadId(), static_cast<uint32_t>(ctx.lr), ctx.r1.u32, sequence, failures));
    for (const auto& line : recent) Append(line);
  }
  REXLOG_ERROR("[trial-file] Dirty disc: caller_LR={:08X}; last 128 reads saved to logs/trial_file_io.txt; forwarding to SDK",
               static_cast<uint32_t>(ctx.lr));
  rex::FlushLogging();
  SetLastError(incoming_error);
  original(ctx, base);
}
} // namespace trial_file
#endif

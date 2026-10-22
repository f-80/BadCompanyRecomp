#pragma once
#include <atomic>
#include <cstdint>
namespace trial_input_frame {
inline std::atomic<uint64_t> epoch{0};
inline std::atomic<uint64_t> mnk_requests{0}, mnk_reads{0};
inline std::atomic<unsigned> observed_right_trigger{0};
}

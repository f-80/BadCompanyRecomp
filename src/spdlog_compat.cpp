// Recompile the SDK's spdlog core against the locally installed MSVC STL.
// Keeps SDK logging behavior without requiring newer vector-algorithm helpers.
#include <spdlog/common-inl.h>
#include <spdlog/details/backtracer-inl.h>
#include <spdlog/details/log_msg-inl.h>
#include <spdlog/details/log_msg_buffer-inl.h>
#include <spdlog/details/os-inl.h>
#include <spdlog/details/registry-inl.h>
#include <spdlog/logger-inl.h>
#include <spdlog/pattern_formatter-inl.h>
#include <spdlog/spdlog-inl.h>
#include <spdlog/details/null_mutex.h>
#include <spdlog/sinks/base_sink-inl.h>
#include <spdlog/sinks/sink-inl.h>

template class spdlog::sinks::base_sink<std::mutex>;
template class spdlog::sinks::base_sink<spdlog::details::null_mutex>;
template spdlog::logger::logger(std::string, spdlog::sinks_init_list::iterator,
                                spdlog::sinks_init_list::iterator);

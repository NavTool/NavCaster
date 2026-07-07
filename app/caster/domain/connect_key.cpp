#include "domain/connect_key.h"

#include <atomic>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <random>
#include <sstream>

namespace navcaster::caster {
namespace {

std::uint64_t fallback_entropy()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return static_cast<std::uint64_t>(now) ^ reinterpret_cast<std::uintptr_t>(&now);
}

std::uint64_t random_u64()
{
    try {
        std::random_device rd;
        const auto high = static_cast<std::uint64_t>(rd()) << 32U;
        const auto low = static_cast<std::uint64_t>(rd());
        const auto value = high ^ low ^ fallback_entropy();
        return value == 0 ? fallback_entropy() : value;
    } catch (...) {
        return fallback_entropy();
    }
}

std::string hex_u64(std::uint64_t value)
{
    std::ostringstream output;
    output << std::hex << std::setw(16) << std::setfill('0') << value;
    return output.str();
}

std::string component_or_default(const std::string &value, const char *fallback)
{
    std::string result;
    result.reserve(value.size());
    for (const unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '.' || ch == '_' || ch == '-') {
            result.push_back(static_cast<char>(ch));
        } else {
            result.push_back('_');
        }
    }
    return result.empty() ? std::string(fallback) : result;
}

std::uint64_t timestamp_or_now(std::uint64_t accepted_at_ms)
{
    if (accepted_at_ms != 0) {
        return accepted_at_ms;
    }
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

struct ConnectKeyState {
    std::string process_nonce = hex_u64(random_u64());
    std::atomic<std::uint64_t> sequence{1};
};

ConnectKeyState &connect_key_state()
{
    static ConnectKeyState state;
    return state;
}

} // namespace

std::string make_connect_key(
    const std::string &runtime_id,
    std::uint64_t accepted_at_ms,
    const std::string &remote_addr,
    std::uint16_t remote_port)
{
    auto &state = connect_key_state();
    const auto sequence = state.sequence.fetch_add(1, std::memory_order_relaxed);
    return component_or_default(runtime_id, "local-runtime") + ":conn:" + state.process_nonce + ":" +
           std::to_string(timestamp_or_now(accepted_at_ms)) + ":" + std::to_string(sequence) + ":" +
           component_or_default(remote_addr, "unknown") + ":" + std::to_string(remote_port);
}

} // namespace navcaster::caster

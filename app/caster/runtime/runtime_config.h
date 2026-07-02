#pragma once

#include <cstdint>
#include <string>

namespace navcaster::caster {

struct RedisEndpoint {
    std::string host = "127.0.0.1";
    std::uint16_t port = 6379;
};

struct RuntimeConfig {
    std::string runtime_id = "local-runtime";
    std::string listen_host = "127.0.0.1";
    std::uint16_t listen_port = 0;
    std::string health_host = "127.0.0.1";
    std::uint16_t health_port = 0;
    std::uint32_t worker_count = 2;
    std::uint32_t self_test_duration_ms = 250;
    bool enable_acceptor = true;
    bool enable_health_api = true;
    bool self_test = false;
    bool help = false;
    RedisEndpoint redis;
};

} // namespace navcaster::caster

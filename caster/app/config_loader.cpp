#include "app/config_loader.h"

#include <cstdlib>
#include <sstream>
#include <vector>

namespace navcaster::caster {
namespace {

bool read_value(const std::vector<std::string> &args, std::size_t &index, const std::string &name, std::string &value)
{
    const std::string prefix = name + "=";
    const std::string &arg = args[index];
    if (arg.rfind(prefix, 0) == 0) {
        value = arg.substr(prefix.size());
        return true;
    }
    if (arg == name && index + 1 < args.size()) {
        ++index;
        value = args[index];
        return true;
    }
    return false;
}

bool parse_u16(const std::string &text, std::uint16_t &out)
{
    char *end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (!end || *end != '\0' || value < 0 || value > 65535) {
        return false;
    }
    out = static_cast<std::uint16_t>(value);
    return true;
}

bool parse_u32(const std::string &text, std::uint32_t &out)
{
    char *end = nullptr;
    const unsigned long value = std::strtoul(text.c_str(), &end, 10);
    if (!end || *end != '\0') {
        return false;
    }
    out = static_cast<std::uint32_t>(value);
    return true;
}

} // namespace

ConfigLoadResult load_runtime_config(int argc, char **argv)
{
    ConfigLoadResult result;
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    for (std::size_t i = 0; i < args.size(); ++i) {
        std::string value;
        const std::string &arg = args[i];
        if (arg == "--help" || arg == "-h") {
            result.config.help = true;
        } else if (arg == "--self-test") {
            result.config.self_test = true;
            result.config.listen_port = 0;
            result.config.health_port = 0;
        } else if (arg == "--no-acceptor") {
            result.config.enable_acceptor = false;
        } else if (arg == "--no-health") {
            result.config.enable_health_api = false;
        } else if (read_value(args, i, "--runtime-id", value)) {
            result.config.runtime_id = value;
        } else if (read_value(args, i, "--listen-host", value)) {
            result.config.listen_host = value;
        } else if (read_value(args, i, "--listen-port", value) || read_value(args, i, "--ntrip-port", value)) {
            if (!parse_u16(value, result.config.listen_port)) {
                result.ok = false;
                result.error = "invalid listen port: " + value;
                return result;
            }
        } else if (read_value(args, i, "--health-host", value)) {
            result.config.health_host = value;
        } else if (read_value(args, i, "--health-port", value)) {
            if (!parse_u16(value, result.config.health_port)) {
                result.ok = false;
                result.error = "invalid health port: " + value;
                return result;
            }
        } else if (read_value(args, i, "--worker-count", value)) {
            if (!parse_u32(value, result.config.worker_count) || result.config.worker_count == 0) {
                result.ok = false;
                result.error = "worker-count must be greater than zero";
                return result;
            }
        } else if (read_value(args, i, "--self-test-duration-ms", value) || read_value(args, i, "--run-ms", value)) {
            if (!parse_u32(value, result.config.self_test_duration_ms)) {
                result.ok = false;
                result.error = "invalid duration: " + value;
                return result;
            }
        } else if (read_value(args, i, "--redis-host", value)) {
            result.config.redis.host = value;
        } else if (read_value(args, i, "--redis-port", value)) {
            if (!parse_u16(value, result.config.redis.port)) {
                result.ok = false;
                result.error = "invalid redis port: " + value;
                return result;
            }
        } else {
            result.ok = false;
            result.error = "unknown argument: " + arg;
            return result;
        }
    }

    return result;
}

std::string runtime_usage()
{
    std::ostringstream out;
    out << "navcaster-caster [options]\n"
        << "\n"
        << "Options:\n"
        << "  --self-test                         start runtime, exercise worker mailbox, then stop\n"
        << "  --worker-count <n>                  number of worker event loops, default 2\n"
        << "  --listen-host <addr>                NTRIP acceptor bind address, default 127.0.0.1\n"
        << "  --listen-port <port>                NTRIP acceptor port, default 0\n"
        << "  --health-host <addr>                local health API bind address, default 127.0.0.1\n"
        << "  --health-port <port>                local health API port, default 0\n"
        << "  --no-acceptor                       skip NTRIP acceptor startup\n"
        << "  --no-health                         skip local health API startup\n"
        << "  --redis-host <addr>                 reserved per-worker Redis endpoint\n"
        << "  --redis-port <port>                 reserved per-worker Redis port\n";
    return out.str();
}

} // namespace navcaster::caster

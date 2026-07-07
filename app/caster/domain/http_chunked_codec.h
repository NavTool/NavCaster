#pragma once

#include <cstddef>
#include <string>

namespace navcaster::caster {

class HttpChunkedDecoder {
public:
    std::string feed(const char *data, std::size_t length);
    std::string feed(const std::string &data) { return feed(data.data(), data.size()); }
    void reset();

    bool complete() const { return _complete; }
    bool failed() const { return _failed; }

private:
    bool parse_next_size();

    std::string _buffer;
    std::size_t _current_chunk_size = 0;
    bool _waiting_for_size = true;
    bool _complete = false;
    bool _failed = false;
};

std::string encode_http_chunk(const char *data, std::size_t length);
std::string encode_http_chunk(const std::string &data);
std::string encode_http_last_chunk();

} // namespace navcaster::caster

#pragma once

#include <cstddef>
#include <string>

namespace navcaster::caster {

class HttpChunkedDecoder {
public:
    std::string feed(const char *data, std::size_t length);
    std::string feed(const std::string &data) { return feed(data.data(), data.size()); }
    void reset();

    bool complete() const { return complete_; }
    bool failed() const { return failed_; }

private:
    bool parse_next_size();

    std::string buffer_;
    std::size_t current_chunk_size_ = 0;
    bool waiting_for_size_ = true;
    bool complete_ = false;
    bool failed_ = false;
};

std::string encode_http_chunk(const char *data, std::size_t length);
std::string encode_http_chunk(const std::string &data);
std::string encode_http_last_chunk();

} // namespace navcaster::caster

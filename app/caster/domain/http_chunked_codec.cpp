#include "domain/http_chunked_codec.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace navcaster::caster {
namespace {

constexpr std::size_t kMaxChunkSizeLine = 128;
constexpr std::size_t kMaxBufferedChunkBytes = 1024 * 1024 * 8;

bool parse_hex_size(const std::string &line, std::size_t &size)
{
    const auto extension = line.find(';');
    const std::string hex = extension == std::string::npos ? line : line.substr(0, extension);
    if (hex.empty()) {
        return false;
    }
    char *end = nullptr;
    const unsigned long long parsed = std::strtoull(hex.c_str(), &end, 16);
    if (!end || *end != '\0') {
        return false;
    }
    size = static_cast<std::size_t>(parsed);
    return true;
}

} // namespace

std::string HttpChunkedDecoder::feed(const char *data, std::size_t length)
{
    std::string decoded;
    if (!data || length == 0 || complete_ || failed_) {
        return decoded;
    }

    buffer_.append(data, length);
    if (buffer_.size() > kMaxBufferedChunkBytes) {
        failed_ = true;
        return {};
    }

    while (!complete_ && !failed_) {
        if (waiting_for_size_) {
            if (!parse_next_size()) {
                break;
            }
            if (complete_) {
                break;
            }
        }

        if (buffer_.size() < current_chunk_size_ + 2) {
            break;
        }
        if (buffer_[current_chunk_size_] != '\r' || buffer_[current_chunk_size_ + 1] != '\n') {
            failed_ = true;
            break;
        }
        decoded.append(buffer_.data(), current_chunk_size_);
        buffer_.erase(0, current_chunk_size_ + 2);
        waiting_for_size_ = true;
        current_chunk_size_ = 0;
    }

    return decoded;
}

void HttpChunkedDecoder::reset()
{
    buffer_.clear();
    current_chunk_size_ = 0;
    waiting_for_size_ = true;
    complete_ = false;
    failed_ = false;
}

bool HttpChunkedDecoder::parse_next_size()
{
    const auto end = buffer_.find("\r\n");
    if (end == std::string::npos) {
        if (buffer_.size() > kMaxChunkSizeLine) {
            failed_ = true;
        }
        return false;
    }
    if (end > kMaxChunkSizeLine) {
        failed_ = true;
        return false;
    }

    std::size_t size = 0;
    if (!parse_hex_size(buffer_.substr(0, end), size)) {
        failed_ = true;
        return false;
    }
    buffer_.erase(0, end + 2);
    if (size == 0) {
        complete_ = true;
        return true;
    }
    current_chunk_size_ = size;
    waiting_for_size_ = false;
    return true;
}

std::string encode_http_chunk(const char *data, std::size_t length)
{
    if (!data || length == 0) {
        return {};
    }
    std::ostringstream out;
    out << std::hex << length << "\r\n";
    std::string encoded = out.str();
    encoded.append(data, length);
    encoded += "\r\n";
    return encoded;
}

std::string encode_http_chunk(const std::string &data)
{
    return encode_http_chunk(data.data(), data.size());
}

std::string encode_http_last_chunk()
{
    return "0\r\n\r\n";
}

} // namespace navcaster::caster

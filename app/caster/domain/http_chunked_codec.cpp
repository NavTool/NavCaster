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
    if (!data || length == 0 || _complete || _failed) {
        return decoded;
    }

    _buffer.append(data, length);
    if (_buffer.size() > kMaxBufferedChunkBytes) {
        _failed = true;
        return {};
    }

    while (!_complete && !_failed) {
        if (_waiting_for_size) {
            if (!parse_next_size()) {
                break;
            }
            if (_complete) {
                break;
            }
        }

        if (_buffer.size() < _current_chunk_size + 2) {
            break;
        }
        if (_buffer[_current_chunk_size] != '\r' || _buffer[_current_chunk_size + 1] != '\n') {
            _failed = true;
            break;
        }
        decoded.append(_buffer.data(), _current_chunk_size);
        _buffer.erase(0, _current_chunk_size + 2);
        _waiting_for_size = true;
        _current_chunk_size = 0;
    }

    return decoded;
}

void HttpChunkedDecoder::reset()
{
    _buffer.clear();
    _current_chunk_size = 0;
    _waiting_for_size = true;
    _complete = false;
    _failed = false;
}

bool HttpChunkedDecoder::parse_next_size()
{
    const auto end = _buffer.find("\r\n");
    if (end == std::string::npos) {
        if (_buffer.size() > kMaxChunkSizeLine) {
            _failed = true;
        }
        return false;
    }
    if (end > kMaxChunkSizeLine) {
        _failed = true;
        return false;
    }

    std::size_t size = 0;
    if (!parse_hex_size(_buffer.substr(0, end), size)) {
        _failed = true;
        return false;
    }
    _buffer.erase(0, end + 2);
    if (size == 0) {
        _complete = true;
        return true;
    }
    _current_chunk_size = size;
    _waiting_for_size = false;
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

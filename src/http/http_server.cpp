#include "http_server.h"
#include <spdlog/spdlog.h>
#include <event2/http.h>
#include <event2/buffer.h>
#include <event2/util.h>
#include <cstring>
#include <fstream>

#define __class__ "http_server"

http_server::http_server() {}

http_server::~http_server()
{
    if (_http)
    {
        evhttp_free(_http);
        _http = nullptr;
    }
}

int http_server::init(event_base *base, int port, const std::string &bind_addr)
{
    _base = base;
    _port = port;
    _bind_addr = bind_addr;

    _http = evhttp_new(base);
    if (!_http)
    {
        spdlog::error("[{}:{}]: Failed to create evhttp", __class__, __func__);
        return -1;
    }

    // Set generic callback for all requests
    evhttp_set_gencb(_http, generic_handler, this);

    // Allow all HTTP methods
    evhttp_set_allowed_methods(_http,
                               EVHTTP_REQ_GET |
                                   EVHTTP_REQ_POST |
                                   EVHTTP_REQ_PUT |
                                   EVHTTP_REQ_DELETE |
                                   EVHTTP_REQ_OPTIONS |
                                   EVHTTP_REQ_PATCH);

    // Bind to address
    if (evhttp_bind_socket(_http, bind_addr.c_str(), port) != 0)
    {
        spdlog::error("[{}:{}]: Failed to bind HTTP server to {}:{}", __class__, __func__, bind_addr, port);
        evhttp_free(_http);
        _http = nullptr;
        return -1;
    }

    spdlog::info("[{}:{}]: HTTP API server listening on {}:{}", __class__, __func__, bind_addr, port);
    return 0;
}

void http_server::route(evhttp_cmd_type method, const std::string &pattern, HttpHandlerFunc handler)
{
    _routes.push_back({method, pattern, handler, nullptr});
}

void http_server::route_raw(evhttp_cmd_type method, const std::string &pattern, RawHttpHandlerFunc handler)
{
    _routes.push_back({method, pattern, nullptr, handler});
}

void http_server::set_cors_origin(const std::string &origin)
{
    _cors_origin = origin;
}

void http_server::set_auth_validator(std::function<bool(const std::string &token)> validator)
{
    _auth_validator = validator;
}

void http_server::add_public_path(const std::string &path)
{
    _public_paths.push_back(path);
}

void http_server::set_static_root(const std::string &web_root)
{
    if (web_root.empty())
        return;
    _web_root = std::filesystem::path(web_root);
    if (std::filesystem::is_directory(_web_root))
    {
        spdlog::info("[{}:{}]: Static file serving enabled from: {}", __class__, __func__, _web_root.string());
    }
    else
    {
        spdlog::warn("[{}:{}]: Web root directory not found: {}", __class__, __func__, _web_root.string());
    }
}

void http_server::generic_handler(evhttp_request *req, void *arg)
{
    auto *server = static_cast<http_server *>(arg);
    server->handle_request(req);
}

HttpRequest http_server::parse_request(evhttp_request *req)
{
    HttpRequest parsed;
    parsed.method = evhttp_request_get_command(req);

    const evhttp_uri *uri = evhttp_request_get_evhttp_uri(req);
    const char *path = evhttp_uri_get_path(uri);
    const char *query = evhttp_uri_get_query(uri);

    parsed.uri = evhttp_request_get_uri(req);
    parsed.path = path ? path : "/";

    // Parse query parameters
    if (query)
    {
        evkeyvalq params;
        evhttp_parse_query_str(query, &params);
        for (evkeyval *kv = params.tqh_first; kv; kv = kv->next.tqe_next)
        {
            parsed.query_params[kv->key] = kv->value;
        }
        evhttp_clear_headers(&params);
    }

    // Parse headers
    evkeyvalq *headers = evhttp_request_get_input_headers(req);
    for (evkeyval *kv = headers->tqh_first; kv; kv = kv->next.tqe_next)
    {
        parsed.headers[kv->key] = kv->value;
    }

    // Parse body
    evbuffer *input = evhttp_request_get_input_buffer(req);
    size_t len = evbuffer_get_length(input);
    if (len > 0)
    {
        parsed.body.resize(len);
        evbuffer_copyout(input, parsed.body.data(), len);
    }

    // Parse path segments (split by '/')
    std::string seg;
    for (size_t i = 1; i < parsed.path.size(); ++i) // skip leading '/'
    {
        if (parsed.path[i] == '/')
        {
            if (!seg.empty())
            {
                parsed.path_segments.push_back(seg);
                seg.clear();
            }
        }
        else
        {
            seg += parsed.path[i];
        }
    }
    if (!seg.empty())
        parsed.path_segments.push_back(seg);

    return parsed;
}

bool http_server::match_route(const RouteEntry &route, const HttpRequest &request) const
{
    if (route.method != request.method)
        return false;

    const std::string &pattern = route.pattern;
    const std::string &path = request.path;

    // Exact match
    if (pattern == path)
        return true;

    // Wildcard match: pattern ends with "/*"
    if (pattern.size() >= 2 && pattern.substr(pattern.size() - 2) == "/*")
    {
        std::string prefix = pattern.substr(0, pattern.size() - 2);
        if (path.size() > prefix.size() && path.substr(0, prefix.size()) == prefix && path[prefix.size()] == '/')
        {
            return true;
        }
    }

    return false;
}

std::string http_server::extract_path_param(const std::string &pattern, const std::string &path) const
{
    // For pattern "/api/xxx/*", extract everything after "/api/xxx/"
    if (pattern.size() >= 2 && pattern.substr(pattern.size() - 2) == "/*")
    {
        std::string prefix = pattern.substr(0, pattern.size() - 1); // "/api/xxx/"
        if (path.size() > prefix.size())
        {
            return path.substr(prefix.size());
        }
    }
    return {};
}

bool http_server::is_public_path(const std::string &path) const
{
    for (const auto &p : _public_paths)
    {
        if (path == p)
            return true;
        // Check prefix match for wildcard public paths
        if (p.size() >= 2 && p.substr(p.size() - 2) == "/*")
        {
            std::string prefix = p.substr(0, p.size() - 2);
            if (path.size() >= prefix.size() && path.substr(0, prefix.size()) == prefix)
                return true;
        }
    }
    return false;
}

void http_server::send_cors_headers(evhttp_request *req, evbuffer *buf)
{
    if (!_cors_origin.empty())
    {
        evhttp_add_header(evhttp_request_get_output_headers(req), "Access-Control-Allow-Origin", _cors_origin.c_str());
        evhttp_add_header(evhttp_request_get_output_headers(req), "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        evhttp_add_header(evhttp_request_get_output_headers(req), "Access-Control-Allow-Headers", "Content-Type, Authorization");
        evhttp_add_header(evhttp_request_get_output_headers(req), "Access-Control-Max-Age", "86400");
    }
}

void http_server::send_response(evhttp_request *req, const HttpResponse &resp)
{
    evbuffer *buf = evbuffer_new();

    // Add CORS headers
    send_cors_headers(req, buf);

    // Content-Type
    evhttp_add_header(evhttp_request_get_output_headers(req), "Content-Type", resp.content_type.c_str());

    // Custom headers
    for (const auto &[key, value] : resp.headers)
    {
        evhttp_add_header(evhttp_request_get_output_headers(req), key.c_str(), value.c_str());
    }

    // Body
    if (!resp.body.empty())
    {
        evbuffer_add(buf, resp.body.data(), resp.body.size());
    }

    evhttp_send_reply(req, resp.status_code, nullptr, buf);
    evbuffer_free(buf);
}

void http_server::handle_request(evhttp_request *req)
{
    HttpRequest parsed = parse_request(req);

    // Handle OPTIONS (CORS preflight)
    if (parsed.method == EVHTTP_REQ_OPTIONS)
    {
        HttpResponse resp;
        resp.status_code = 204;
        resp.body = "";
        send_response(req, resp);
        return;
    }

    // Auth check
    if (_auth_validator && !is_public_path(parsed.path))
    {
        std::string token;
        auto it = parsed.headers.find("Authorization");
        if (it != parsed.headers.end())
        {
            const std::string &auth = it->second;
            if (auth.size() > 7 && auth.substr(0, 7) == "Bearer ")
            {
                token = auth.substr(7);
            }
        }
        if (!_auth_validator(token))
        {
            HttpResponse resp;
            resp.status_code = 401;
            resp.body = R"({"error":"Unauthorized","message":"Invalid or missing token"})";
            send_response(req, resp);
            return;
        }
    }

    // Find matching route
    for (const auto &route : _routes)
    {
        if (match_route(route, parsed))
        {
            if (route.is_raw())
            {
                // Raw handler manages the response lifecycle itself (e.g. SSE)
                try
                {
                    route.raw_handler(req, parsed);
                }
                catch (const std::exception &e)
                {
                    spdlog::error("[{}:{}]: Raw handler exception: {}", __class__, __func__, e.what());
                    HttpResponse resp;
                    resp.status_code = 500;
                    resp.body = R"({"error":"Internal Server Error"})";
                    send_response(req, resp);
                }
                return;
            }

            HttpResponse resp;
            try
            {
                route.handler(parsed, resp);
            }
            catch (const std::exception &e)
            {
                spdlog::error("[{}:{}]: Handler exception: {}", __class__, __func__, e.what());
                resp.status_code = 500;
                resp.body = R"({"error":"Internal Server Error"})";
            }
            send_response(req, resp);
            return;
        }
    }

    // No route matched — try static file serving
    if (!_web_root.empty() && parsed.method == EVHTTP_REQ_GET)
    {
        if (serve_static_file(req, parsed.path))
            return;
    }

    // Nothing matched
    HttpResponse resp;
    resp.status_code = 404;
    resp.body = R"({"error":"Not Found"})";
    send_response(req, resp);
}

std::string http_server::get_mime_type(const std::string &ext)
{
    static const std::unordered_map<std::string, std::string> mime_types = {
        {".html", "text/html; charset=utf-8"},
        {".css", "text/css; charset=utf-8"},
        {".js", "application/javascript; charset=utf-8"},
        {".json", "application/json; charset=utf-8"},
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif", "image/gif"},
        {".svg", "image/svg+xml"},
        {".ico", "image/x-icon"},
        {".woff", "font/woff"},
        {".woff2", "font/woff2"},
        {".ttf", "font/ttf"},
        {".txt", "text/plain; charset=utf-8"},
        {".map", "application/json"},
    };
    auto it = mime_types.find(ext);
    return (it != mime_types.end()) ? it->second : "application/octet-stream";
}

bool http_server::serve_static_file(evhttp_request *req, const std::string &url_path)
{
    namespace fs = std::filesystem;

    // Decode percent-encoded path
    char *decoded = evhttp_uridecode(url_path.c_str(), 0, nullptr);
    std::string decoded_path = decoded ? decoded : url_path;
    if (decoded) free(decoded);

    // Security: reject paths containing ".." to prevent directory traversal
    if (decoded_path.find("..") != std::string::npos)
        return false;

    // Map URL path to filesystem path
    std::string relative = decoded_path;
    if (!relative.empty() && relative[0] == '/')
        relative = relative.substr(1);

    fs::path file_path = _web_root / relative;

    // If it's a directory, try index.html
    if (fs::is_directory(file_path))
        file_path = file_path / "index.html";

    // If file exists, serve it
    if (fs::is_regular_file(file_path))
    {
        std::ifstream ifs(file_path, std::ios::binary);
        if (!ifs.is_open())
            return false;

        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        ifs.close();

        std::string ext = file_path.extension().string();
        std::string mime = get_mime_type(ext);

        evbuffer *buf = evbuffer_new();
        evhttp_add_header(evhttp_request_get_output_headers(req), "Content-Type", mime.c_str());

        // Cache static assets (js/css with hash in filename), no-cache for html
        if (ext == ".html")
        {
            evhttp_add_header(evhttp_request_get_output_headers(req), "Cache-Control", "no-cache");
        }
        else if (ext == ".js" || ext == ".css" || ext == ".woff2" || ext == ".woff" || ext == ".ttf")
        {
            evhttp_add_header(evhttp_request_get_output_headers(req), "Cache-Control", "public, max-age=31536000, immutable");
        }

        if (!_cors_origin.empty())
            send_cors_headers(req, buf);

        evbuffer_add(buf, content.data(), content.size());
        evhttp_send_reply(req, 200, nullptr, buf);
        evbuffer_free(buf);
        return true;
    }

    // SPA fallback: for non-API paths, serve index.html
    if (url_path.substr(0, 4) != "/api")
    {
        fs::path index_path = _web_root / "index.html";
        if (fs::is_regular_file(index_path))
        {
            std::ifstream ifs(index_path, std::ios::binary);
            if (ifs.is_open())
            {
                std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
                ifs.close();

                evbuffer *buf = evbuffer_new();
                evhttp_add_header(evhttp_request_get_output_headers(req), "Content-Type", "text/html; charset=utf-8");
                evhttp_add_header(evhttp_request_get_output_headers(req), "Cache-Control", "no-cache");

                if (!_cors_origin.empty())
                    send_cors_headers(req, buf);

                evbuffer_add(buf, content.data(), content.size());
                evhttp_send_reply(req, 200, nullptr, buf);
                evbuffer_free(buf);
                return true;
            }
        }
    }

    return false;
}

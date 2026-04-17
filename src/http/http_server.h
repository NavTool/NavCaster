#pragma once

#include <event2/event.h>
#include <event2/http.h>
#include <event2/buffer.h>
#include <event2/keyvalq_struct.h>
#include <string>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <filesystem>

struct HttpRequest
{
    evhttp_cmd_type method;
    std::string uri;
    std::string path;
    std::string body;
    std::unordered_map<std::string, std::string> headers;
    std::unordered_map<std::string, std::string> query_params;
    std::vector<std::string> path_segments; // parsed from URI path
};

struct HttpResponse
{
    int status_code = 200;
    std::string content_type = "application/json";
    std::string body;
    std::unordered_map<std::string, std::string> headers;
};

using HttpHandlerFunc = std::function<void(const HttpRequest &req, HttpResponse &resp)>;

// Raw handler gets direct access to evhttp_request* (for SSE/chunked streaming)
using RawHttpHandlerFunc = std::function<void(evhttp_request *raw_req, const HttpRequest &req)>;

struct RouteEntry
{
    evhttp_cmd_type method;
    std::string pattern; // e.g. "/api/accounts" or "/api/accounts/*"
    HttpHandlerFunc handler;
    RawHttpHandlerFunc raw_handler; // if set, handler is ignored
    bool is_raw() const { return raw_handler != nullptr; }
};

class http_server
{
public:
    http_server();
    ~http_server();

    // Initialize the HTTP server on the given event_base
    int init(event_base *base, int port, const std::string &bind_addr = "0.0.0.0");

    // Register a route handler
    void route(evhttp_cmd_type method, const std::string &pattern, HttpHandlerFunc handler);

    // Register a raw route handler (for SSE/chunked streaming)
    void route_raw(evhttp_cmd_type method, const std::string &pattern, RawHttpHandlerFunc handler);

    // Set CORS allowed origin (empty to disable)
    void set_cors_origin(const std::string &origin);

    // Set auth token validator
    void set_auth_validator(std::function<bool(const std::string &token)> validator);

    // Public paths that skip auth
    void add_public_path(const std::string &path);

    // Set static file root directory for serving web frontend
    void set_static_root(const std::string &web_root);

private:
    static void generic_handler(evhttp_request *req, void *arg);
    static void options_handler(evhttp_request *req, void *arg);

    void handle_request(evhttp_request *req);
    void send_response(evhttp_request *req, const HttpResponse &resp);
    void send_cors_headers(evhttp_request *req, evbuffer *buf);

    HttpRequest parse_request(evhttp_request *req);
    bool match_route(const RouteEntry &route, const HttpRequest &request) const;
    bool is_public_path(const std::string &path) const;

    // Extract path parameter from wildcard match
    std::string extract_path_param(const std::string &pattern, const std::string &path) const;

    // Serve a static file, returns true if file was served
    bool serve_static_file(evhttp_request *req, const std::string &path);
    static std::string get_mime_type(const std::string &ext);

private:
    event_base *_base = nullptr;
    evhttp *_http = nullptr;
    int _port = 8080;
    std::string _bind_addr = "0.0.0.0";

    std::vector<RouteEntry> _routes;
    std::string _cors_origin;
    std::function<bool(const std::string &)> _auth_validator;
    std::vector<std::string> _public_paths;
    std::filesystem::path _web_root;
};

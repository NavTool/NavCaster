#pragma once

#include <string>
#include <chrono>
#include <list>

#ifdef _WIN32
#define util_socket_t intptr_t
#else
#define util_socket_t int
#endif

std::string util_random_string(int string_len);

std::string util_cal_connect_key(const char *ServerIP, int serverPort, const char *ClientIP, int clientPort);

std::string util_cal_half_key(const char *IP, int Port);

std::string util_cal_connect_key(int fd);

std::string util_port_to_key(int port);

std::string util_get_user_ip(int fd);
int util_get_user_port(int fd);

std::string util_get_date_time();
std::string util_get_space_time();

std::string util_get_http_date();
std::time_t util_get_now_second();

long long util_get_time_stamp();

std::string util_get_time_stamp_str();

int util_get_use_memory();

void util_ecef2pos(double ecef_x, double ecef_y, double ecef_z, double &lat, double &lon, double &alt);

void util_pos2ecef(double lat, double lon, double alt, double &ecef_x, double &ecef_y, double &ecef_z);

double util_dist3d(double x1, double y1, double z1,
                   double x2, double y2, double z2);

std::string util_generate_random_key(int length);

// 返回 RTT（微秒），失败返回 -1
int64_t util_get_tcp_delay(util_socket_t sockfd);

// // 生成一个 NTRIP 请求头
// std::string gen_ntrip_ntrip_server_req(const char *mount_point, const char *user_name, const char *password, bool ntrip_v2 = false, bool chunked = false);
// std::string gen_ntrip_ntrip_client_req(const char *mount_point, const char *user_name, const char *password, bool ntrip_v2 = false, bool chunked = false);

// std::string gen_ntrip_ntrip_server_respone(bool ntrip_v2 = false, bool chunked = false);
// std::string gen_ntrip_ntrip_client_respone(bool ntrip_v2 = false, bool chunked = false);

// // 解析 NTRIP 请求头，返回 mount_point,user_name,password,chunked version parameters
// std::string decode_ntrip_req(const char *auth_str);


std::list<std::string> util_split_string(std::string str, char delimiter);
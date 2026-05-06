/*
    ntrip_msg.h - NTRIP request/response message helpers.
*/
#pragma once

#include "ntrip_global.h"

#include <string>

std::string build_nrtip_reply(ConnectType type, bool version2, bool chunked);
std::string build_ntrip_request(ConnectType type, bool version2, std::string mpt, std::string host, std::string auth);

bool verify_ntrip_response(const char *data, size_t len, bool &version2, bool &chunked);
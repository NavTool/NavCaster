#pragma once

#include "ntrip_global.h"

class ntrip_config
{
public:
    CasterCoreOpt _caster_core_opt;
    AuthVerifyOpt _auth_verify_opt;

    ListenerOpt _listener_opt;
    ServiceOpt _service_opt;
    NtripServerOpt _ntrip_server_opt;
    NtripClientOpt _ntrip_client_opt;

public:
    ntrip_config(/* args */);
    ~ntrip_config();

    static ntrip_config *getInstance();


    int Init(int argc, char **argv,std::string conf_file_path);

    int load_Caster_Conf(std::string conf_file_path);
    int load_Core_Conf(std::string conf_file_path);
    int load_Auth_Conf(std::string conf_file_path);

    int load_Conf_from_Center(std::string conf_center_addr, int port, std::string conf_center_auth);
};

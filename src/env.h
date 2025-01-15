#pragma once
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <iostream>

int getIntFromEnv(const char * env_var_name, bool isRequired)  {
    const char* env_server_port = getenv(env_var_name);
    if (env_server_port != nullptr) {
        return atoi(env_server_port);
    }
    else if (isRequired) {
        std::cerr << "Environment variable " << env_var_name << " not found." << std::endl;
        exit(1);
    }
    return 0;
}

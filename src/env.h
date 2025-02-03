#pragma once
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <iostream>

int getIntFromEnv(const char * env_var_name, bool isRequired, int defaultVal = 0)  {
    const char* env_var = std::getenv(env_var_name);
    if (env_var != nullptr) {
        return atoi(env_var);
    }
    else if (isRequired) {
        std::cerr << "Environment variable " << env_var_name << " not found." << std::endl;
        exit(1);
    }
    return defaultVal;
}

const char* getStrFromEnv(const char* env_var_name, bool isRequired)  {
    const char* env_var = getenv(env_var_name);
    if (env_var != nullptr) {
        return env_var;
    }
    else if (isRequired) {
        std::cerr << "Environment variable " << env_var_name << " not found." << std::endl;
        exit(1);
    }
    return nullptr;
}

#pragma once
#include <cstdlib>
#include <iostream>
#include <type_traits>
#include <cstring>
#include <strings.h>

template <typename T>
T getFromEnv(const char* env_var_name, bool isRequired, T defaultVal = T()) {
    const char* env_var = std::getenv(env_var_name);
    
    if (env_var != nullptr) {
        if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
            return static_cast<T>(std::atoll(env_var));  // Handles signed integers
        } 
        else if constexpr (std::is_integral_v<T> && std::is_unsigned_v<T>) {
            return static_cast<T>(std::strtoull(env_var, nullptr, 10));  // Handles unsigned integers
        }
        else if constexpr (std::is_floating_point_v<T>) {
            return static_cast<T>(std::atof(env_var));
        } 
        else if constexpr (std::is_same_v<T, bool>) {
            return (std::strcmp(env_var, "1") == 0 || strcasecmp(env_var, "true") == 0);
        } 
        else if constexpr (std::is_same_v<T, const char*>) {
            return env_var;
        } 
        else {
            static_assert(sizeof(T) == 0, "Unsupported type for environment variable retrieval.");
        }
    }

    if (isRequired) {
        std::cerr << "Environment variable " << env_var_name << " not found." << std::endl;
        std::exit(1);
    }

    return defaultVal;
}

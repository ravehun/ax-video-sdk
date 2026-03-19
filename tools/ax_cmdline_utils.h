#pragma once

#include <cstddef>
#include <iostream>
#include <string>

#include "cmdline/cmdline.hpp"

namespace axvsdk::tooling {

enum class CliParseResult {
    kOk = 0,
    kExitSuccess,
    kExitFailure,
};

inline CliParseResult ParseCommandLine(cmdline::parser& parser, int argc, char** argv) {
    try {
        parser.add("help", 'h', "print this message");
    } catch (const cmdline::cmdline_error&) {
    }

    if (!parser.parse(argc, argv)) {
        std::cerr << parser.error() << "\n" << parser.usage();
        return CliParseResult::kExitFailure;
    }

    if (parser.exist("help")) {
        std::cout << parser.usage();
        return CliParseResult::kExitSuccess;
    }

    return CliParseResult::kOk;
}

inline int CliParseExitCode(CliParseResult result) noexcept {
    return result == CliParseResult::kExitSuccess ? 0 : 1;
}

template <typename T>
bool ParseValue(const std::string& text, T* value) {
    if (value == nullptr) {
        return false;
    }

    try {
        *value = cmdline::detail::lexical_cast<T>(text);
        return true;
    } catch (...) {
        return false;
    }
}

template <>
inline bool ParseValue<std::string>(const std::string& text, std::string* value) {
    if (value == nullptr) {
        return false;
    }
    *value = text;
    return true;
}

template <typename T>
bool GetOptionalArgument(const cmdline::parser& parser,
                         const std::string& option_name,
                         std::size_t positional_index,
                         const T& default_value,
                         T* value,
                         std::ostream& error_stream) {
    if (value == nullptr) {
        return false;
    }

    if (parser.exist(option_name)) {
        *value = parser.get<T>(option_name);
        return true;
    }

    const auto& positional = parser.rest();
    if (positional_index < positional.size()) {
        if (ParseValue<T>(positional[positional_index], value)) {
            return true;
        }

        error_stream << "invalid positional argument: " << positional[positional_index] << "\n";
        return false;
    }

    *value = default_value;
    return true;
}

template <typename T>
bool GetRequiredArgument(const cmdline::parser& parser,
                         const std::string& option_name,
                         std::size_t positional_index,
                         const char* display_name,
                         T* value,
                         std::ostream& error_stream) {
    if (value == nullptr) {
        return false;
    }

    if (parser.exist(option_name)) {
        *value = parser.get<T>(option_name);
        return true;
    }

    const auto& positional = parser.rest();
    if (positional_index < positional.size()) {
        if (ParseValue<T>(positional[positional_index], value)) {
            return true;
        }

        error_stream << "invalid positional argument: " << positional[positional_index] << "\n";
        return false;
    }

    error_stream << "missing required argument: " << display_name << "\n";
    return false;
}

}  // namespace axvsdk::tooling

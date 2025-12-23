#pragma once

#include <initializer_list>
#include <string_view>
#include <vector>

namespace zero {

struct ArgPair {
  std::string_view name;
  std::string_view value;

  ArgPair(std::string_view name, std::string_view value) : name(name), value(value) {}
};

struct ArgParser {
  // This could be faster with map, but probably not in average case of low arg count.
  // Also probably doesn't matter since this should just be used during startup.
  std::vector<ArgPair> args;

  ArgParser(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
      std::string_view arg = argv[i];

      if (arg[0] == '-') {
        arg = arg.substr(1);

        // Handle -- case
        if (arg[0] == '-') {
          arg = arg.substr(1);
        }

        std::string_view value;

        if (i < argc - 1) {
          std::string_view next = argv[i + 1];

          if (next[0] != '-') {
            value = next;
            ++i;
          }
        }

        args.emplace_back(arg, value);
      }
    }
  }

  inline std::string_view GetValue(std::initializer_list<const char*> parameters) {
    for (const auto& arg : args) {
      for (auto& parameter : parameters) {
        if (arg.name == parameter) {
          return arg.value;
        }
      }
    }

    return "";
  }

  inline std::string_view GetValue(const char* parameter) { return GetValue({parameter}); }

  inline bool HasParameter(std::initializer_list<const char*> parameters) {
    for (const auto& arg : args) {
      for (auto& parameter : parameters) {
        if (arg.name == parameter) {
          return true;
        }
      }
    }

    return false;
  }

  inline bool HasParameter(const char* parameter) { return HasParameter({parameter}); }
};

}  // namespace zero

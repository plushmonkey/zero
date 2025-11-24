#pragma once

#include <stdlib.h>
#include <zero/Types.h>

#include <string>
#include <string_view>
#include <vector>

namespace zero {

inline std::vector<std::string_view> SplitString(std::string_view string, std::string_view delim) {
  std::vector<std::string_view> result;

  size_t offset = 0;
  size_t start = 0;

  while ((offset = string.find(delim, offset)) != std::string::npos) {
    std::string_view split = string.substr(start, offset - start);

    result.push_back(split);

    offset += delim.size();
    start = offset;
  }

  result.push_back(string.substr(start));

  return result;
}

inline std::string_view Trim(std::string_view str) {
  while (!str.empty() && (str[0] == ' ' || str[0] == '\r' || str[0] == '\t')) {
    str = str.substr(1);
  }

  while (!str.empty() && (str.back() == ' ' || str.back() == '\r' || str.back() == '\t')) {
    str = str.substr(0, str.size() - 1);
  }

  return str;
}

// Gets the frequency and request arena name from a user arena string. This is because the arena_number and arena_name
// are handled in a specific way for the login request packet.
inline std::pair<u16, std::string> ParseLoginArena(std::string_view arena) {
  if (arena.empty() || arena == "-1") {
    return std::make_pair<u16, std::string>(0xFFFF, "");
  }

  bool is_number = true;
  for (size_t i = 0; i < arena.size(); ++i) {
    if (arena[i] < '0' || arena[i] > '9') {
      is_number = false;
      break;
    }
  }

  if (is_number) {
    int number = atoi(arena.data());

    if (number > 0xFFFF) {
      number = 0xFFFF;
    }

    return std::make_pair<u16, std::string>(number, "");
  }

  return std::make_pair<u16, std::string>(0xFFFD, arena.data());
}

}  // namespace zero

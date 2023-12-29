#pragma once

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

}  // namespace zero

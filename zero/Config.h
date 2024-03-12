#pragma once

#include <stdlib.h>

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace zero {

struct ConfigGroup {
  std::unordered_map<std::string, std::string> map;
};

struct Config {
  std::unordered_map<std::string, ConfigGroup> groups;

  inline ConfigGroup& GetOrCreateGroup(const std::string& group) {
    auto group_iter = groups.find(group);
    if (group_iter == groups.end()) {
      group_iter = groups.insert(std::make_pair(group, ConfigGroup())).first;
    }

    return group_iter->second;
  }

  inline std::optional<int> GetInt(const std::string& group, const char* key) {
    auto group_iter = groups.find(group);
    if (group_iter == groups.end()) return {};

    auto map_iter = group_iter->second.map.find(key);
    if (map_iter == group_iter->second.map.end()) return {};

    return (int)strtol(map_iter->second.data(), nullptr, 10);
  }

  inline std::optional<const char*> GetString(const std::string& group, const char* key) {
    auto group_iter = groups.find(group);
    if (group_iter == groups.end()) return {};

    auto map_iter = group_iter->second.map.find(key);
    if (map_iter == group_iter->second.map.end()) return {};

    return map_iter->second.data();
  }

  static std::unique_ptr<Config> Load(const char* file_path);
};

}  // namespace zero

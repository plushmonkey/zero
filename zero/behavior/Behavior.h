#pragma once

#include <zero/behavior/BehaviorTree.h>

#include <string>
#include <unordered_map>

namespace zero {
namespace behavior {

struct Behavior {
  virtual void OnInitialize(ExecuteContext& ctx) = 0;
  virtual std::unique_ptr<BehaviorNode> CreateTree(ExecuteContext& ctx) = 0;
};

struct BehaviorRepository {
  inline void Add(const std::string& name, std::unique_ptr<Behavior> tree) { behaviors[name] = std::move(tree); }

  inline Behavior* Find(const std::string& name) {
    auto iter = behaviors.find(name);

    if (iter == behaviors.end()) return nullptr;

    return iter->second.get();
  }

  inline void Clear() { behaviors.clear(); }

  std::unordered_map<std::string, std::unique_ptr<Behavior>> behaviors;
};

}  // namespace behavior
}  // namespace zero

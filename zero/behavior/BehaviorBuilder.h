#pragma once

#include <zero/behavior/BehaviorTree.h>

#include <memory>
#include <vector>

namespace zero {
namespace behavior {

enum class CompositeType {
  None,
  Sequence,
  Selector,
  Parallel,
};

class CompositeBuilder {
 public:
  CompositeBuilder() : parent(nullptr), type(CompositeType::None) {}
  CompositeBuilder(CompositeType type) : parent(nullptr), type(type) {}
  CompositeBuilder(CompositeBuilder* parent, CompositeType type) : parent(parent), type(type) {}

  template <typename T, typename... Args>
  CompositeBuilder& Child(Args... args) {
    children.emplace_back(std::make_unique<T>(std::forward<Args>(args)...));
    return *this;
  }

  template <typename T, typename... Args>
  CompositeBuilder& InvertChild(Args... args) {
    children.emplace_back(std::make_unique<InvertNode>(std::make_unique<T>(std::forward<Args>(args)...)));
    return *this;
  }

  CompositeBuilder& Sequence();
  CompositeBuilder& Selector();
  CompositeBuilder& Parallel();
  CompositeBuilder& End();

  inline CompositeType GetType() const { return type; }

  std::vector<std::unique_ptr<BehaviorNode>>& GetChildren() { return children; }

 private:
  CompositeBuilder* parent;
  CompositeType type;

  std::unique_ptr<CompositeBuilder> current_builder;

  std::vector<std::unique_ptr<BehaviorNode>> children;
};

class BehaviorBuilder {
 public:
  CompositeBuilder& Sequence();
  CompositeBuilder& Selector();
  CompositeBuilder& Parallel();

  std::unique_ptr<BehaviorNode> Build();

 private:
  CompositeBuilder composite;
};

}  // namespace behavior
}  // namespace zero

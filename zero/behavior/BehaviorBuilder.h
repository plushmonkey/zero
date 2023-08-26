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

enum class CompositeDecorator { None, Success, Invert };

class CompositeBuilder {
 public:
  CompositeBuilder() : parent(nullptr), type(CompositeType::None) {}
  CompositeBuilder(CompositeType type) : parent(nullptr), type(type) {}
  CompositeBuilder(CompositeBuilder* parent, CompositeType type) : parent(parent), type(type) {}
  CompositeBuilder(CompositeBuilder* parent, CompositeType type, CompositeDecorator decorator)
      : parent(parent), type(type), decorator(decorator) {}

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

  template <typename T, typename... Args>
  CompositeBuilder& SuccessChild(Args... args) {
    children.emplace_back(std::make_unique<SuccessNode>(std::make_unique<T>(std::forward<Args>(args)...)));
    return *this;
  }

  CompositeBuilder& Sequence(CompositeDecorator decorator = CompositeDecorator::None);
  CompositeBuilder& Selector(CompositeDecorator decorator = CompositeDecorator::None);
  CompositeBuilder& Parallel(CompositeDecorator decorator = CompositeDecorator::None);

  CompositeBuilder& End();

  inline CompositeType GetType() const { return type; }

  std::vector<std::unique_ptr<BehaviorNode>>& GetChildren() { return children; }

 private:
  CompositeBuilder* parent;
  CompositeType type;
  CompositeDecorator decorator = CompositeDecorator::None;

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

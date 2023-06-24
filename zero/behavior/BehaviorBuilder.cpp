#include "BehaviorBuilder.h"

namespace zero {
namespace behavior {

CompositeBuilder& CompositeBuilder::Sequence() {
  children.emplace_back(std::make_unique<SequenceNode>());

  current_builder = std::make_unique<CompositeBuilder>(this, CompositeType::Sequence);

  return *current_builder;
}

CompositeBuilder& CompositeBuilder::Selector() {
  children.emplace_back(std::make_unique<SelectorNode>());

  current_builder = std::make_unique<CompositeBuilder>(this, CompositeType::Selector);

  return *current_builder;
}

CompositeBuilder& CompositeBuilder::Parallel() {
  children.emplace_back(std::make_unique<ParallelNode>());

  current_builder = std::make_unique<CompositeBuilder>(this, CompositeType::Parallel);

  return *current_builder;
}

CompositeBuilder& CompositeBuilder::End() {
  if (parent) {
    CompositeNode* composite_node = (CompositeNode*)parent->children.back().get();

    for (auto& node : children) {
      composite_node->children_.emplace_back(std::move(node));
    }

    return *parent;
  }

  return *this;
}

CompositeBuilder& BehaviorBuilder::Sequence() {
  if (composite.GetType() != CompositeType::None) return composite;

  composite = CompositeBuilder(CompositeType::Sequence);
  return composite;
}

CompositeBuilder& BehaviorBuilder::Selector() {
  if (composite.GetType() != CompositeType::None) return composite;

  composite = CompositeBuilder(CompositeType::Selector);
  return composite;
}

CompositeBuilder& BehaviorBuilder::Parallel() {
  if (composite.GetType() != CompositeType::None) return composite;

  composite = CompositeBuilder(CompositeType::Parallel);
  return composite;
}

std::unique_ptr<BehaviorNode> BehaviorBuilder::Build() {
  auto& children = composite.GetChildren();

  std::unique_ptr<CompositeNode> composite_node;

  switch (composite.GetType()) {
    case CompositeType::Sequence: {
      composite_node = std::make_unique<SequenceNode>();
    } break;
    case CompositeType::Selector: {
      composite_node = std::make_unique<SelectorNode>();
    } break;
    case CompositeType::Parallel: {
      composite_node = std::make_unique<ParallelNode>();
    } break;
    case CompositeType::None: {
      return nullptr;
    } break;
  }

  for (auto& child : children) {
    composite_node->children_.emplace_back(std::move(child));
  }

  return composite_node;
}

}  // namespace behavior
}  // namespace zero

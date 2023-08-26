#include "BehaviorBuilder.h"

namespace zero {
namespace behavior {

CompositeBuilder& CompositeBuilder::Sequence(CompositeDecorator decorator) {
  switch (decorator) {
    case CompositeDecorator::None: {
      children.emplace_back(std::make_unique<SequenceNode>());
    } break;
    case CompositeDecorator::Success: {
      children.emplace_back(std::make_unique<SuccessNode>(std::make_unique<SequenceNode>()));
    } break;
    case CompositeDecorator::Invert: {
      children.emplace_back(std::make_unique<InvertNode>(std::make_unique<SequenceNode>()));
    } break;
  }

  current_builder = std::make_unique<CompositeBuilder>(this, CompositeType::Sequence, decorator);

  return *current_builder;
}

CompositeBuilder& CompositeBuilder::Selector(CompositeDecorator decorator) {
  switch (decorator) {
    case CompositeDecorator::None: {
      children.emplace_back(std::make_unique<SelectorNode>());
    } break;
    case CompositeDecorator::Success: {
      children.emplace_back(std::make_unique<SuccessNode>(std::make_unique<SelectorNode>()));
    } break;
    case CompositeDecorator::Invert: {
      children.emplace_back(std::make_unique<InvertNode>(std::make_unique<SelectorNode>()));
    } break;
  }

  current_builder = std::make_unique<CompositeBuilder>(this, CompositeType::Selector, decorator);

  return *current_builder;
}

CompositeBuilder& CompositeBuilder::Parallel(CompositeDecorator decorator) {
  switch (decorator) {
    case CompositeDecorator::None: {
      children.emplace_back(std::make_unique<ParallelNode>());
    } break;
    case CompositeDecorator::Success: {
      children.emplace_back(std::make_unique<SuccessNode>(std::make_unique<ParallelNode>()));
    } break;
    case CompositeDecorator::Invert: {
      children.emplace_back(std::make_unique<InvertNode>(std::make_unique<ParallelNode>()));
    } break;
  }

  current_builder = std::make_unique<CompositeBuilder>(this, CompositeType::Parallel, decorator);

  return *current_builder;
}

CompositeBuilder& CompositeBuilder::End() {
  if (parent) {
    CompositeNode* composite_node = (CompositeNode*)parent->children.back().get();

    if (decorator == CompositeDecorator::Success) {
      SuccessNode* success_node = (SuccessNode*)parent->children.back().get();
      composite_node = (CompositeNode*)success_node->child_.get();
    } else if (decorator == CompositeDecorator::Invert) {
      InvertNode* invert_node = (InvertNode*)parent->children.back().get();
      composite_node = (CompositeNode*)invert_node->child_.get();
    }

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

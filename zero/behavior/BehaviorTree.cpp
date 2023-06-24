#include "BehaviorTree.h"

namespace zero {
namespace behavior {

ExecuteResult SequenceNode::Execute(ExecuteContext& ctx) {
  std::size_t index = 0;

  if (running_node_index_ < children_.size()) {
    index = running_node_index_;
  }

  for (; index < children_.size(); ++index) {
    auto& node = children_[index];
    ExecuteResult result = node->Execute(ctx);

    if (result == ExecuteResult::Failure) {
      this->running_node_index_ = 0;
      return result;
    } else if (result == ExecuteResult::Running) {
      this->running_node_index_ = index;
      return result;
    }
  }

  this->running_node_index_ = 0;

  return ExecuteResult::Success;
}

ExecuteResult ParallelNode::Execute(ExecuteContext& ctx) {
  ExecuteResult result = ExecuteResult::Success;

  for (auto& child : children_) {
    ExecuteResult child_result = child->Execute(ctx);

    if (result == ExecuteResult::Success && child_result != ExecuteResult::Success) {
      // TODO: Implement failure policies
      // result = child_result;
    }
  }

  return result;
}

ExecuteResult SelectorNode::Execute(ExecuteContext& ctx) {
  ExecuteResult result = ExecuteResult::Failure;

  for (auto& child : children_) {
    ExecuteResult child_result = child->Execute(ctx);

    if (child_result == ExecuteResult::Running || child_result == ExecuteResult::Success) {
      return child_result;
    }
  }

  return result;
}

ExecuteResult SuccessNode::Execute(ExecuteContext& ctx) {
  if (!child_) return ExecuteResult::Failure;

  child_->Execute(ctx);
  return ExecuteResult::Success;
}

ExecuteResult InvertNode::Execute(ExecuteContext& ctx) {
  if (!child_) return ExecuteResult::Failure;

  ExecuteResult child_result = child_->Execute(ctx);

  if (child_result == ExecuteResult::Success) {
    return ExecuteResult::Failure;
  } else if (child_result == ExecuteResult::Failure) {
    return ExecuteResult::Success;
  }

  return child_result;
}

}  // namespace behavior
}  // namespace zero

#pragma once

#include <zero/behavior/Blackboard.h>

#include <cstdlib>
#include <memory>
#include <vector>

namespace zero {

struct ZeroBot;

namespace behavior {

enum class ExecuteResult { Success, Failure, Running };

struct ExecuteContext {
  Blackboard blackboard;
  ZeroBot* bot;
  float dt;

  ExecuteContext() : bot(nullptr), dt(0) {}
};

class BehaviorNode {
 public:
  virtual ExecuteResult Execute(ExecuteContext& ctx) = 0;
};

class CompositeNode : public BehaviorNode {
 public:
  std::vector<std::unique_ptr<BehaviorNode>> children_;
};

class SequenceNode : public CompositeNode {
 public:
  ExecuteResult Execute(ExecuteContext& ctx) override;

  SequenceNode& Child(std::unique_ptr<BehaviorNode> child) {
    children_.push_back(std::move(child));
    return *this;
  }

 private:
  std::size_t running_node_index_ = 0;
};

class ParallelNode : public CompositeNode {
 public:
  ExecuteResult Execute(ExecuteContext& ctx) override;

  ParallelNode& Child(std::unique_ptr<BehaviorNode> child) {
    children_.push_back(std::move(child));
    return *this;
  }
};

class SelectorNode : public CompositeNode {
 public:
  ExecuteResult Execute(ExecuteContext& ctx) override;

  SelectorNode& Child(std::unique_ptr<BehaviorNode> child) {
    children_.push_back(std::move(child));
    return *this;
  }
};

class SuccessNode : public BehaviorNode {
 public:
  SuccessNode() : child_(nullptr) {}
  SuccessNode(std::unique_ptr<BehaviorNode> child) : child_(std::move(child)) {}

  ExecuteResult Execute(ExecuteContext& ctx) override;

  void Child(std::unique_ptr<BehaviorNode> child) { child_ = std::move(child); }

  std::unique_ptr<BehaviorNode> child_;
};

class InvertNode : public BehaviorNode {
 public:
  InvertNode() : child_(nullptr) {}
  InvertNode(std::unique_ptr<BehaviorNode> child) : child_(std::move(child)) {}

  ExecuteResult Execute(ExecuteContext& ctx) override;

  void Child(std::unique_ptr<BehaviorNode> child) { child_ = std::move(child); }

  std::unique_ptr<BehaviorNode> child_;
};

}  // namespace behavior
}  // namespace zero

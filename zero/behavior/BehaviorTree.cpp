#include "BehaviorTree.h"

#include <Windows.h>

namespace zero {
namespace behavior {

TreePrinter* gDebugTreePrinter = nullptr;

static void ClearScreen() {
  static const HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

  CONSOLE_SCREEN_BUFFER_INFO csbi;
  COORD topLeft = {0, 0};

  fflush(stdout);

  if (!GetConsoleScreenBufferInfo(hOut, &csbi)) {
    return;
  }

  DWORD length = csbi.dwSize.X * csbi.dwSize.Y;
  DWORD written;

  // Flood-fill the console with spaces to clear it
  FillConsoleOutputCharacter(hOut, TEXT(' '), length, topLeft, &written);
  SetConsoleCursorPosition(hOut, topLeft);
}

void TreePrinter::Render(FILE* f) {
  ClearScreen();

  if (render_brackets) {
    --depth;
    int remaining_depth = depth;

    for (int i = 0; i <= remaining_depth; ++i) {
      Print("}");
      --depth;
    }
  }

  fprintf(f, "%s\n", output.data());
}

void Print(std::string_view str) {
  if (gDebugTreePrinter) {
    gDebugTreePrinter->Print(str);
  }
}

template <typename T>
static bool IsNodeType(std::unique_ptr<BehaviorNode>& node) {
  return dynamic_cast<T*>(node.get());
}

void Print(std::unique_ptr<BehaviorNode>& node) {
  if (gDebugTreePrinter) {
    if (IsNodeType<SequenceNode>(node)) return;
    if (IsNodeType<ParallelNode>(node)) return;
    if (IsNodeType<SelectorNode>(node)) return;
    if (IsNodeType<SuccessNode>(node)) return;
    if (IsNodeType<InvertNode>(node)) return;

    gDebugTreePrinter->Print(typeid(*node.get()).name());
  }
}

void DepthIncrease() {
  if (gDebugTreePrinter) {
    if (gDebugTreePrinter->render_brackets) {
      Print("{");
    }
    ++gDebugTreePrinter->depth;
  }
}

void DepthDecrease() {
  if (gDebugTreePrinter) {
    --gDebugTreePrinter->depth;

    if (gDebugTreePrinter->render_brackets) {
      Print("}");
    }
  }
}

ExecuteResult SequenceNode::Execute(ExecuteContext& ctx) {
  std::size_t index = 0;

  if (running_node_index_ < children_.size()) {
    index = running_node_index_;
  }

  Print("Sequence");
  DepthIncrease();

  for (; index < children_.size(); ++index) {
    auto& node = children_[index];

    Print(node);

    ExecuteResult result = node->Execute(ctx);

    if (result == ExecuteResult::Failure) {
      this->running_node_index_ = 0;
      return result;
    } else if (result == ExecuteResult::Running) {
      this->running_node_index_ = index;
      return result;
    }
  }

  DepthDecrease();

  this->running_node_index_ = 0;

  return ExecuteResult::Success;
}

ExecuteResult ParallelNode::Execute(ExecuteContext& ctx) {
  ExecuteResult result = ExecuteResult::Success;

  Print("Parallel");
  DepthIncrease();

  for (auto& child : children_) {
    Print(child);

    ExecuteResult child_result = child->Execute(ctx);

    if (result == ExecuteResult::Success && child_result != ExecuteResult::Success) {
      // TODO: Implement failure policies
      result = child_result;
    }
  }

  DepthDecrease();

  return result;
}

ExecuteResult SelectorNode::Execute(ExecuteContext& ctx) {
  ExecuteResult result = ExecuteResult::Failure;

  Print("Selector");
  DepthIncrease();

  for (auto& child : children_) {
    Print(child);

    ExecuteResult child_result = child->Execute(ctx);

    if (child_result == ExecuteResult::Running || child_result == ExecuteResult::Success) {
      return child_result;
    }
  }

  DepthDecrease();

  return result;
}

ExecuteResult SuccessNode::Execute(ExecuteContext& ctx) {
  if (!child_) return ExecuteResult::Failure;

  Print("Success");
  DepthIncrease();
  Print(child_);

  child_->Execute(ctx);

  DepthDecrease();

  return ExecuteResult::Success;
}

ExecuteResult InvertNode::Execute(ExecuteContext& ctx) {
  if (!child_) return ExecuteResult::Failure;

  Print("Invert");
  DepthIncrease();
  Print(child_);

  ExecuteResult child_result = child_->Execute(ctx);

  DepthDecrease();

  if (child_result == ExecuteResult::Success) {
    return ExecuteResult::Failure;
  } else if (child_result == ExecuteResult::Failure) {
    return ExecuteResult::Success;
  }

  return child_result;
}

}  // namespace behavior
}  // namespace zero

#pragma once

#include <zero/behavior/Behavior.h>
#include <zero/behavior/BehaviorBuilder.h>

namespace zero {
namespace tw {

std::unique_ptr<behavior::BehaviorNode> CreateSpiderTree(behavior::ExecuteContext& ctx);

}  // namespace tw
}  // namespace zero

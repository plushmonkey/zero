#pragma once

#include <zero/behavior/Behavior.h>
#include <zero/zones/devastation/BaseManager.h>

namespace zero {
namespace deva {

struct GetSpawnNode : public behavior::BehaviorNode {
  enum class Type { Team, Enemy };

  GetSpawnNode(Type type, const char* output_key) : type(type), output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto bm_opt = ctx.blackboard.Value<BaseManager*>("base_manager");
    if (!bm_opt) return behavior::ExecuteResult::Failure;

    BaseManager* bm = *bm_opt;
    if (!bm) return behavior::ExecuteResult::Failure;

    if (bm->current_base.region == kUndefinedRegion) return behavior::ExecuteResult::Failure;

    Vector2f coord;

    if (type == Type::Team) {
      coord = bm->current_base.spawn.ToVector();
    } else {
      coord = bm->current_base.enemy_spawn.ToVector();
    }

    ctx.blackboard.Set(output_key, coord);

    return behavior::ExecuteResult::Success;
  }

  const char* output_key = nullptr;
  Type type = Type::Team;
};

}  // namespace deva
}  // namespace zero

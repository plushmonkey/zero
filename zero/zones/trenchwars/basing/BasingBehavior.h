#pragma once

#include <zero/Math.h>
#include <zero/behavior/Behavior.h>
#include <zero/behavior/BehaviorBuilder.h>
#include <zero/zones/trenchwars/TrenchWars.h>

namespace zero {
namespace tw {

struct BasingBehavior : public behavior::Behavior {
  virtual void OnInitialize(behavior::ExecuteContext& ctx) override {
    // Setup blackboard here for this specific behavior

    // Only set request ship if we don't already have one.
    // This lets us retain our ship when switching between behaviors.
    if (!ctx.blackboard.Has("request_ship")) {
      ctx.blackboard.Set("request_ship", 2);
    }

    ctx.blackboard.Set("leash_distance", 35.0f);

    auto opt_tw = ctx.blackboard.Value<TrenchWars*>("tw");
    if (opt_tw) {
      TrenchWars* tw = *opt_tw;
      Vector2f flag_pos = tw->flag_position;

      // TODO: Should find empty spaces in base instead of hoping they are traversable.
      std::vector<Vector2f> waypoints{
          flag_pos + Vector2f(-17, 6),
          flag_pos + Vector2f(-7, -6),

          flag_pos + Vector2f(18, -4),
          flag_pos + Vector2f(13, 11),
      };

      ctx.blackboard.Set("waypoints", waypoints);
    }
  }

  std::unique_ptr<behavior::BehaviorNode> CreateTree(behavior::ExecuteContext& ctx) override;
};

std::unique_ptr<behavior::BehaviorNode> CreateJavelinBasingTree(behavior::ExecuteContext& ctx);
std::unique_ptr<behavior::BehaviorNode> CreateSpiderBasingTree(behavior::ExecuteContext& ctx);
std::unique_ptr<behavior::BehaviorNode> CreateTerrierBasingTree(behavior::ExecuteContext& ctx);
std::unique_ptr<behavior::BehaviorNode> CreateSharkBasingTree(behavior::ExecuteContext& ctx);

}  // namespace tw
}  // namespace zero

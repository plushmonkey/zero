#pragma once

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/Behavior.h>
#include <zero/behavior/nodes/MoveNode.h>
#include <zero/zones/hyperspace/Hyperspace.h>

namespace zero {
namespace hyperspace {

struct GlobalGoToNode : public behavior::BehaviorNode {
  GlobalGoToNode(const char* position_key) : position_key(position_key) {}
  GlobalGoToNode(Vector2f position) : position(position), position_key(nullptr) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    Player* self = ctx.bot->game->player_manager.GetSelf();
    if (!self) return behavior::ExecuteResult::Failure;

    Vector2f target = position;

    if (position_key) {
      auto opt_pos = ctx.blackboard.Value<Vector2f>(position_key);
      if (!opt_pos.has_value()) return behavior::ExecuteResult::Failure;

      target = opt_pos.value();
    }

    int self_sector = GetSectorFromPosition(*ctx.bot->bot_controller->region_registry, self->position);
    int target_sector = GetSectorFromPosition(*ctx.bot->bot_controller->region_registry, target);

    if (self_sector == -1 || target_sector == -1) {
      return behavior::ExecuteResult::Failure;
    }

    // If we are in the same sector then just use the normal goto node.
    if (self_sector == target_sector) {
      behavior::GoToNode goto_node(target);
      return goto_node.Execute(ctx);
    }

    // If we are in the warp ring, find the first position in the warp connections that leads to our destination.
    if (self_sector == kWarpRingSectorIndex) {
      // Special case center target so we can find the closest of the two warpers to it.
      if (target_sector == kCenterSectorIndex) {
        float dist_sq_0 = kWarpgateConnections[8][0].DistanceSq(self->position);
        float dist_sq_1 = kWarpgateConnections[9][0].DistanceSq(self->position);

        Vector2f target = kWarpgateConnections[8][0];
        if (dist_sq_1 < dist_sq_0) {
          target = kWarpgateConnections[9][0];
        }

        behavior::GoToNode goto_node(target);
        return goto_node.Execute(ctx);
      }

      behavior::GoToNode goto_node(kWarpgateConnections[target_sector][0]);
      return goto_node.Execute(ctx);
    } else if (self_sector == kCenterSectorIndex) {
      if (target_sector < 8) {
        // Our target is not the warp ring, so pretend it is, then set out target position to the warper position to
        // find the closest one.
        target = kWarpgateConnections[target_sector][0];
        target_sector = kWarpRingSectorIndex;
      }

      // If our target is the warp ring, then find the closest outside position to the target.
      if (target_sector == kWarpRingSectorIndex) {
        float dist_sq_0 = kWarpgateConnections[8][0].DistanceSq(target);
        float dist_sq_1 = kWarpgateConnections[9][0].DistanceSq(target);

        Vector2f target = kWarpgateConnections[8][1];
        if (dist_sq_1 < dist_sq_0) {
          target = kWarpgateConnections[9][1];
        }

        behavior::GoToNode goto_node(target);
        return goto_node.Execute(ctx);
      }
    } else if (self_sector >= 0 && self_sector < 8) {
      // If we are in one of the 1-1 sectors, then just find the warper out.
      Vector2f target = kWarpgateConnections[self_sector][1];

      behavior::GoToNode goto_node(target);
      return goto_node.Execute(ctx);
    }

    return behavior::ExecuteResult::Failure;
  }

  Vector2f position;
  const char* position_key = nullptr;
};

}  // namespace hyperspace
}  // namespace zero

#pragma once

#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/behavior/nodes/AttachNode.h>
#include <zero/behavior/nodes/MapNode.h>
#include <zero/behavior/nodes/TimerNode.h>
#include <zero/zones/trenchwars/TrenchWars.h>

namespace zero {
namespace tw {

struct BestAttachQueryNode : public behavior::BehaviorNode {
  BestAttachQueryNode(const char* output_key) : output_key(output_key) {}
  BestAttachQueryNode(bool require_closer_than_self, const char* output_key)
      : require_closer_than_self(require_closer_than_self), output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto& pm = ctx.bot->game->player_manager;
    auto self = pm.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    Player* best_player = nullptr;
    float best_dist_sq = 1024.0f * 1024.0f;

    Vector2f flag_position = ctx.blackboard.ValueOr("tw_flag_position", Vector2f(512, 269));
    float self_dist_sq = self->position.DistanceSq(flag_position);

    // Loop over players to find a teammate that can be attached to that is closer to the flag room than us.
    for (size_t i = 0; i < pm.player_count; ++i) {
      Player* player = pm.players + i;

      if (player->id == self->id) continue;
      if (player->ship >= 8) continue;
      if (player->frequency != self->frequency) continue;
      if (require_closer_than_self && player->position.DistanceSq(flag_position) > self_dist_sq) continue;

      u8 turret_limit = ctx.bot->game->connection.settings.ShipSettings[player->ship].TurretLimit;

      if (turret_limit == 0) continue;

      u8 turret_count = 0;

      AttachInfo* child = player->children;
      while (child) {
        ++turret_count;
        child = child->next;
      }

      if (turret_count >= turret_limit) continue;

      float dist_sq = flag_position.DistanceSq(player->position);
      if (dist_sq < best_dist_sq) {
        best_dist_sq = dist_sq;
        best_player = player;
      }
    }

    if (!best_player) return behavior::ExecuteResult::Failure;

    ctx.blackboard.Set<Player*>(output_key, best_player);

    return behavior::ExecuteResult::Success;
  }

  bool require_closer_than_self = true;
  const char* output_key = nullptr;
};

struct AttachParentValidNode : public behavior::BehaviorNode {
  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    PlayerId parent_id = self->attach_parent;
    if (parent_id == kInvalidPlayerId) return behavior::ExecuteResult::Failure;

    Player* parent = ctx.bot->game->player_manager.GetPlayerById(parent_id);
    if (parent == nullptr || parent->ship >= 8) return behavior::ExecuteResult::Failure;

    constexpr float kMinimumSpeed = 0.1f;

    if (parent->velocity.LengthSq() <= kMinimumSpeed * kMinimumSpeed) return behavior::ExecuteResult::Failure;

    return behavior::ExecuteResult::Success;
  }
};

inline std::unique_ptr<behavior::BehaviorNode> CreateBaseAttachTree(const char* self_key) {
  using namespace behavior;

  BehaviorBuilder builder;

  constexpr u32 kAttachCooldown = 100;
  constexpr u32 kDetachTimeout = 2000;
  constexpr float kNearFlagroomDistance = 50.0f;

  // clang-format off
  builder
    .Selector()
        .Sequence() // Attach to teammate if possible
            .InvertChild<AttachedQueryNode>(self_key)
            .Child<DistanceThresholdNode>("tw_flag_position", kNearFlagroomDistance)
            .Child<TimerExpiredNode>("attach_cooldown")
            .Child<BestAttachQueryNode>("best_attach_player")
            .Child<AttachNode>("best_attach_player")
            .Child<TimerSetNode>("attach_cooldown", kAttachCooldown)
            .Child<TimerSetNode>("detach_timeout", kDetachTimeout)
            .End()
        .Sequence() // Detach after cooldown
            .Child<AttachedQueryNode>(self_key)
#if 0
            .Selector() // Detach if we are close to flagroom or our parent ended up being a bad attach target.
                .InvertChild<DistanceThresholdNode>("tw_flag_position", kNearFlagroomDistance)
                .InvertChild<AttachParentValidNode>()
                .Child<TimerExpiredNode>("detach_timeout")
                .End()
#endif
            .Child<TimerExpiredNode>("attach_cooldown")
            .Child<DetachNode>()
            .Child<TimerSetNode>("attach_cooldown", kAttachCooldown)
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

}  // namespace tw
}  // namespace zero

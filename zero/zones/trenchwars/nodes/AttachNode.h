#pragma once

#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/behavior/nodes/AttachNode.h>
#include <zero/behavior/nodes/MapNode.h>
#include <zero/behavior/nodes/PlayerNode.h>
#include <zero/behavior/nodes/TimerNode.h>
#include <zero/zones/trenchwars/TrenchWars.h>

namespace zero {
namespace tw {

struct BestAttachQueryNode : public behavior::BehaviorNode {
  enum class Filter {
    Any,
    BaseAndAbove,
  };

  BestAttachQueryNode(Filter filter, const char* output_key) : filter(filter), output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto& pm = ctx.bot->game->player_manager;
    auto self = pm.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    auto opt_tw = ctx.blackboard.Value<TrenchWars*>("tw");
    if (!opt_tw) return behavior::ExecuteResult::Failure;

    TrenchWars* tw = *opt_tw;

    Player* best_player = nullptr;
    float best_dist_sq = 1024.0f * 1024.0f;

    Vector2f flag_position = ctx.blackboard.ValueOr("tw_flag_position", Vector2f(512, 269));

    Game& game = *ctx.bot->game;

    // Loop over players to find a teammate that can be attached to that is closer to the flag room than us.
    for (size_t i = 0; i < pm.player_count; ++i) {
      Player* player = pm.players + i;

      if (player->id == self->id) continue;
      if (player->ship >= 8) continue;
      if (player->frequency != self->frequency) continue;

      if (filter == Filter::BaseAndAbove) {
        Sector sector = tw->GetSector(player->position);

        // Never attach to center and roof players.
        if (sector == Sector::Center || sector == Sector::Roof) continue;

        bool self_in_base = tw->base_bitset.Test(self->position);
        Sector self_sector = tw->GetSector(self->position);

        // If we're inside the base, filter out everyone equal and below us.
        if (self_in_base && !IsSectorAbove(sector, self_sector)) {
          continue;
        }
      }

      if (!CanAttachTo(game, *player)) continue;

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

  inline bool CanAttachTo(Game& game, Player& target) {
    u8 turret_limit = game.connection.settings.ShipSettings[target.ship].TurretLimit;

    if (turret_limit == 0) return false;

    u8 turret_count = 0;

    AttachInfo* child = target.children;
    while (child) {
      ++turret_count;
      child = child->next;
    }

    return turret_count < turret_limit;
  }

  Filter filter = Filter::Any;
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

inline std::unique_ptr<behavior::BehaviorNode> CreateBaseAttachTree(float distance_threshold) {
  using namespace behavior;

  BehaviorBuilder builder;

  constexpr u32 kAttachCooldown = 100;
  constexpr u32 kDetachTimeout = 2000;

  // clang-format off
  builder
    .Selector()
        .Sequence() // Attach to teammate if possible
            .InvertChild<AttachedQueryNode>()
            .Child<TimerExpiredNode>("attach_cooldown")
            .Child<BestAttachQueryNode>(BestAttachQueryNode::Filter::BaseAndAbove, "best_attach_player")
            .Child<PlayerPositionQueryNode>("best_attach_player", "best_attach_player_position")
            .Child<DistanceThresholdNode>("best_attach_player_position", distance_threshold)
            .Child<AttachNode>("best_attach_player")
            .Child<TimerSetNode>("attach_cooldown", kAttachCooldown)
            .End()
        .Sequence() // Detach after cooldown
            .Child<AttachedQueryNode>()
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

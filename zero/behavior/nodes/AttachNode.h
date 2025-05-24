#pragma once

#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/behavior/nodes/AimNode.h>
#include <zero/game/Game.h>

namespace zero {
namespace behavior {

// Returns success if the player selected by the player key is attached to another player.
struct AttachedQueryNode : public BehaviorNode {
  AttachedQueryNode() : player_key(nullptr) {}
  AttachedQueryNode(const char* player_key) : player_key(player_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    Player* target = ctx.bot->game->player_manager.GetSelf();

    if (player_key) {
      auto opt_player = ctx.blackboard.Value<Player*>(player_key);
      if (!opt_player) return ExecuteResult::Failure;
      target = *opt_player;
    }

    if (!target) return ExecuteResult::Failure;

    // Don't consider us as attached when it hasn't been confirmed by server yet.
    if (target->id == ctx.bot->game->player_manager.player_id && ctx.bot->game->player_manager.requesting_attach) {
      return ExecuteResult::Failure;
    }

    return target->attach_parent == kInvalidPlayerId ? ExecuteResult::Failure : ExecuteResult::Success;
  }

  const char* player_key = nullptr;
};

// Sends an attach request to a target player.
// This should be put behind a timer so it doesn't spam.
struct AttachNode : public BehaviorNode {
  AttachNode(const char* target_player_key) : target_player_key(target_player_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    if (!target_player_key) return ExecuteResult::Failure;

    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self) return ExecuteResult::Failure;

    auto opt_player = ctx.blackboard.Value<Player*>(target_player_key);
    if (!opt_player) return ExecuteResult::Failure;

    AttachRequestResponse response = ctx.bot->game->player_manager.AttachSelf(*opt_player);
    if (response == AttachRequestResponse::Success) {
      return ExecuteResult::Success;
    }

    return ExecuteResult::Failure;
  }

  const char* target_player_key = nullptr;
};

struct DetachNode : public BehaviorNode {
  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self) return ExecuteResult::Failure;

    if (self->attach_parent == kInvalidPlayerId) return ExecuteResult::Failure;

    ctx.bot->game->player_manager.DetachPlayer(*self);

    return ExecuteResult::Success;
  }
};

// Returns the number of players attached to the carrier including the carrier.
// This can be used on any of the children to get the same result.
struct TurretCountQueryNode : behavior::BehaviorNode {
  TurretCountQueryNode(const char* output_key) : output_key(output_key) {}
  TurretCountQueryNode(const char* player_key, const char* output_key)
      : player_key(player_key), output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    Player* player = ctx.bot->game->player_manager.GetSelf();

    if (player_key) {
      auto opt_player = ctx.blackboard.Value<Player*>(player_key);
      if (!opt_player) return behavior::ExecuteResult::Failure;
      player = *opt_player;
    }

    if (!player || player->ship >= 8) return behavior::ExecuteResult::Failure;

    size_t count = 1;
    Player* carrier = player;

    if (player->attach_parent != kInvalidPlayerId) {
      carrier = ctx.bot->game->player_manager.GetPlayerById(player->attach_parent);
    }

    if (!carrier || carrier->ship >= 8) return behavior::ExecuteResult::Failure;

    AttachInfo* attach_info = carrier->children;
    while (attach_info) {
      ++count;
      attach_info = attach_info->next;
    }

    ctx.blackboard.Set<size_t>(output_key, count);

    return behavior::ExecuteResult::Success;
  }

  const char* player_key = nullptr;
  const char* output_key = nullptr;
};

}  // namespace behavior
}  // namespace zero

#pragma once

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

namespace zero {
namespace behavior {

struct PowerballFireNode : public BehaviorNode {
  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto& soccer = ctx.bot->game->soccer;

    soccer.FireBall(BallFireMethod::Warp);

    return ExecuteResult::Success;
  }
};

struct PowerballRemainingTimeQueryNode : public BehaviorNode {
  PowerballRemainingTimeQueryNode(const char* output_key) : output_key(output_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto player = ctx.bot->game->player_manager.GetSelf();
    if (!player || player->ship >= 8) return ExecuteResult::Failure;

    auto& soccer = ctx.bot->game->soccer;

    if (soccer.carry_id == kInvalidBallId) return ExecuteResult::Failure;

    ctx.blackboard.Set(output_key, soccer.carry_timer);

    return ExecuteResult::Success;
  }

  const char* output_key = nullptr;
};

struct PowerballCarryQueryNode : public BehaviorNode {
  PowerballCarryQueryNode() {}
  PowerballCarryQueryNode(const char* player_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto player = ctx.bot->game->player_manager.GetSelf();

    if (player_key) {
      auto opt_player = ctx.blackboard.Value<Player*>(player_key);
      if (!opt_player.has_value()) return ExecuteResult::Failure;

      player = opt_player.value();
    }

    if (!player) return ExecuteResult::Failure;

    return player->ball_carrier ? ExecuteResult::Success : ExecuteResult::Failure;
  }

  const char* player_key = nullptr;
};

struct PowerballClosestQueryNode : public BehaviorNode {
  PowerballClosestQueryNode(const char* output_position_key, bool include_carried = true)
      : output_position_key(output_position_key) {}

  PowerballClosestQueryNode(const char* player_key, const char* output_position_key, bool include_carried = true)
      : player_key(player_key), output_position_key(output_position_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto player = ctx.bot->game->player_manager.GetSelf();

    if (player_key) {
      auto opt_player = ctx.blackboard.Value<Player*>(player_key);
      if (!opt_player.has_value()) return ExecuteResult::Failure;

      player = opt_player.value();
    }

    if (!player) return ExecuteResult::Failure;

    Vector2f best_position;

    if (!GetClosestBall(ctx.bot->game->soccer, *player, &best_position)) {
      // No balls exist, so return failure.
      return ExecuteResult::Failure;
    }

    ctx.blackboard.Set(output_position_key, best_position);

    return ExecuteResult::Success;
  }

  bool GetClosestBall(Soccer& soccer, Player& player, Vector2f* best_position) {
    float best_dist_sq = 1025.0f * 1025.0f;
    bool ball_exists = false;

    for (size_t i = 0; i < ZERO_ARRAY_SIZE(soccer.balls); ++i) {
      Powerball* ball = soccer.balls + i;

      if (ball->id == kInvalidBallId) continue;

      ball_exists = true;

      Vector2f position = soccer.GetBallPosition(*ball, GetMicrosecondTick());

      float dist_sq = position.DistanceSq(player.position);
      if (dist_sq < best_dist_sq) {
        best_dist_sq = dist_sq;
        *best_position = position;
      }
    }

    return ball_exists;
  }

  const char* player_key = nullptr;
  const char* output_position_key = nullptr;
};

}  // namespace behavior
}  // namespace zero

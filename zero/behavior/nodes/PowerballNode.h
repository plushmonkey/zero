#pragma once

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

namespace zero {
namespace behavior {

// This node is very computationally expensive since it does a full simulation of the ball trajectory.
// Returns success if carrying the ball and pointing in a direction that will fire into a goal.
struct PowerballGoalPathQuery : public BehaviorNode {
  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto& soccer = ctx.bot->game->soccer;

    if (soccer.carry_id == kInvalidBallId) return ExecuteResult::Failure;

    auto player = ctx.bot->game->player_manager.GetSelf();
    if (!player || player->ship >= 8) return ExecuteResult::Failure;

    Powerball* ball = GetBallById(soccer, soccer.carry_id);
    if (!ball) return ExecuteResult::Failure;

    Powerball projected_ball = *ball;

    float speed = ctx.bot->game->connection.settings.ShipSettings[player->ship].SoccerBallSpeed / 10.0f / 16.0f;
    Vector2f position = player->position;
    Vector2f heading = OrientationToHeading((u8)(player->orientation * 40.0f));
    Vector2f velocity = player->velocity + heading * speed;

    projected_ball.x = (u32)(position.x * 16000.0f);
    projected_ball.y = (u32)(position.y * 16000.0f);
    projected_ball.vel_x = (s32)(velocity.x * 16.0f * 10.0f);
    projected_ball.vel_y = (s32)(velocity.y * 16.0f * 10.0f);

    projected_ball.friction = 1000000;
    projected_ball.friction_delta = ctx.bot->game->connection.settings.ShipSettings[player->ship].SoccerBallFriction;

    auto& map = ctx.bot->game->GetMap();

    for (size_t i = 0; i < 1000000; ++i) {
      SimulateAxis(soccer, projected_ball, map, &projected_ball.x, &projected_ball.vel_x);
      bool hit_goal = SimulateAxis(soccer, projected_ball, map, &projected_ball.y, &projected_ball.vel_y);

      if (hit_goal) {
        return ExecuteResult::Success;
      }

      s32 friction = projected_ball.friction / 1000;
      projected_ball.vel_x = (projected_ball.vel_x * friction) / 1000;
      projected_ball.vel_y = (projected_ball.vel_y * friction) / 1000;

      projected_ball.friction -= projected_ball.friction_delta;

      if (projected_ball.friction <= 0) {
        break;
      }
    }

    return ExecuteResult::Failure;
  }

  inline bool SimulateAxis(Soccer& soccer, Powerball& ball, Map& map, u32* pos, s16* vel) {
    u32 previous = *pos;

    *pos += *vel;

    float x = floorf(ball.x / 16000.0f);
    float y = floorf(ball.y / 16000.0f);

    if (map.IsSolid((u16)x, (u16)y, ball.frequency)) {
      *pos = previous;
      *vel = -*vel;
    }

    if (map.GetTileId((u16)x, (u16)y) == 172) {
      return !soccer.IsTeamGoal(Vector2f(x, y));
    }

    return false;
  }

  Powerball* GetBallById(Soccer& soccer, u16 id) {
    for (size_t i = 0; i < ZERO_ARRAY_SIZE(soccer.balls); ++i) {
      if (soccer.balls[i].id == id) {
        return soccer.balls + i;
      }
    }

    return nullptr;
  }
};
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

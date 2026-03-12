#pragma once

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>
#include <zero/game/Soccer.h>
#include <zero/zones/hockey/HockeyZone.h>

namespace zero {
namespace hz {

struct PowerballCarrierQueryNode : public behavior::BehaviorNode {
  PowerballCarrierQueryNode(const char* ball_key, const char* output_key)
      : ball_key(ball_key), output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto opt_ball = ctx.blackboard.Value<Powerball*>(ball_key);
    if (!opt_ball) return behavior::ExecuteResult::Failure;

    Powerball* ball = *opt_ball;
    if (!ball) return behavior::ExecuteResult::Failure;

    if (ball->state != BallState::Carried) return behavior::ExecuteResult::Failure;

    Player* carrier = ctx.bot->game->player_manager.GetPlayerById(ball->carrier_id);

    if (!carrier || carrier->ship >= 8) return behavior::ExecuteResult::Failure;

    ctx.blackboard.Set(output_key, carrier);

    return behavior::ExecuteResult::Success;
  }

  const char* ball_key = nullptr;
  const char* output_key = nullptr;
};
struct PowerballPositionQueryNode : public behavior::BehaviorNode {
  PowerballPositionQueryNode(const char* ball_key, const char* output_key)
      : ball_key(ball_key), output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto opt_ball = ctx.blackboard.Value<Powerball*>(ball_key);
    if (!opt_ball) return behavior::ExecuteResult::Failure;

    Powerball* ball = *opt_ball;
    if (!ball) return behavior::ExecuteResult::Failure;

    Vector2f position = ctx.bot->game->soccer.GetBallPosition(*ball, GetMicrosecondTick());

    ctx.blackboard.Set(output_key, position);

    return behavior::ExecuteResult::Success;
  }

  const char* ball_key = nullptr;
  const char* output_key = nullptr;
};

struct PowerballTeamOwnedNode : public behavior::BehaviorNode {
  PowerballTeamOwnedNode(const char* ball_key) : ball_key(ball_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();

    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    auto opt_ball = ctx.blackboard.Value<Powerball*>(ball_key);
    if (!opt_ball) return behavior::ExecuteResult::Failure;

    Powerball* ball = *opt_ball;
    if (!ball) return behavior::ExecuteResult::Failure;

    if (ball->state == BallState::World && ball->frequency == self->frequency) {
      return behavior::ExecuteResult::Success;
    }

    if (ball->state == BallState::Carried) {
      Player* carrier = ctx.bot->game->player_manager.GetPlayerById(ball->carrier_id);
      if (carrier && carrier->ship < 8 && carrier->frequency == self->frequency) {
        return behavior::ExecuteResult::Success;
      }
    }

    return behavior::ExecuteResult::Failure;
  }

  const char* ball_key = nullptr;
};

// Finds the closest uncarried ball that's within the current rink.
struct GetNearestBallNode : public behavior::BehaviorNode {
  GetNearestBallNode(const char* output_key, BallState required_state)
      : output_key(output_key), required_state(required_state) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    Player* self = ctx.bot->game->player_manager.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    auto opt_hz = ctx.blackboard.Value<HockeyZone*>("hz");
    if (!opt_hz) return behavior::ExecuteResult::Failure;

    HockeyZone* hz = *opt_hz;

    Rink* rink = hz->GetRinkFromTile(self->position);
    if (!rink) return behavior::ExecuteResult::Failure;

    Soccer& soccer = ctx.bot->game->soccer;

    for (size_t i = 0; i < ZERO_ARRAY_SIZE(soccer.balls); ++i) {
      Powerball* ball = soccer.balls + i;

      if (ball->id == kInvalidBallId) continue;

      Vector2f position = soccer.GetBallPosition(*ball, GetMicrosecondTick());
      RegionIndex ball_region_index =
          ctx.bot->bot_controller->region_registry->GetRegionIndex(MapCoord((u16)position.x, (u16)position.y));

      if (ball_region_index != rink->region_index) continue;
      if (ball->state == required_state) {
        ctx.blackboard.Set(output_key, ball);
        return behavior::ExecuteResult::Success;
      }
    }

    return behavior::ExecuteResult::Failure;
  }

  const char* output_key = nullptr;
  BallState required_state = BallState::Carried;
};

}  // namespace hz
}  // namespace zero
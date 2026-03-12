#pragma once

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>
#include <zero/zones/hockey/HockeyZone.h>

namespace zero {
namespace hz {

struct RinkCenterNode : public behavior::BehaviorNode {
  RinkCenterNode(const char* output_key) : output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    auto opt_hz = ctx.blackboard.Value<HockeyZone*>("hz");
    if (!opt_hz) return behavior::ExecuteResult::Failure;

    HockeyZone* hz = *opt_hz;

    Rink* rink = hz->GetRinkFromTile(self->position);
    if (!rink) return behavior::ExecuteResult::Failure;

    Vector2f center = rink->center;

    if (IsRinkClosed(ctx.bot->game->GetMap(), *rink)) {
      if (self->position.x < rink->center.x) {
        center = (rink->west_goal_rect.min + rink->west_goal_rect.max) * 0.5f + Vector2f(20.0f, 0);
      } else {
        center = (rink->east_goal_rect.min + rink->east_goal_rect.max) * 0.5f + Vector2f(-20.0f, 0);
      }
    }

    ctx.blackboard.Set(output_key, center);

    return behavior::ExecuteResult::Success;
  }

  const char* output_key = nullptr;
};

struct RectangleCenterNode : public behavior::BehaviorNode {
  RectangleCenterNode(const char* rect_key, const char* output_key) : rect_key(rect_key), output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto opt_rect = ctx.blackboard.Value<Rectangle>(rect_key);
    if (!opt_rect) return behavior::ExecuteResult::Failure;

    Rectangle rect = *opt_rect;
    Vector2f center = (rect.min + rect.max) * 0.5f;

    ctx.blackboard.Set(output_key, center);

    return behavior::ExecuteResult::Success;
  }

  const char* rect_key = nullptr;
  const char* output_key = nullptr;
};

struct FindEnemyGoalRectNode : public behavior::BehaviorNode {
  FindEnemyGoalRectNode(const char* output_key) : output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    Player* self = ctx.bot->game->player_manager.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    auto opt_hz = ctx.blackboard.Value<HockeyZone*>("hz");
    if (!opt_hz) return behavior::ExecuteResult::Failure;

    HockeyZone* hz = *opt_hz;

    Rink* rink = hz->GetRinkFromTile(self->position);
    if (!rink) return behavior::ExecuteResult::Failure;

    Map& map = ctx.bot->game->GetMap();

    if (IsRinkClosed(map, *rink)) {
      // Determine which side of the rink we're on if the doors are closed because we can only use that goal.
      if (self->position.x < rink->center.x) {
        ctx.blackboard.Set(output_key, rink->west_goal_rect);
      } else {
        ctx.blackboard.Set(output_key, rink->east_goal_rect);
      }
    } else {
      if (self->frequency & 1) {
        ctx.blackboard.Set(output_key, rink->west_goal_rect);
      } else {
        ctx.blackboard.Set(output_key, rink->east_goal_rect);
      }
    }

    return behavior::ExecuteResult::Success;
  }

  const char* output_key = nullptr;
};

struct FindTeamGoalRectNode : public behavior::BehaviorNode {
  FindTeamGoalRectNode(const char* output_key) : output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    Player* self = ctx.bot->game->player_manager.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    auto opt_hz = ctx.blackboard.Value<HockeyZone*>("hz");
    if (!opt_hz) return behavior::ExecuteResult::Failure;

    HockeyZone* hz = *opt_hz;

    Rink* rink = hz->GetRinkFromTile(self->position);
    if (!rink) return behavior::ExecuteResult::Failure;

    Map& map = ctx.bot->game->GetMap();

    if (IsRinkClosed(map, *rink)) {
      // Determine which side of the rink we're on if the doors are closed because we can only use that goal.
      if (self->position.x < rink->center.x) {
        ctx.blackboard.Set(output_key, rink->west_goal_rect);
      } else {
        ctx.blackboard.Set(output_key, rink->east_goal_rect);
      }
    } else {
      if (self->frequency & 1) {
        ctx.blackboard.Set(output_key, rink->east_goal_rect);
      } else {
        ctx.blackboard.Set(output_key, rink->west_goal_rect);
      }
    }

    return behavior::ExecuteResult::Success;
  }

  const char* output_key = nullptr;
};

}  // namespace hz
}  // namespace zero
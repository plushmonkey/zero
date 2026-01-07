#include "MoveNode.h"

namespace zero {
namespace behavior {

ExecuteResult FollowPathNode::Execute(ExecuteContext& ctx) {
  Player* self = ctx.bot->game->player_manager.GetSelf();
  if (!self || self->ship >= 8) return ExecuteResult::Failure;

  auto& map = ctx.bot->game->GetMap();
  auto& current_path = ctx.bot->bot_controller->current_path;
  auto& pathfinder = ctx.bot->bot_controller->pathfinder;
  auto& game = *ctx.bot->game;
  float radius = game.connection.settings.ShipSettings[self->ship].GetRadius();

  if (current_path.Empty()) {
    return ExecuteResult::Failure;
  }

  if (current_path.IsDone()) {
    return ExecuteResult::Success;
  }

  Vector2f movement_target = current_path.GetCurrent();

  // Cull future nodes if they are all unobstructed from current position.
  while (!current_path.IsOnGoalNode()) {
    Vector2f next = current_path.GetNext();

    CastResult cast = map.CastShip(self, radius, next);
    if (cast.hit) {
      break;
    }

    // Reset the stuck counter when we successfully move to a new node.
    ctx.blackboard.Set("bounce_count", 0U);
    movement_target = current_path.Advance();
  }

  if (current_path.IsDone()) {
    current_path.Clear();
  }

  if (current_path.IsOnGoalNode() && current_path.GetCurrent().DistanceSq(self->position) < 2 * 2) {
    current_path.Clear();
  }

  auto& steering = ctx.bot->bot_controller->steering;
  bool is_stuck = IsStuck(*self, ctx);

  if (is_stuck) {
    bool find_nearest_corner = true;
    Vector2f best_corner;

    auto opt_start_tick = ctx.blackboard.Value<Tick>("stuck_start_tick");
    if (opt_start_tick) {
      Tick start_tick = *opt_start_tick;

      if (abs(TICK_DIFF(GetCurrentTick(), start_tick)) < 400) {
        auto opt_stuck_corner = ctx.blackboard.Value<Vector2f>("stuck_corner");

        if (opt_stuck_corner) {
          best_corner = *opt_stuck_corner;
          find_nearest_corner = false;
        }
      }
    }

    if (find_nearest_corner) {
      // Calculate the corners of the ship.
      Vector2f corners[] = {
          Vector2f(-radius, -radius),
          Vector2f(radius, -radius),
          Vector2f(-radius, radius),
          Vector2f(radius, radius),
      };
      float best_dist_sq = 1024 * 1024;
      size_t best_corner_index = 0;

      // Find the closest corner to the movement target.
      for (size_t i = 0; i < ZERO_ARRAY_SIZE(corners); ++i) {
        float dist_sq = movement_target.DistanceSq(self->position + corners[i]);
        if (dist_sq < best_dist_sq) {
          best_dist_sq = dist_sq;
          best_corner_index = i;
        }
      }

      best_corner = corners[best_corner_index];
      ctx.blackboard.Set<Vector2f>("stuck_corner", best_corner);
    }

    // Use the closest corner as our new target movement, so we rotate toward our closest corner.

    Log(LogLevel::Debug, "Unstucking: %f, %f", best_corner.x, best_corner.y);

    steering.Face(game, self->position + best_corner);
    // Avoid using seek because it wants to correct for velocity.
    steering.force += best_corner * 1000.0f;
  } else {
    float speed_sq = self->velocity.LengthSq();

    steering.Arrive(game, movement_target, 7.0f, 0.15f);
    if (speed_sq > 8.0f * 8.0f) {
      steering.AvoidWalls(game);
    }
  }

  return ExecuteResult::Success;
}

// Determine if we are stuck by checking last wall collision ticks and having a counter.
bool FollowPathNode::IsStuck(const Player& self, ExecuteContext& ctx) {
  // How many ticks of bouncing against a wall before it's considered stuck.
  constexpr u32 kStuckTickThreshold = 50;
  // The max number of bounces stored, which are depleted when not bouncing against a wall to eventually become
  // unstuck when below kStickTickThreshold again.
  constexpr u32 kStuckTickMax = 100;
  // How many ticks we are allowed to be in the stuck state.
  constexpr u32 kStuckTickMaxDuration = 400;

  u32 last_stored_bounce_tick = ctx.blackboard.ValueOr<u32>("last_bounce_tick", 0);
  u32 last_stored_bounce_tick_check = ctx.blackboard.ValueOr<u32>("last_bounce_tick_check", 0);
  u32 bounce_count = ctx.blackboard.ValueOr<u32>("bounce_count", 0);
  u32 previous_bounce_count = bounce_count;

  u32 tick = GetCurrentTick();

  if (tick != last_stored_bounce_tick_check) {
    if (last_stored_bounce_tick != self.last_bounce_tick) {
      ++bounce_count;

      ctx.blackboard.Set("bounce_count", bounce_count);
      ctx.blackboard.Set("last_bounce_tick", self.last_bounce_tick);
    } else {
      if (bounce_count > 0) --bounce_count;
      if (bounce_count > kStuckTickMax) bounce_count = kStuckTickMax;

      ctx.blackboard.Set("bounce_count", bounce_count);
    }

    ctx.blackboard.Set<u32>("last_bounce_tick_check", tick);
  }

  if (bounce_count >= kStuckTickThreshold) {
    Tick current_tick = GetCurrentTick();

    // Set the start tick so we can remove stuck status if it lasts too long.
    if (previous_bounce_count < kStuckTickThreshold) {
      ctx.blackboard.Set("stuck_start_tick", current_tick);
      ctx.blackboard.Erase("stuck_corner");
    }

    Tick start_tick = ctx.blackboard.ValueOr("stuck_start_tick", 0U);

    if (TICK_DIFF(current_tick, start_tick) >= kStuckTickMaxDuration) {
      ctx.blackboard.Set("bounce_count", 0U);
      ctx.blackboard.Erase("stuck_start_tick");
      ctx.blackboard.Erase("stuck_corner");
      Log(LogLevel::Debug, "Unstuck for more than %u ticks.", kStuckTickMaxDuration);
      return false;
    }
  }

  return bounce_count >= kStuckTickThreshold;
}

}  // namespace behavior
}  // namespace zero

#pragma once

#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

namespace zero {
namespace behavior {

// Returns success if the provided position is within a provided view angle
struct HeadingPositionViewNode : public behavior::BehaviorNode {
  HeadingPositionViewNode(const char* position_key, float view_radians)
      : position_key(position_key), view_radians(view_radians) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();

    if (!self) return ExecuteResult::Failure;

    auto opt_position = ctx.blackboard.Value<Vector2f>(position_key);
    if (!opt_position.has_value()) return ExecuteResult::Failure;

    Vector2f& position = opt_position.value();
    Vector2f direction = Normalize(position - self->position);

    float cos_angle = direction.Dot(self->GetHeading());
    float angle = acosf(cos_angle);

    return angle <= view_radians ? ExecuteResult::Success : ExecuteResult::Failure;
  }

  const char* position_key;
  float view_radians;
};

struct HeadingDirectionViewNode : public behavior::BehaviorNode {
  HeadingDirectionViewNode(const char* direction_key, float view_radians)
      : direction_key(direction_key), view_radians(view_radians) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();

    if (!self) return ExecuteResult::Failure;

    auto opt_direction = ctx.blackboard.Value<Vector2f>(direction_key);
    if (!opt_direction.has_value()) return ExecuteResult::Failure;

    Vector2f& direction = opt_direction.value();

    float cos_angle = direction.Dot(self->GetHeading());
    float angle = acosf(cos_angle);

    return angle <= view_radians ? ExecuteResult::Success : ExecuteResult::Failure;
  }

  const char* direction_key;
  float view_radians;
};

struct NearestTargetNode : public behavior::BehaviorNode {
  NearestTargetNode(const char* player_key) : player_key(player_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    Player* self = ctx.bot->game->player_manager.GetSelf();
    if (!self) return behavior::ExecuteResult::Failure;

    Player* nearest = GetNearestTarget(*ctx.bot->game, *self);

    if (!nearest) return behavior::ExecuteResult::Failure;

    ctx.blackboard.Set(player_key, nearest);

    return behavior::ExecuteResult::Success;
  }

 private:
  Player* GetNearestTarget(Game& game, Player& self) {
    Player* best_target = nullptr;
    float closest_dist_sq = std::numeric_limits<float>::max();

    for (size_t i = 0; i < game.player_manager.player_count; ++i) {
      Player* player = game.player_manager.players + i;

      if (player->ship >= 8) continue;
      if (player->frequency == self.frequency) continue;
      if (player->frequency == 90 || player->frequency == 91) continue;
      if (player->IsRespawning()) continue;
      if (player->position == Vector2f(0, 0)) continue;
      if (!game.player_manager.IsSynchronized(*player)) continue;

      bool in_safe = game.connection.map.GetTileId(player->position) == kTileSafeId;
      if (in_safe) continue;

      float dist_sq = player->position.DistanceSq(self.position);
      if (dist_sq < closest_dist_sq) {
        closest_dist_sq = dist_sq;
        best_target = player;
      }
    }

    return best_target;
  }

  const char* player_key;
};

}  // namespace behavior
}  // namespace zero

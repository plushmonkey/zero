#include "BotController.h"

#include <zero/Actuator.h>
#include <zero/RegionRegistry.h>
#include <zero/Steering.h>
#include <zero/path/Pathfinder.h>

namespace zero {

constexpr u8 kRequestedShip = 0;

struct ShipEnforcer {
  s32 last_request_tick;
  u8 requested_ship;

  ShipEnforcer() {
    last_request_tick = GetCurrentTick();
    requested_ship = kRequestedShip;
  }

  void Update(Game& game) {
    constexpr s32 kRequestInterval = 300;

    Player* self = game.player_manager.GetSelf();

    if (!self) return;
    if (self->ship == requested_ship) return;

    s32 current_tick = GetCurrentTick();

    if (TICK_DIFF(current_tick, last_request_tick) >= kRequestInterval) {
      printf("Sending ship request\n");
      game.connection.SendShipRequest(requested_ship);
      last_request_tick = current_tick;
    }
  }
};

BotController::BotController() {
  ship_enforcer = std::make_unique<ShipEnforcer>();
}

Player* BotController::GetNearestTarget(Game& game, Player& self) {
  Player* best_target = nullptr;
  float closest_dist_sq = std::numeric_limits<float>::max();

  float enter_delay = (game.connection.settings.EnterDelay / 100.0f);

  for (size_t i = 0; i < game.player_manager.player_count; ++i) {
    Player* player = game.player_manager.players + i;

    if (player->ship >= 8) continue;
    if (player->frequency == self.frequency) continue;
    if (player->enter_delay > 0.0f && player->enter_delay < enter_delay) continue;

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

bool CanMoveBetween(Game& game, Vector2f from, Vector2f to, float radius, u32 frequency) {
  Vector2f trajectory = to - from;
  Vector2f direction = Normalize(trajectory);
  Vector2f side = Perpendicular(direction);

  float distance = from.Distance(to);

  CastResult center = game.GetMap().Cast(from, direction, distance, frequency);
  CastResult side1 = game.GetMap().Cast(from + side * radius, direction, distance, frequency);
  CastResult side2 = game.GetMap().Cast(from - side * radius, direction, distance, frequency);

  return !center.hit && !side1.hit && !side2.hit;
}

void BotController::Update(float dt, Game& game, InputState& input) {
  ship_enforcer->Update(game);

  Player* self = game.player_manager.GetSelf();
  if (!self || self->ship == 8) return;

#if 1
  if (pathfinder == nullptr) {
    auto processor = std::make_unique<path::NodeProcessor>(game);

    region_registry = std::make_unique<RegionRegistry>();
    region_registry->CreateAll(game.connection.map, 16.0f / 14.0f);

    pathfinder = std::make_unique<path::Pathfinder>(std::move(processor), *region_registry);
  }
#endif

  Player* target = GetNearestTarget(game, *self);
  if (!target || target->ship == 8) return;

  float radius = game.connection.settings.ShipSettings[self->ship].GetRadius();

  Vector2f movement_target = target->position;
  Vector2f to_target = target->position - self->position;
  Vector2f direction = Normalize(to_target);
  bool path_following = false;

  CastResult cast_result = game.connection.map.Cast(self->position, direction, to_target.Length(), self->frequency);

  if (cast_result.hit) {
    bool build = true;

    path_following = true;

    if (!current_path.empty()) {
      if (target->position.DistanceSq(current_path.back()) > 3.0f * 3.0f) {
        Vector2f next = current_path.front();
        Vector2f direction = Normalize(next - self->position);
        Vector2f side = Perpendicular(direction);
        float distance = next.Distance(self->position);

        CastResult center = game.GetMap().Cast(self->position, direction, distance, self->frequency);
        CastResult side1 = game.GetMap().Cast(self->position + side * radius, direction, distance, self->frequency);
        CastResult side2 = game.GetMap().Cast(self->position - side * radius, direction, distance, self->frequency);

        if (!center.hit && !side1.hit && !side2.hit) {
          build = false;
        }
      }
    }

    if (build) {
      current_path = pathfinder->FindPath(game.connection.map, self->position, target->position, radius);
      current_path = pathfinder->SmoothPath(game, current_path, radius);
    }

    if (!current_path.empty()) {
      movement_target = current_path.front();
    }

    if (!current_path.empty() && (u16)self->position.x == (u16)current_path.at(0).x &&
        (u16)self->position.y == (u16)current_path.at(0).y) {
      current_path.erase(current_path.begin());

      if (!current_path.empty()) {
        movement_target = current_path.front();
      }
    }

    while (current_path.size() > 1 &&
           CanMoveBetween(game, self->position, current_path.at(1), radius, self->frequency)) {
      current_path.erase(current_path.begin());
      movement_target = current_path.front();
    }

    if (current_path.size() == 1 && current_path.front().DistanceSq(self->position) < 2 * 2) {
      current_path.clear();
    }
  }

  float enter_delay = (game.connection.settings.EnterDelay / 100.0f);
  if (target->enter_delay > 0.0f && target->enter_delay < enter_delay) return;

  float weapon_speed = game.connection.settings.ShipSettings[self->ship].BulletSpeed / 16.0f / 10.0f;
  Vector2f shot_velocity = self->GetHeading() * weapon_speed;
  Vector2f shot_direction = Normalize(self->velocity + shot_velocity);

  Steering steering;

  if (path_following) {
    steering.Seek(game, movement_target);
  } else {
    steering.Pursue(game, *target, 15.0f);

    if ((float)self->energy > game.ship_controller.ship.energy * 0.3f) {
      steering.Face(game, target->position);
    }
  }

  // Optimize for shot direction unless it's going backwards
  if (shot_direction.Dot(self->GetHeading()) < 0.0f) {
    shot_direction = self->GetHeading();
  }

  Actuator actuator;
  actuator.Update(game, input, shot_direction, steering.force, steering.rotation);

  float nearby_radius = game.connection.settings.ShipSettings[target->ship].GetRadius() * 1.5f;
  Vector2f nearest_point =
      GetClosestLinePoint(self->position, self->position + shot_direction * 100.0f, target->position);

  bool in_safe = game.connection.map.GetTileId(self->position) == kTileSafeId;

  if (!in_safe && nearest_point.DistanceSq(target->position) < nearby_radius * nearby_radius) {
    input.SetAction(InputAction::Bullet, true);
  }
}

}  // namespace zero

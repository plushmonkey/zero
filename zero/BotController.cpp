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

    if (player->frequency == self.frequency) continue;
    if (player->enter_delay > 0.0f && player->enter_delay < enter_delay) continue;

    float dist_sq = player->position.DistanceSq(self.position);
    if (dist_sq < closest_dist_sq) {
      closest_dist_sq = dist_sq;
      best_target = player;
    }
  }

  return best_target;
}
void BotController::Update(float dt, Game& game, InputState& input) {
  ship_enforcer->Update(game);

  Player* self = game.player_manager.GetSelf();
  if (!self || self->ship == 8) return;

#if 0
  if (pathfinder == nullptr) {
    auto processor = std::make_unique<path::NodeProcessor>(game);

    region_registry = std::make_unique<RegionRegistry>();
    region_registry->CreateAll(game.connection.map, 16.0f / 14.0f);

    pathfinder = std::make_unique<path::Pathfinder>(std::move(processor), *region_registry);
  }
#endif

  Player* follow_target = GetNearestTarget(game, *self);
  // Player* follow_target = game.player_manager.GetPlayerByName("monkey");
  if (!follow_target || follow_target->ship == 8) return;

  float enter_delay = (game.connection.settings.EnterDelay / 100.0f);
  if (follow_target->enter_delay > 0.0f && follow_target->enter_delay < enter_delay) return;

  float weapon_speed = game.connection.settings.ShipSettings[self->ship].BulletSpeed / 16.0f / 10.0f;
  Vector2f shot_velocity = self->GetHeading() * weapon_speed;
  Vector2f shot_direction = Normalize(self->velocity + shot_velocity);

  Steering steering;
  steering.Pursue(game, *follow_target, 15.0f);

  if ((float)self->energy > game.ship_controller.ship.energy * 0.3f) {
    steering.Face(game, follow_target->position);
  }

  // Optimize for shot direction unless it's going backwards
  if (shot_direction.Dot(self->GetHeading()) < 0.0f) {
    shot_direction = self->GetHeading();
  }

  Actuator actuator;
  actuator.Update(game, input, shot_direction, steering.force, steering.rotation);

  float nearby_radius = game.connection.settings.ShipSettings[follow_target->ship].GetRadius() * 1.5f;
  Vector2f nearest_point =
      GetClosestLinePoint(self->position, self->position + shot_direction * 100.0f, follow_target->position);

  bool in_safe = game.connection.map.GetTileId(self->position) == kTileSafeId;

  if (!in_safe && nearest_point.DistanceSq(follow_target->position) < nearby_radius * nearby_radius) {
    input.SetAction(InputAction::Bullet, true);
  }
}

}  // namespace zero

#include "BotController.h"

#include <zero/Actuator.h>
#include <zero/RegionRegistry.h>
#include <zero/Steering.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/behavior/nodes/GoToNode.h>
#include <zero/behavior/nodes/ShipNode.h>
#include <zero/path/Pathfinder.h>

namespace zero {

Vector2f CalculateShot(const Vector2f& pShooter, const Vector2f& pTarget, const Vector2f& vShooter,
                       const Vector2f& vTarget, float sProjectile) {
  Vector2f totarget = pTarget - pShooter;
  Vector2f v = vTarget - vShooter;

  float a = v.Dot(v) - sProjectile * sProjectile;
  float b = 2 * v.Dot(totarget);
  float c = totarget.Dot(totarget);

  Vector2f solution;

  float disc = (b * b) - 4 * a * c;
  float t = -1.0;

  if (disc >= 0.0) {
    float t1 = (-b + sqrtf(disc)) / (2 * a);
    float t2 = (-b - sqrtf(disc)) / (2 * a);
    if (t1 < t2 && t1 >= 0)
      t = t1;
    else
      t = t2;
  }

  solution = pTarget + (v * t);

  return solution;
}

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
      if (player->IsRespawning()) continue;
      if (player->position == Vector2f(0, 0)) continue;

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

struct GetPlayerPositionNode : public behavior::BehaviorNode {
  GetPlayerPositionNode(const char* player_key, const char* position_key)
      : player_key(player_key), position_key(position_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto player_opt = ctx.blackboard.Value<Player*>(player_key);
    if (!player_opt.has_value()) return behavior::ExecuteResult::Failure;

    ctx.blackboard.Set(position_key, player_opt.value()->position);

    return behavior::ExecuteResult::Success;
  }

  const char* player_key;
  const char* position_key;
};

Player* BotController::GetNearestTarget(Game& game, Player& self) {
  Player* best_target = nullptr;
  float closest_dist_sq = std::numeric_limits<float>::max();

  for (size_t i = 0; i < game.player_manager.player_count; ++i) {
    Player* player = game.player_manager.players + i;

    if (player->ship >= 8) continue;
    if (player->frequency == self.frequency) continue;
    if (player->IsRespawning()) continue;
    if (player->position == Vector2f(0, 0)) continue;

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

BotController::BotController() {
  using namespace std;
  using namespace behavior;

  constexpr u8 kRequestedShip = 0;

  auto root = make_unique<SelectorNode>();

  auto ship_join_sequence = make_unique<SequenceNode>();
  ship_join_sequence->Child(make_unique<InvertNode>(make_unique<ShipQueryNode>(kRequestedShip)))
      .Child(make_unique<ShipRequestNode>(kRequestedShip));

#if 0
  auto chase_sequence = make_unique<SequenceNode>();

  chase_sequence->Child(make_unique<NearestTargetNode>("nearest_target"))
      .Child(make_unique<GetPlayerPositionNode>("nearest_target", "nearest_position"))
      .Child(make_unique<GoToNode>("nearest_position"));

  root->Child(move(ship_join_sequence)).Child(move(chase_sequence));
#else
  root->Child(move(ship_join_sequence));

  // auto goto_fr = make_unique<GoToNode>("flagroom");
  // root->Child(move(goto_fr));
#endif

  behavior_tree = move(root);
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

void BotController::Update(float dt, Game& game, InputState& input, behavior::ExecuteContext& execute_ctx) {
  if (pathfinder == nullptr) {
    auto processor = std::make_unique<path::NodeProcessor>(game);

    region_registry = std::make_unique<RegionRegistry>();
    region_registry->CreateAll(game.GetMap(), 16.0f / 14.0f);

    pathfinder = std::make_unique<path::Pathfinder>(std::move(processor), *region_registry);

    pathfinder->CreateMapWeights(game.GetMap(), 14.0f / 16.0f);
  }

  steering.Reset();

  execute_ctx.blackboard.Set("flagroom", Vector2f(155, 580));

  if (behavior_tree) {
    behavior_tree->Execute(execute_ctx);
  }

#if 0
  actuator.Update(game, input, steering.force, steering.rotation);

#else
  Player* self = game.player_manager.GetSelf();
  if (!self || self->ship == 8) return;

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

  // Seek directly out of safe to prevent oscillating in safe attempting to fight nearby player.
  if (game.GetMap().GetTileId(self->position) == kTileSafeId) {
    path_following = true;
  }

  Steering steering;

  Vector2f aimshot = target->position;

  bool in_safe = game.connection.map.GetTileId(self->position) == kTileSafeId;

  if (path_following) {
    steering.Seek(game, movement_target);
    steering.AvoidWalls(game, 30.0f);
  } else {
    aimshot = CalculateShot(self->position, target->position, self->velocity, target->velocity, weapon_speed);

    const float kTargetDistance = 15.0f;
    // steering.Pursue(game, aimshot, *target, kTargetDistance);
    steering.Seek(game, aimshot, kTargetDistance);

    if (target->position.DistanceSq(self->position) <= kTargetDistance * kTargetDistance) {
      //  steering.Face(game, target->position);
    } else {
    }
    steering.Face(game, aimshot);
  }

  // Optimize for shot direction unless it's going backwards
  if (shot_direction.Dot(self->GetHeading()) < 0.0f) {
    shot_direction = self->GetHeading();
  }

  Actuator actuator;
  actuator.Update(game, input, steering.force, steering.rotation);

  float nearby_radius = game.connection.settings.ShipSettings[target->ship].GetRadius() * 18.0f;
  Vector2f nearest_point = GetClosestLinePoint(self->position, self->position + shot_direction * 200.0f, aimshot);

  if (!path_following && !in_safe && nearest_point.DistanceSq(target->position) < nearby_radius * nearby_radius) {
    input.SetAction(InputAction::Bullet, true);
  }
#endif
}

}  // namespace zero

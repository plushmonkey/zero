#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/behavior/nodes/AimNode.h>
#include <zero/behavior/nodes/AttachNode.h>
#include <zero/behavior/nodes/BlackboardNode.h>
#include <zero/behavior/nodes/FlagNode.h>
#include <zero/behavior/nodes/InputActionNode.h>
#include <zero/behavior/nodes/MapNode.h>
#include <zero/behavior/nodes/MathNode.h>
#include <zero/behavior/nodes/MoveNode.h>
#include <zero/behavior/nodes/PlayerNode.h>
#include <zero/behavior/nodes/PowerballNode.h>
#include <zero/behavior/nodes/RegionNode.h>
#include <zero/behavior/nodes/RenderNode.h>
#include <zero/behavior/nodes/ShipNode.h>
#include <zero/behavior/nodes/TargetNode.h>
#include <zero/behavior/nodes/ThreatNode.h>
#include <zero/behavior/nodes/TimerNode.h>
#include <zero/behavior/nodes/WaypointNode.h>
#include <zero/zones/svs/nodes/BurstAreaQueryNode.h>
#include <zero/zones/svs/nodes/DynamicPlayerBoundingBoxQueryNode.h>
#include <zero/zones/svs/nodes/FindNearestGreenNode.h>
#include <zero/zones/svs/nodes/IncomingDamageQueryNode.h>
#include <zero/zones/svs/nodes/NearbyEnemyWeaponQueryNode.h>
#include <zero/zones/trenchwars/TrenchWars.h>
#include <zero/zones/trenchwars/nodes/AttachNode.h>
#include <zero/zones/trenchwars/nodes/BaseNode.h>
#include <zero/zones/trenchwars/nodes/FlagNode.h>
#include <zero/zones/trenchwars/nodes/MoveNode.h>

namespace zero {
namespace tw {

constexpr float kJavLeashDistance = 35.0f;
constexpr float kAvoidTeamDistance = 2.0f;

using namespace behavior;

// Wiggle our heading so we might line up a better shot.
// Uses steering Face to align ourselves to new heading.
struct AimWiggleNode : public BehaviorNode {
  AimWiggleNode(const char* target_position_key, float arc_rads, float period)
      : target_position_key(target_position_key), arc_rads(arc_rads), period(period) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    Player* self = ctx.bot->game->player_manager.GetSelf();
    if (!self || self->ship >= 8) return ExecuteResult::Failure;

    auto opt_target = ctx.blackboard.Value<Vector2f>(target_position_key);

    if (!opt_target) return ExecuteResult::Failure;
    Vector2f target = *opt_target;

    if (period <= 0.0f) {
      period = 1.0f;
    }

    float t = GetTime();
    float rads = sinf(t / period) * arc_rads;
    Vector2f v = target - self->position;
    Vector2f new_forward = self->position + Rotate(v, rads);

    ctx.bot->bot_controller->steering.Face(*ctx.bot->game, new_forward);

    return ExecuteResult::Success;
  }

  inline float GetTime() { return GetMicrosecondTick() / (kTickDurationMicro * 10.0f); }

  float arc_rads = 0.0f;
  float period = 1.0f;
  const char* target_position_key = nullptr;
};

// Determines if a bomb explosion will hit the provided rect.
struct BombRectCollideQuery : BehaviorNode {
  BombRectCollideQuery(const char* trajectory_key, const char* rect_key, float required_damage)
      : trajectory_key(trajectory_key), rect_key(rect_key), required_damage(required_damage) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto& game = *ctx.bot->game;

    auto self = game.player_manager.GetSelf();
    if (!self || self->ship >= 8) return ExecuteResult::Failure;

    if (game.ship_controller.ship.bombs == 0) return ExecuteResult::Failure;

    auto opt_trajectory = ctx.blackboard.Value<Vector2f>(trajectory_key);
    if (!opt_trajectory) return ExecuteResult::Failure;

    Vector2f trajectory = *opt_trajectory;

    auto opt_rect = ctx.blackboard.Value<Rectangle>(rect_key);
    if (!opt_rect) return ExecuteResult::Failure;

    Rectangle target_rect = *opt_rect;
    Vector2f target_center = target_rect.GetCenter();

    std::vector<Rectangle> colliders = GetEnemyColliders(game.player_manager, *self);

    Vector2f heading = self->GetHeading();

    Weapon weapon = {};
    s32 speed = game.connection.settings.ShipSettings[self->ship].BombSpeed;

    weapon.x = (u32)(self->position.x * 16) * 1000;
    weapon.y = (u32)(self->position.y * 16) * 1000;
    weapon.velocity_x = (s32)(self->velocity.x * 16.0f * 10.0f) + (s32)(heading.x * speed);
    weapon.velocity_y = (s32)(self->velocity.y * 16.0f * 10.0f) + (s32)(heading.y * speed);
    weapon.last_tick = GetCurrentTick();
    weapon.end_tick =
        MAKE_TICK(weapon.last_tick + game.weapon_manager.GetWeaponTotalAliveTime(WeaponType::Bomb, false));
    weapon.player_id = self->id;
    weapon.frequency = self->frequency;
    weapon.bounces_remaining = game.connection.settings.ShipSettings[self->ship].BombBounceCount;

    auto opt_explosion_position = SimulateBomb(game, weapon, colliders);
    if (!opt_explosion_position) return ExecuteResult::Failure;

    Vector2f explosion_position = *opt_explosion_position;

    float damage = GetBombDamage(game, *self, explosion_position, target_center);

#if 0
    game.line_renderer.PushCross(explosion_position, Vector3f(0, 1, 0), 3.0f);
    game.line_renderer.Render(game.camera);
#endif

    if (damage < required_damage) return ExecuteResult::Failure;

    return ExecuteResult::Success;
  }

  float GetBombDamage(const Game& game, Player& self, Vector2f explosion_position, Vector2f target_center) {
    constexpr float kTilePixels = 16.0f;
    constexpr float kBombRadius = 2.0f;

    float distance = explosion_position.Distance(target_center) * kTilePixels - kBombRadius;
    if (distance < 0.0f) distance = 0.0f;

    float explode_pixels = (float)(game.connection.settings.BombExplodePixels +
                                   game.connection.settings.BombExplodePixels * (game.ship_controller.ship.bombs - 1));
    if (explode_pixels <= 0.0f || distance > explode_pixels) return 0.0f;

    int bomb_dmg = game.connection.settings.BombDamageLevel / 1000;
    int level = game.ship_controller.ship.bombs;

    if (game.connection.settings.ShipSettings[self.ship].EmpBomb) {
      bomb_dmg = (int)(bomb_dmg * (game.connection.settings.EBombDamagePercent / 1000.0f));
    }

    return (explode_pixels - distance) * (bomb_dmg / explode_pixels);
  }

  // Projects a bomb weapon forward to try to find collision and returns the optional explosion position.
  std::optional<Vector2f> SimulateBomb(Game& game, Weapon weapon, const std::vector<Rectangle>& enemy_colliders) {
    constexpr u32 kMaxSimTicks = 1000;
    const Map& map = game.GetMap();

    for (u32 i = 0; i < kMaxSimTicks; ++i) {
      Tick tick = MAKE_TICK(weapon.last_tick + i);

      bool x_collide = SimulateAxis(map, weapon, 0);
      bool y_collide = SimulateAxis(map, weapon, 1);
      weapon.UpdatePosition();

      if (TICK_GTE(tick, weapon.end_tick)) break;

      if (x_collide || y_collide) {
        if (weapon.bounces_remaining == 0) {
          // Wall explosion
          return weapon.position;
        } else {
          --weapon.bounces_remaining;
        }
      }

      Rectangle weapon_collider = GetBombCollider(weapon, game.connection.settings.ProximityDistance);

      for (auto& rect : enemy_colliders) {
        if (BoxBoxOverlap(rect.min, rect.max, weapon_collider.min, weapon_collider.max)) {
          return weapon.position;
        }
      }
    }

    return std::nullopt;
  }

  bool SimulateAxis(const Map& map, Weapon& weapon, int axis) {
    u32* pos = axis == 0 ? &weapon.x : &weapon.y;
    s32* vel = axis == 0 ? &weapon.velocity_x : &weapon.velocity_y;
    u32 previous = *pos;

    *pos += *vel;

    if (weapon.data.type == WeaponType::Thor) return false;

    if (map.IsSolid(weapon.x / 16000, weapon.y / 16000, weapon.frequency)) {
      *pos = previous;
      *vel = -*vel;

      return true;
    }

    return false;
  }

  Rectangle GetBombCollider(const Weapon& weapon, u16 proximity_distance) {
    float weapon_radius = 18.0f;

    if (weapon.data.type == WeaponType::ProximityBomb) {
      float prox = (float)(proximity_distance + weapon.data.level);

      if (weapon.data.type == WeaponType::Thor) {
        prox += 3;
      }

      weapon_radius = prox * 18.0f;
    }

    weapon_radius = (weapon_radius - 14.0f) / 16.0f;

    Vector2f min_w = weapon.position.PixelRounded() - Vector2f(weapon_radius, weapon_radius);
    Vector2f max_w = weapon.position.PixelRounded() + Vector2f(weapon_radius, weapon_radius);

    return Rectangle(min_w, max_w);
  }

  std::vector<Rectangle> GetEnemyColliders(const PlayerManager& pm, const Player& self) {
    std::vector<Rectangle> result;

    result.reserve(pm.player_count);

    for (size_t i = 0; i < pm.player_count; ++i) {
      const Player* player = pm.players + i;

      if (player->frequency == self.frequency) continue;
      if (player->ship >= 8) continue;
      if (player->IsRespawning()) continue;

      float radius = pm.connection.settings.ShipSettings[player->ship].GetRadius();

      result.push_back(Rectangle::FromPositionRadius(player->position, radius));
    }

    return result;
  }

  const char* trajectory_key = nullptr;
  const char* rect_key = nullptr;
  float required_damage = 1000.0f;
};

struct NearestTeamTerrierPosition : BehaviorNode {
  NearestTeamTerrierPosition(const char* output_key, float max_distance_check)
      : output_key(output_key), max_distance_check(max_distance_check) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto& pm = ctx.bot->game->player_manager;
    auto self = pm.GetSelf();

    if (!self || self->ship >= 8) return ExecuteResult::Failure;

    Player* nearest_terrier = nullptr;
    float closest_dist_sq = 1024.0f * 1024.0f;

    for (size_t i = 0; i < pm.player_count; ++i) {
      Player* player = pm.players + i;

      if (player->ship != 4) continue;
      if (player->id == self->id) continue;
      if (player->frequency != self->frequency) continue;

      float dist_sq = player->position.DistanceSq(self->position);
      if (dist_sq < closest_dist_sq && dist_sq <= max_distance_check * max_distance_check) {
        closest_dist_sq = dist_sq;
        nearest_terrier = player;
      }
    }

    if (!nearest_terrier) return ExecuteResult::Failure;

    ctx.blackboard.Set(output_key, nearest_terrier->position);

    return ExecuteResult::Success;
  }

  const char* output_key = nullptr;
  const float max_distance_check = 30.0f;
};

static std::unique_ptr<behavior::BehaviorNode> CreateDefensiveTree() {
  using namespace behavior;

  BehaviorBuilder builder;

  // clang-format off
  builder
    .Sequence()
        .Child<DodgeIncomingDamage>(0.4f, 16.0f, 0.0f)
        .End();
  // clang-format on

  return builder.Build();
}

static std::unique_ptr<behavior::BehaviorNode> CreateBulletShootTree(const char* nearest_target_key) {
  using namespace behavior;

  BehaviorBuilder builder;

  // clang-format off
  builder
    .Sequence()
        .Sequence() // Determine if a shot should be fired by using weapon trajectory and bounding boxes.
            .Child<AimNode>(WeaponType::Bullet, nearest_target_key, "aimshot")
            .Child<svs::DynamicPlayerBoundingBoxQueryNode>(nearest_target_key, "target_bounds", 4.0f)
            .Child<MoveRectangleNode>("target_bounds", "aimshot", "target_bounds")
            .Child<RenderRectNode>("world_camera", "target_bounds", Vector3f(1.0f, 0.0f, 0.0f))
            .InvertChild<ShipWeaponCooldownQueryNode>(WeaponType::Bullet)
            .InvertChild<TileQueryNode>(kTileIdSafe)
            .Child<ShotVelocityQueryNode>(WeaponType::Bullet, "bullet_fire_velocity")
            .Child<RayNode>("self_position", "bullet_fire_velocity", "bullet_fire_ray")
            .Child<svs::DynamicPlayerBoundingBoxQueryNode>(nearest_target_key, "target_bounds", 4.0f)
            .Child<MoveRectangleNode>("target_bounds", "aimshot", "target_bounds")
            .Child<RayRectangleInterceptNode>("bullet_fire_ray", "target_bounds")
            .Child<InputActionNode>(InputAction::Bullet)
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

static std::unique_ptr<behavior::BehaviorNode> CreateBombShootTree(const char* nearest_target_key) {
  using namespace behavior;

  BehaviorBuilder builder;

  constexpr float kRequiredExplodeDamage = 1000.0f;

  // clang-format off
  builder
    .Sequence()
        .Sequence() // Determine if a shot should be fired by using weapon trajectory and bounding boxes.
            .Child<svs::DynamicPlayerBoundingBoxQueryNode>(nearest_target_key, "target_bounds", 2.0f)
            .Child<RenderRectNode>("world_camera", "target_bounds", Vector3f(1.0f, 0.0f, 0.0f))
            .InvertChild<ShipWeaponCooldownQueryNode>(WeaponType::Bomb)
            .InvertChild<TileQueryNode>(kTileIdSafe)
            .Child<ShotVelocityQueryNode>(WeaponType::Bomb, "bomb_fire_velocity")
            .Child<BombRectCollideQuery>("bomb_fire_velocity", "target_bounds", kRequiredExplodeDamage)
            .Child<InputActionNode>(InputAction::Bomb)
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

static std::unique_ptr<behavior::BehaviorNode> CreateOffensiveTree(const char* nearest_target_key,
                                                                   const char* nearest_target_position_key) {
  using namespace behavior;

  constexpr float kMaxBulletDistance = 6.0f;

  BehaviorBuilder builder;

  // clang-format off
  builder
    .Sequence() // Aim at target and shoot while seeking them.
        .Parallel()
            .Selector(CompositeDecorator::Success)
                .Composite(CreateBombShootTree(nearest_target_key))
                .Sequence() // If target is very close, shoot bullets at them.
                    .InvertChild<DistanceThresholdNode>(nearest_target_position_key, kMaxBulletDistance)
                    .Composite(CreateBulletShootTree(nearest_target_key))
                    .End()
                .End()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

static std::unique_ptr<behavior::BehaviorNode> CreateFlagroomTravelBehavior() {
  using namespace behavior;

  BehaviorBuilder builder;

  // Spread out from team members while going to flagroom so we don't all die in one shot.
  float kAvoidTeamTravelRange = 4.0f;

  // clang-format off
  builder
    .Sequence()
        .Child<PlayerSelfNode>("self")
        .Child<PlayerPositionQueryNode>("self_position")
        .Selector() // Choose between traveling and fighting enemies in the base.
            .Sequence() // Look for a nearby enemy while traveling through the base.
                .InvertChild<InFlagroomNode>("self_position")
                .Child<FindBaseEnemyNode>("nearest_target")
                .Child<PlayerPositionQueryNode>("nearest_target", "nearest_target_position")
                .Sequence(CompositeDecorator::Success) // Detach if we are attached
                    .Child<AttachedQueryNode>()
                    .Child<TimerExpiredNode>("attach_cooldown")
                    .Child<DetachNode>()
                    .Child<TimerSetNode>("attach_cooldown", 100)
                    .End()
                .Child<AvoidTeamNode>(kAvoidTeamDistance)
                .Child<AimNode>(WeaponType::Bomb, "nearest_target", "aimshot")
                .Child<FaceNode>("aimshot")
                .Child<SeekNode>("aimshot", kJavLeashDistance, SeekNode::DistanceResolveType::Dynamic)
                .Composite(CreateOffensiveTree("nearest_target", "nearest_target_position"))
                .End()
            .Sequence() // Travel to the flag room
                .Child<AvoidTeamNode>(kAvoidTeamTravelRange)
                .Sequence(CompositeDecorator::Success) // Use afterburners to get to flagroom faster.
                    .InvertChild<InFlagroomNode>("self_position")
                    .Child<AfterburnerThresholdNode>()
                    .End()
                .Selector()
                    .Composite(CreateBaseAttachTree("self"))
                    .Sequence() // Go directly to the flag room if we aren't there.
                        .InvertChild<InFlagroomNode>("self_position")
                        .Child<GoToNode>("tw_flag_position")
                        .Child<RenderPathNode>(Vector3f(0.0f, 1.0f, 0.5f))
                        .End()
                    .Sequence() // If we are the closest player to the unclaimed flag, touch it.
                        .Child<InFlagroomNode>("self_position")
                        .Child<NearestFlagNode>(NearestFlagNode::Type::Unclaimed, "nearest_flag")
                        .Child<FlagPositionQueryNode>("nearest_flag", "nearest_flag_position")
                        .InvertChild<DistanceThresholdNode>("nearest_flag_position", 12.0f) // Only go claim it if we are already close
                        .Child<BestFlagClaimerNode>()
                        .Sequence(CompositeDecorator::Success)
                            .Child<NearestTargetNode>("nearest_target", true)
                            .Composite(CreateBulletShootTree("nearest_target")) // Shoot weapons while collecting flag so we don't ride on top of each other
                            .End()
                        .Selector()
                            .Sequence()
                                .InvertChild<ShipTraverseQueryNode>("nearest_flag_position")
                                .Child<GoToNode>("nearest_flag_position")
                                .End()
                            .Child<ArriveNode>("nearest_flag_position", 1.25f)
                            .End()
                        .End()
                    .End()
                .End() // End travel to flagroom sequence
            .End() // End fight/travel selector
        .End();
  // clang-format on

  return builder.Build();
}

std::unique_ptr<behavior::BehaviorNode> CreateJavelinBasingTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  BehaviorBuilder builder;

  constexpr float kMaxDistanceFromTerrier = 10.0f;

  // clang-format off
  builder
    .Selector()
        .Composite(CreateFlagroomTravelBehavior())
        .Sequence() // Find nearest target and either path to them or seek them directly.
            .Sequence() // Find an enemy
                .Child<PlayerPositionQueryNode>("self_position")
                .Child<NearestTargetNode>("nearest_target", true)
                .Child<PlayerPositionQueryNode>("nearest_target", "nearest_target_position")
                .End()
            .Selector()
                .Composite(CreateDefensiveTree())
                .Sequence() // Go to enemy and attack if they are in the flag room.
                    .Child<InFlagroomNode>("nearest_target_position")
                    .Selector(CompositeDecorator::Success) // Move toward nearest target but stay near terrier so we try to stay alive.
                        .Sequence() // Stay near our terrier
                            .Child<NearestTeamTerrierPosition>("nearest_terrier_position", kMaxDistanceFromTerrier * 3)
                            .Child<DistanceThresholdNode>("nearest_terrier_position", kMaxDistanceFromTerrier)
                            .Sequence(CompositeDecorator::Success) // If we have direct access to our terrier, face the target so we just move backwards to the terr.
                                .Child<ShipTraverseQueryNode>("nearest_terrier_position")
                                .Child<FaceNode>("nearest_target_position")
                                .End()
                            .Child<VectorNode>("nearest_terrier_position", "best_position")
                            .End()
                        .Sequence()
                            .InvertChild<ShipTraverseQueryNode>("nearest_target_position")
                            .Child<VectorNode>("nearest_target_position", "best_position")
                            .End()
                        .End()
                    .Sequence(CompositeDecorator::Success) // If we can traverse to the target, wiggle our aim a bit for better shots.
                        .Child<ShipTraverseQueryNode>("nearest_target_position")
                        .Child<AimWiggleNode>("nearest_target_position", Radians(50.0f), 1.5f)
                        .End()
                    .Child<GoToNode>("best_position")
                    .Composite(CreateOffensiveTree("nearest_target", "nearest_target_position"))
                    .End()
                .End()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

}  // namespace tw
}  // namespace zero

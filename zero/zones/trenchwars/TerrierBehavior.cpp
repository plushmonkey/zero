#include "TerrierBehavior.h"

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
#include <zero/behavior/nodes/ThreatNode.h>
#include <zero/behavior/nodes/TimerNode.h>
#include <zero/behavior/nodes/WaypointNode.h>
#include <zero/zones/svs/nodes/BurstAreaQueryNode.h>
#include <zero/zones/svs/nodes/DynamicPlayerBoundingBoxQueryNode.h>
#include <zero/zones/svs/nodes/FindNearestGreenNode.h>
#include <zero/zones/svs/nodes/IncomingDamageQueryNode.h>
#include <zero/zones/svs/nodes/MemoryTargetNode.h>
#include <zero/zones/svs/nodes/NearbyEnemyWeaponQueryNode.h>
#include <zero/zones/trenchwars/TrenchWars.h>
#include <zero/zones/trenchwars/nodes/BaseNode.h>
#include <zero/zones/trenchwars/nodes/FlagNode.h>

namespace zero {
namespace tw {

constexpr float kTerrierLeashDistance = 30.0f;

// This checks all of the enemy weapons to make sure none are in the provided rect.
// Returns success if it is safe.
struct SafeRectNode : behavior::BehaviorNode {
  SafeRectNode(Rectangle rect) : rect(rect) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    auto& wm = ctx.bot->game->weapon_manager;

    for (size_t i = 0; i < wm.weapon_count; ++i) {
      Weapon* weapon = wm.weapons + i;
      WeaponType type = weapon->data.type;

      if (type == WeaponType::Repel || type == WeaponType::Decoy) continue;
      if (weapon->frequency == self->frequency) continue;
      if (!rect.Contains(weapon->position)) continue;

      return behavior::ExecuteResult::Failure;
    }

    return behavior::ExecuteResult::Success;
  }

  Rectangle rect;
};

// This node is a hacky way to get the terrier to move away from bomb explosions.
// It will project bombs forward to see if any explosions will kill us. If they will,
// it will read the flagroom partition and override the enemy count to be very high, so we want to move to a new
// quadrant.
struct EscapeBombExplodeQuadrantNode : behavior::BehaviorNode {
  EscapeBombExplodeQuadrantNode(const char* partition_key) : partition_key(partition_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto& pm = ctx.bot->game->player_manager;

    auto self = pm.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    auto opt_tw = ctx.blackboard.Value<TrenchWars*>("tw");
    if (!opt_tw) return behavior::ExecuteResult::Failure;

    auto opt_partition = ctx.blackboard.Value<FlagroomPartition>(partition_key);
    if (!opt_partition) return behavior::ExecuteResult::Failure;

    TrenchWars* tw = *opt_tw;
    FlagroomPartition partition = *opt_partition;

    float danger_damage = ctx.bot->game->ship_controller.ship.energy * 0.3f;
    if (danger_damage > self->energy) {
      danger_damage = self->energy;
    }

    if (InDangerousPosition(ctx, danger_damage)) {
      FlagroomQuadrantRegion region = GetQuadrantRegion(tw->flag_position, self->position);
      partition.GetQuadrant(region).enemy_count = 10000;

      ctx.blackboard.Set(partition_key, partition);

      return behavior::ExecuteResult::Success;
    }

    return behavior::ExecuteResult::Failure;
  }

  bool InDangerousPosition(behavior::ExecuteContext& ctx, float damage_amount) {
    auto& pm = ctx.bot->game->player_manager;
    auto self = pm.GetSelf();

    // Store the nearby teammate positions so we can quickly see if a bomb will explode on them.
    std::vector<Vector2f> nearby_teammates;
    nearby_teammates.reserve(32);

    constexpr float kNearbyDistanceSq = 20.0f * 20.0f;

    for (size_t i = 0; i < pm.player_count; ++i) {
      Player* player = pm.players + i;

      if (player->ship >= 8) continue;
      if (player->frequency != self->frequency) continue;
      if (player->IsRespawning()) continue;
      if (!pm.IsSynchronized(*player)) continue;
      if (player->position.DistanceSq(self->position) > kNearbyDistanceSq) continue;

      nearby_teammates.push_back(player->position);
    }

    auto& settings = ctx.bot->game->connection.settings;

    float total_damage = 0.0f;

    for (size_t i = 0; i < ctx.bot->game->weapon_manager.weapon_count; ++i) {
      Weapon* weapon = ctx.bot->game->weapon_manager.weapons + i;

      if (weapon->data.type != WeaponType::Bomb && weapon->data.type != WeaponType::ProximityBomb) continue;
      if (weapon->data.alternate) continue;
      if (weapon->position.DistanceSq(self->position) > kNearbyDistanceSq) continue;

      auto opt_explosion = GetBombExplosion(ctx, *weapon, nearby_teammates, weapon->frequency == self->frequency);
      if (!opt_explosion) continue;

      int bomb_dmg = ctx.bot->game->connection.settings.BombDamageLevel;
      int level = weapon->data.level;

      if (weapon->data.type == WeaponType::Thor) {
        // Weapon level should always be 0 for thor in normal gameplay, I believe, but this is how it's done
        bomb_dmg = bomb_dmg + bomb_dmg * weapon->data.level * weapon->data.level;
        level = 3 + weapon->data.level;
      }

      bomb_dmg = bomb_dmg / 1000;

      if (weapon->flags & WEAPON_FLAG_EMP) {
        bomb_dmg = (int)(bomb_dmg * (settings.EBombDamagePercent / 1000.0f));
      }

      float explode_pixels = (float)(settings.BombExplodePixels + settings.BombExplodePixels * weapon->data.level);

      float constexpr kBombSize = 2.0f;
      Vector2f delta = Absolute(*opt_explosion - self->position) * 16.0f;
      float distance = delta.Length() - kBombSize;
      // Skipping bounce damage and close-bomb damage
      float damage = (float)((explode_pixels - distance) * (bomb_dmg / explode_pixels));

      total_damage += damage;

      if (total_damage >= damage_amount) {
        return true;
      }
    }

    return false;
  }

  // Project a bomb forward to see where it will explode.
  std::optional<Vector2f> GetBombExplosion(behavior::ExecuteContext& ctx, Weapon weapon,
                                           const std::vector<Vector2f>& nearby_teammates, bool team_weapon) {
    constexpr u32 kMaxSimTicks = 500;
    const Map& map = ctx.bot->game->GetMap();

    float ship_radius = ctx.bot->game->connection.settings.ShipSettings[0].GetRadius();
    Vector2f player_r(ship_radius, ship_radius);

    for (u32 i = 0; i < kMaxSimTicks; ++i) {
      Tick tick = MAKE_TICK(weapon.last_tick + i);

      bool x_collide = SimulateAxis(map, weapon, 1.0f / 100.0f, 0);
      bool y_collide = SimulateAxis(map, weapon, 1.0f / 100.0f, 1);

      if (TICK_GTE(tick, weapon.end_tick)) break;

      if (x_collide || y_collide) {
        if (weapon.bounces_remaining == 0) {
          // Wall explosion
          return weapon.position;
        } else {
          --weapon.bounces_remaining;
        }
      }

      if (team_weapon) continue;

      Rectangle weapon_collider = GetBombCollider(weapon, ctx.bot->game->connection.settings.ProximityDistance);

      for (const auto& pos : nearby_teammates) {
        if (BoxBoxOverlap(pos - player_r, pos + player_r, weapon_collider.min, weapon_collider.max)) {
          return weapon.position;
        }
      }
    }

    return std::nullopt;
  }

  bool SimulateAxis(const Map& map, Weapon& weapon, float dt, int axis) {
    float previous = weapon.position[axis];

    weapon.position[axis] += weapon.velocity[axis] * dt;

    if (weapon.data.type == WeaponType::Thor) return false;

    if (map.IsSolid((u16)floorf(weapon.position.x), (u16)floorf(weapon.position.y), weapon.frequency)) {
      weapon.position[axis] = previous;
      weapon.velocity[axis] = -weapon.velocity[axis];

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

  const char* partition_key = nullptr;
};

// Returns success if we want to travel to a new partition. The Vector2f of the center of the quadrant will be put in
// the output_key.
struct FindNewSafePositionNode : behavior::BehaviorNode {
  FindNewSafePositionNode(const char* partition_key, const char* output_key)
      : partition_key(partition_key), output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    auto opt_tw = ctx.blackboard.Value<TrenchWars*>("tw");
    if (!opt_tw) return behavior::ExecuteResult::Failure;

    auto opt_partition = ctx.blackboard.Value<FlagroomPartition>(partition_key);
    if (!opt_partition) return behavior::ExecuteResult::Failure;

    TrenchWars* tw = *opt_tw;
    const FlagroomPartition& partition = *opt_partition;

    FlagroomQuadrantRegion region = GetQuadrantRegion(tw->flag_position, self->position);
    size_t index = (size_t)region;

    const FlagroomQuadrant& current_quad = partition.quadrants[index];
    // Find the two neighboring quadrants to see if they are better.
    const FlagroomQuadrant& quad1 = partition.quadrants[(index + 1) % 4];
    const FlagroomQuadrant& quad2 = partition.quadrants[(index - 1) % 4];

    s32 danger_current = current_quad.enemy_count - current_quad.team_count;

    s32 danger_q1 = quad1.enemy_count - quad1.team_count;
    s32 danger_q2 = quad2.enemy_count - quad2.team_count;

    if (current_quad.enemy_count == 0 || (danger_current < danger_q1 && danger_current < danger_q2)) {
      return behavior::ExecuteResult::Failure;
    }

    FlagroomQuadrantRegion new_region = region;

    if (danger_q1 < danger_q2) {
      new_region = (FlagroomQuadrantRegion)((index + 1) % 4);
    } else {
      new_region = (FlagroomQuadrantRegion)((index - 1) % 4);
    }

    Vector2f new_position = tw->flag_position;

    switch (new_region) {
      case FlagroomQuadrantRegion::NorthEast: {
        new_position += Vector2f(15, -5);
      } break;
      case FlagroomQuadrantRegion::NorthWest: {
        new_position += Vector2f(-15, -5);
      } break;
      case FlagroomQuadrantRegion::SouthWest: {
        new_position += Vector2f(-15, 5);
      } break;
      case FlagroomQuadrantRegion::SouthEast: {
        new_position += Vector2f(15, 5);
      } break;
    }

    ctx.blackboard.Set(output_key, new_position);

    return behavior::ExecuteResult::Success;
  }

  const char* partition_key = nullptr;
  const char* output_key = nullptr;
};

static std::unique_ptr<behavior::BehaviorNode> CreateDefensiveTree() {
  using namespace behavior;

  constexpr float kIncomingDamageDistance = 6.0f;

  BehaviorBuilder builder;

  // clang-format off
  builder
    .Sequence()
        .Sequence(CompositeDecorator::Success) // Always check incoming damage
            .Child<svs::IncomingDamageQueryNode>(kIncomingDamageDistance, "incoming_damage")
            .Child<PlayerCurrentEnergyQueryNode>("self_energy")
            .End()
        .Sequence(CompositeDecorator::Success) // If we are in danger, use a portal
            .Child<ShipPortalPositionQueryNode>() // Check if we have a portal down.
            .Child<ScalarThresholdNode<float>>("incoming_damage", "self_energy")
            .Child<TimerExpiredNode>("defense_timer")
            .Child<InputActionNode>(InputAction::Warp)
            .Child<TimerSetNode>("defense_timer", 100)
            .End()
        .Child<DodgeIncomingDamage>(0.1f, 16.0f, 0.0f)
        .End();
  // clang-format on

  return builder.Build();
}

static std::unique_ptr<behavior::BehaviorNode> CreateBurstSequence() {
  using namespace behavior;

  BehaviorBuilder builder;

  // clang-format off
  builder
    .Sequence()
        .Sequence(CompositeDecorator::Success) // Use burst when near a wall.
            .Child<ShipWeaponCapabilityQueryNode>(WeaponType::Burst)
            .InvertChild<ShipWeaponCooldownQueryNode>(WeaponType::Bomb)
            .Child<svs::BurstAreaQueryNode>()
            .Child<InputActionNode>(InputAction::Burst)
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

static std::unique_ptr<behavior::BehaviorNode> CreateOffensiveTree(const char* nearest_target_key,
                                                                   const char* nearest_target_position_key,
                                                                   float enemy_distance_threshold) {
  using namespace behavior;

  BehaviorBuilder builder;

  // clang-format off
  builder
    .Sequence() // Aim at target and shoot while seeking them.
        .Child<AimNode>(WeaponType::Bullet, nearest_target_key, "aimshot")
        .Parallel()
            .Child<FaceNode>("aimshot")
            .Child<SeekNode>("aimshot", kTerrierLeashDistance, SeekNode::DistanceResolveType::Dynamic)
            .Sequence(CompositeDecorator::Success) // Determine if a shot should be fired by using weapon trajectory and bounding boxes.
                .InvertChild<DistanceThresholdNode>(nearest_target_position_key, enemy_distance_threshold)
                .Composite(CreateBurstSequence())
                .Child<PlayerEnergyPercentThresholdNode>(0.9f)
                .Child<svs::DynamicPlayerBoundingBoxQueryNode>(nearest_target_key, "target_bounds", 4.0f)
                .Child<MoveRectangleNode>("target_bounds", "aimshot", "target_bounds")
                .Child<RenderRectNode>("world_camera", "target_bounds", Vector3f(1.0f, 0.0f, 0.0f))
                .Selector()
                    .Child<BlackboardSetQueryNode>("rushing")
                    .Child<PlayerEnergyPercentThresholdNode>(0.3f)
                    .End()
                .InvertChild<ShipWeaponCooldownQueryNode>(WeaponType::Bullet)
                .InvertChild<InputQueryNode>(InputAction::Bomb) // Don't try to shoot a bullet when shooting a bomb.
                .InvertChild<TileQueryNode>(kTileIdSafe)
                .Child<ShotVelocityQueryNode>(WeaponType::Bullet, "bullet_fire_velocity")
                .Child<RayNode>("self_position", "bullet_fire_velocity", "bullet_fire_ray")
                .Child<svs::DynamicPlayerBoundingBoxQueryNode>(nearest_target_key, "target_bounds", 4.0f)
                .Child<MoveRectangleNode>("target_bounds", "aimshot", "target_bounds")
                .Child<RayRectangleInterceptNode>("bullet_fire_ray", "target_bounds")
                .Child<InputActionNode>(InputAction::Bullet)
                .End()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

// This is a tree to handle progressing through the entrance area.
// It will attempt to move forward when safe and fall back when in danger.
static std::unique_ptr<behavior::BehaviorNode> CreateEntranceBehavior() {
  using namespace behavior;

  BehaviorBuilder builder;

  constexpr float kNearEntranceDistance = 15.0f;

  const Rectangle kCheckRect(Vector2f(504, 271), Vector2f(520, 285));

  // clang-format off
  builder
    .Sequence()
        .Child<PlayerPositionQueryNode>("self_position")
        .Child<RectangleContainsNode>(kCheckRect, "self_position")
        .Child<NearEntranceNode>(kNearEntranceDistance)
        .Child<InfluenceMapPopulateWeapons>()
        .Child<InfluenceMapPopulateEnemies>(3.0f, 5000.0f, false)
        .SuccessChild<DodgeIncomingDamage>(0.1f, 15.0f, 0.0f)
        .Composite(CreateBurstSequence())
        .Selector()
            .Sequence()
                .Child<SelectBestEntranceSideNode>()
                .Child<FollowPathNode>()
                .Child<RenderPathNode>(Vector3f(0.0f, 0.0f, 0.75f))
                .End()
            .Selector()
                .Sequence()
                    .InvertChild<EmptyEntranceNode>()
                    .Child<ExecuteNode>([](ExecuteContext& ctx) { // Go to the area below the entrance
                      Vector2f entrance_position = ctx.blackboard.ValueOr<Vector2f>("tw_entrance_position", Vector2f(512, 281));

                      entrance_position.y += 8;

                      GoToNode node(entrance_position);

                      return node.Execute(ctx);
                    })
                    .Child<RenderPathNode>(Vector3f(1.0f, 0.0f, 0.0f))
                    .End()
                .Sequence()
                    .InvertChild<ShipTraverseQueryNode>("tw_entrance_position")
                    .Child<GoToNode>("tw_entrance_position")
                    .Child<RenderPathNode>(Vector3f(1.0f, 0.0f, 0.0f))
                    .End()
                .Child<ArriveNode>("tw_entrance_position", 1.25f)
                .End()
            .End()
        .End()
        .End();
  // clang-format on

  return builder.Build();
}

// This is a tree to handle staying safe below the entrance until it is cleared and safe to move up.
static std::unique_ptr<behavior::BehaviorNode> CreateBelowEntranceBehavior() {
  using namespace behavior;

  BehaviorBuilder builder;

  constexpr float kEntranceNearbyDistance = 24.0f;
  // How many teammates must be in the flagroom before we start moving up including self.
  constexpr u32 kFlagroomPresenceRequirement = 2;

  const Rectangle kEntranceBottomShaftRect(Vector2f(509, 277), Vector2f(516, 293));

  // clang-format off
  builder
    .Sequence()
        .Sequence() // A sequence that contains the conditionals for testing that we are below the entrance.
            .Child<PlayerPositionQueryNode>("self_position")
            .InvertChild<InFlagroomNode>("self_position") // We must not be in the flagroom
            .InvertChild<DistanceThresholdNode>("tw_entrance_position", kEntranceNearbyDistance) // We must be near the 'entrance' position.
            .Child<ExecuteNode>([](ExecuteContext& ctx) { // We must be below the entrance position
              auto self = ctx.bot->game->player_manager.GetSelf();
              if (!self || self->ship >= 8) return ExecuteResult::Failure;

              auto opt_entrance_pos = ctx.blackboard.Value<Vector2f>("tw_entrance_position");
              if (!opt_entrance_pos) return ExecuteResult::Failure;
              Vector2f entrance_pos = *opt_entrance_pos;

              return (self->position.y > entrance_pos.y) ? ExecuteResult::Success : ExecuteResult::Failure;
            })
            .End()
        .Selector()
            .Sequence() // Check if the above area is clear to move up
                .Selector() // Check if we have a team presence above us so we don't rush in alone.
                    .Sequence() // If we are alone on a frequency, consider it enough to move forward.
                        .Child<PlayerFrequencyCountQueryNode>("self_freq_size")
                        .InvertChild<ScalarThresholdNode<size_t>>("self_freq_size", 2)
                        .End()
                    .Selector() // Either we have a teammate in the fr or the fr has no enemies
                        .Child<SafeFlagroomNode>()
                        .Child<FlagroomPresenceNode>(kFlagroomPresenceRequirement)
                        .End()
                    .End()
                .Child<EmptyEntranceNode>()
                .Child<SafeRectNode>(kEntranceBottomShaftRect)
                .Child<GoToNode>("tw_entrance_position") // Move into entrance area so other tree takes over.
                .End()
            .Selector(CompositeDecorator::Success) // Above area is not yet safe, sit still and dodge. TODO: Move into safe area.
                .Child<DodgeIncomingDamage>(0.1f, 15.0f, 0.0f)
                .Child<SeekZeroNode>()
                .End()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

static std::unique_ptr<behavior::BehaviorNode> CreateFlagroomTravelBehavior() {
  using namespace behavior;

  BehaviorBuilder builder;

  // clang-format off
  builder
    .Sequence()
        .Child<PlayerSelfNode>("self")
        .Child<PlayerPositionQueryNode>("self_position")
        .InvertChild<InFlagroomNode>("self_position")
        .SuccessChild<DodgeIncomingDamage>(0.3f, 16.0f, 0.0f)
        .Sequence(CompositeDecorator::Success) // Use afterburners to get to flagroom faster.
            .InvertChild<InFlagroomNode>("self_position")
            .Child<ExecuteNode>([](ExecuteContext& ctx) {
              auto self = ctx.bot->game->player_manager.GetSelf();
              if (!self || self->ship >= 8) return ExecuteResult::Failure;

              float max_energy = (float)ctx.bot->game->ship_controller.ship.energy;

              auto& input = *ctx.bot->bot_controller->input;
              auto& last_input = ctx.bot->bot_controller->last_input;

              // Keep using afterburners above 50%. Disable until full energy, then enable again.
              if (last_input.IsDown(InputAction::Afterburner)) {
                input.SetAction(InputAction::Afterburner, self->energy > max_energy * 0.5f);
              } else if (self->energy >= max_energy) {
                input.SetAction(InputAction::Afterburner, true);
              }

              return ExecuteResult::Success;
            })
            .End()
        .Child<GoToNode>("tw_flag_position")
        .End();
  // clang-format on

  return builder.Build();
}

// We are in the flagroom and need to find a safe position
static std::unique_ptr<behavior::BehaviorNode> CreateSafeFlagroomPositionTree() {
  using namespace behavior;

  BehaviorBuilder builder;

  // TODO: Find safest area within our safe quadrant.

  // clang-format off
  builder
    .Sequence()
        .Child<FlagroomPartitionNode>("partition")
        .Selector()
            .Sequence() // Try to move to a new quadrant if we are overran by enemies.
                .Child<FindNewSafePositionNode>("partition", "new_quad_position") // Returns false if we are in the safest quadrant
                .Child<GoToNode>("new_quad_position")
                .End()
            .Sequence() // Begin moving to a new quadrant when we are near an incoming bomb explosion. This might end early and stay in the current quadrant when we escape bomb area.
                .Child<EscapeBombExplodeQuadrantNode>("partition")
                .Child<FindNewSafePositionNode>("partition", "new_quad_position") // This will find a new quadrant if we are near a bomb explosion
                .Child<GoToNode>("new_quad_position")
                .End()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

// We are fully in the flagroom now, so try to stay alive.
static std::unique_ptr<behavior::BehaviorNode> CreateFlagroomBehavior() {
  using namespace behavior;

  BehaviorBuilder builder;

  // This is how far away to check for enemies that are rushing at us with low energy.
  // We will stop dodging and try to finish them off if they are within this distance and low energy.
  constexpr float kNearbyEnemyThreshold = 10.0f;

  // clang-format off
  builder
    .Sequence()
        .Sequence() // Find an enemy
            .Child<PlayerPositionQueryNode>("self_position")
            .Child<svs::NearestMemoryTargetNode>("nearest_target")
            .Child<PlayerPositionQueryNode>("nearest_target", "nearest_target_position")
            .End()
        .Sequence(CompositeDecorator::Success) // If we have a portal but no location, lay one down.
            .Child<InFlagroomNode>("self_position")
            .Child<InFlagroomNode>("nearest_target_position")
            .Child<ShipItemCountThresholdNode>(ShipItemType::Portal, 1)
            .InvertChild<ShipPortalPositionQueryNode>()
            .Child<InputActionNode>(InputAction::Portal)
            .End()
        .Selector()
            .Composite(CreateDefensiveTree())
            .Composite(CreateSafeFlagroomPositionTree())
            .Sequence() // Kill any enemies that get very close to us.
                .Child<InFlagroomNode>("nearest_target_position")
                .Composite(CreateOffensiveTree("nearest_target", "nearest_target_position", kNearbyEnemyThreshold))
                .End()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

std::unique_ptr<behavior::BehaviorNode> CreateTerrierTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  BehaviorBuilder builder;

  // clang-format off
  builder
    .Selector()
        .Composite(CreateEntranceBehavior())
        .Composite(CreateBelowEntranceBehavior())
        .Composite(CreateFlagroomTravelBehavior())
        .Composite(CreateFlagroomBehavior())
        .End();
  // clang-format on

  return builder.Build();
}

}  // namespace tw
}  // namespace zero

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
        .Child<DodgeIncomingDamage>(0.1f, 16.0f)
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

  // clang-format off
  builder
    .Sequence()
        .Child<NearEntranceNode>(kNearEntranceDistance)
        .Child<InfluenceMapPopulateWeapons>()
        .Child<InfluenceMapPopulateEnemies>(3.0f, 5000.0f, false)
        .SuccessChild<DodgeIncomingDamage>(0.1f, 15.0f)
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

  constexpr float kEntranceNearbyDistance = 15.0f;
  // How many teammates must be in the flagroom before we start moving up including self.
  constexpr u32 kFlagroomPresenceRequirement = 2;

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
                .Child<GoToNode>("tw_entrance_position") // Move into entrance area so other tree takes over.
                .End()
            .Sequence(CompositeDecorator::Success) // Above area is not yet safe, sit still and dodge. TODO: Move into safe area.
                .Child<SeekNode>("self_position", 0.0f, SeekNode::DistanceResolveType::Zero)
                .Child<DodgeIncomingDamage>(0.1f, 15.0f)
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
        .SuccessChild<DodgeIncomingDamage>(0.3f, 16.0f)
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
        .Selector()
            .Sequence() // Go directly to the flag room if we aren't there.
                .InvertChild<InFlagroomNode>("self_position")
                .Child<GoToNode>("tw_flag_position")
                //.Child<RenderPathNode>(Vector3f(0.0f, 1.0f, 0.5f))
                .End()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

// We are in the flagroom and need to find a safe position
static std::unique_ptr<behavior::BehaviorNode> CreateSafeFlagroomPositionTree() {
  using namespace behavior;

  BehaviorBuilder builder;

  // TODO: Find safest area within our safe quadrant.
  // TODO: Move away from bombs.

  // clang-format off
  builder
    .Sequence() // Split up the fr into quadrants and travel to the safest one
        .Child<FlagroomPartitionNode>("partition") // Returns false if we are in the safest quadrant
        .Child<FindNewSafePositionNode>("partition", "new_quad_position")
        .Child<GoToNode>("new_quad_position")
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
        .Composite(CreateBelowEntranceBehavior())
        .Composite(CreateEntranceBehavior())
        .Composite(CreateFlagroomTravelBehavior())
        .Composite(CreateFlagroomBehavior())
        .End();
  // clang-format on

  return builder.Build();
}

}  // namespace tw
}  // namespace zero

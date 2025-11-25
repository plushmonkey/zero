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

constexpr float kSpiderLeashDistance = 30.0f;
constexpr float kAvoidTeamDistance = 2.0f;

static std::unique_ptr<behavior::BehaviorNode> CreateDefensiveTree() {
  using namespace behavior;

  constexpr float kRepelDistance = 16.0f;
  constexpr float kLowEnergyThreshold = 450.0f;
  // This is how far away to check for enemies that are rushing at us with low energy.
  // We will stop dodging and try to finish them off if they are within this distance and low energy.
  constexpr float kNearbyEnemyThreshold = 20.0f;

  BehaviorBuilder builder;

  // clang-format off
  builder
    .Sequence() // Attempt to dodge and use defensive items.
        .Sequence(CompositeDecorator::Success) // Always check incoming damage 
            .Child<svs::IncomingDamageQueryNode>(kRepelDistance, "incoming_damage")
            .Child<PlayerCurrentEnergyQueryNode>("self_energy")
            .End()
        .Sequence(CompositeDecorator::Invert) // Check if enemy is very low energy and close to use. Don't bother dodging if they are rushing us with low energy.
            .Child<PlayerEnergyQueryNode>("nearest_target", "nearest_target_energy")
            .InvertChild<ScalarThresholdNode<float>>("nearest_target_energy", kLowEnergyThreshold)
            .InvertChild<DistanceThresholdNode>("nearest_target_position", "self_position", kNearbyEnemyThreshold)
            .End()
        .Child<DodgeIncomingDamage>(0.4f, 16.0f, 0.0f)
        .End();
  // clang-format on

  return builder.Build();
}

static std::unique_ptr<behavior::BehaviorNode> CreateShootTree(const char* nearest_target_key) {
  using namespace behavior;

  BehaviorBuilder builder;

  // clang-format off
  builder
    .Sequence()
        .Sequence(CompositeDecorator::Success) // Determine if a shot should be fired by using weapon trajectory and bounding boxes.
            .Child<AimNode>(WeaponType::Bullet, nearest_target_key, "aimshot")
            .Child<svs::DynamicPlayerBoundingBoxQueryNode>(nearest_target_key, "target_bounds", 4.0f)
            .Child<MoveRectangleNode>("target_bounds", "aimshot", "target_bounds")
            .Child<RenderRectNode>("world_camera", "target_bounds", Vector3f(1.0f, 0.0f, 0.0f))
            .Selector()
                .Child<BlackboardSetQueryNode>("rushing")
                .Child<PlayerEnergyPercentThresholdNode>(0.3f)
                .End()
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

static std::unique_ptr<behavior::BehaviorNode> CreateOffensiveTree(const char* nearest_target_key,
                                                                   const char* nearest_target_position_key) {
  using namespace behavior;

  constexpr float kRepelDistance = 16.0f;
  constexpr float kLowEnergyThreshold = 450.0f;
  // This is how far away to check for enemies that are rushing at us with low energy.
  // We will stop dodging and try to finish them off if they are within this distance and low energy.
  constexpr float kNearbyEnemyThreshold = 20.0f;

  BehaviorBuilder builder;

  // clang-format off
  builder
    .Sequence() // Aim at target and shoot while seeking them.
        .Child<AimNode>(WeaponType::Bullet, nearest_target_key, "aimshot")
        .Parallel()
            .Child<FaceNode>("aimshot")
            .Child<BlackboardEraseNode>("rushing")
            .Selector()
                .Sequence() // If our target is very low energy, rush at them
                    .Child<PlayerEnergyQueryNode>(nearest_target_key, "nearest_target_energy")
                    .InvertChild<ScalarThresholdNode<float>>("nearest_target_energy", kLowEnergyThreshold)
                    .Child<SeekNode>("aimshot", 0.0f, SeekNode::DistanceResolveType::Static)
                    .Child<ScalarNode>(1.0f, "rushing")
                    .End()
                .Sequence() // Begin moving away if our energy is low.
                    .InvertChild<PlayerEnergyPercentThresholdNode>(0.3f)
                    .Child<SeekNode>("aimshot", kSpiderLeashDistance, SeekNode::DistanceResolveType::Dynamic)
                    .End()
                .Child<SeekNode>("aimshot", 0.0f, SeekNode::DistanceResolveType::Zero)
                .End()
            .Child<AvoidTeamNode>(kAvoidTeamDistance)
            .Composite(CreateShootTree(nearest_target_key))
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
                .Composite(CreateOffensiveTree("nearest_target", "nearest_target_position"))
                .End()
            .Sequence() // Travel to the flag room
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
                        .Child<BestFlagClaimerNode>()
                        .Sequence(CompositeDecorator::Success)
                            .Child<NearestTargetNode>("nearest_target", true)
                            .Composite(CreateShootTree("nearest_target")) // Shoot weapons while collecting flag so we don't ride on top of each other
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

std::unique_ptr<behavior::BehaviorNode> CreateSpiderBasingTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  BehaviorBuilder builder;

#if TW_RENDER_FR
  // clang-format off
  builder.
    Sequence()
        .Child<ExecuteNode>([](ExecuteContext& ctx) {
          auto self = ctx.bot->game->player_manager.GetSelf();
          if (self) {
            self->position = Vector2f(512, 269);
          }
          return ExecuteResult::Success;
        })
        .Child<RenderFlagroomNode>()
        .End();
  // clang-format on

  return builder.Build();
#endif

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
                    .Selector()
                        .Sequence()
                            .Child<ShipTraverseQueryNode>("nearest_target_position")
                            .Composite(CreateOffensiveTree("nearest_target", "nearest_target_position"))
                            .End()
                        .Child<GoToNode>("nearest_target_position")
                        .End()
                    .End()
                .End()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

}  // namespace tw
}  // namespace zero

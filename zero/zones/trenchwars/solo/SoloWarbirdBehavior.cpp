#include "SoloWarbirdBehavior.h"

#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/behavior/nodes/AimNode.h>
#include <zero/behavior/nodes/BlackboardNode.h>
#include <zero/behavior/nodes/InputActionNode.h>
#include <zero/behavior/nodes/MapNode.h>
#include <zero/behavior/nodes/MathNode.h>
#include <zero/behavior/nodes/MoveNode.h>
#include <zero/behavior/nodes/PlayerNode.h>
#include <zero/behavior/nodes/RegionNode.h>
#include <zero/behavior/nodes/ShipNode.h>
#include <zero/behavior/nodes/TargetNode.h>
#include <zero/behavior/nodes/ThreatNode.h>
#include <zero/zones/svs/nodes/DynamicPlayerBoundingBoxQueryNode.h>
#include <zero/zones/svs/nodes/IncomingDamageQueryNode.h>
#include <zero/zones/trenchwars/TrenchWars.h>
#include <zero/zones/trenchwars/nodes/BaseNode.h>
#include <zero/zones/trenchwars/nodes/MoveNode.h>

namespace zero {
namespace tw {

constexpr float kFarEnemyDistance = 35.0f;

static std::unique_ptr<behavior::BehaviorNode> CreateDefensiveTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  BehaviorBuilder builder;

  constexpr float kDangerDistance = 4.0f;

  // clang-format off
  builder
    .Sequence()
        .Sequence(CompositeDecorator::Success)
            .Child<svs::IncomingDamageQueryNode>(kDangerDistance, "incoming_damage")
            .Child<PlayerCurrentEnergyQueryNode>("self_energy")
            .End()
        .Sequence(CompositeDecorator::Success) // Use warp if we are fully energy and about to die
            .Child<ScalarThresholdNode<float>>("incoming_damage", "self_energy")
            .Child<InputActionNode>(InputAction::Warp)
            .End()
        .Child<DodgeIncomingDamage>(0.6f, 25.0f) 
        .Child<InputActionNode>(InputAction::Afterburner) // If DodgeIncomingDamage is true, then we should activate afterburners to escape
        .End();
  // clang-format on

  return builder.Build();
}

static std::unique_ptr<behavior::BehaviorNode> CreateChaseTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  BehaviorBuilder builder;

  // clang-format off
  builder
    .Sequence()
        .Sequence(CompositeDecorator::Success) // Use afterburners to chase enemy if they are very far.
            .Child<DistanceThresholdNode>("nearest_target_position", kFarEnemyDistance)
            .Child<AfterburnerThresholdNode>()
            .End()
        .Sequence() // Path to target if they aren't immediately visible.
            .InvertChild<ShipTraverseQueryNode>("nearest_target_position")
            .Child<GoToNode>("nearest_target_position")
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

static std::unique_ptr<behavior::BehaviorNode> CreateAimTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  BehaviorBuilder builder;

  constexpr float kBulletEnergyCost = 0.9f;

  // clang-format off
  builder
    .Sequence()
        .Child<PlayerPositionQueryNode>("self_position")
        .Child<PlayerEnergyQueryNode>("self_energy")
        .Child<AimNode>(WeaponType::Bullet, "nearest_target", "aimshot")
        .Sequence(CompositeDecorator::Success) // Determine main movement vectors
            .Child<VectorNode>("aimshot", "face_position")
            .Selector()
                .Sequence() // If we have more energy than target, rush close to them to get a better shot.
                    .Child<PlayerEnergyQueryNode>("nearest_target", "nearest_target_energy")
                    .Child<ScalarThresholdNode<float>>("self_energy", "nearest_target_energy")
                    .Child<SeekNode>("aimshot", 0.0f, SeekNode::DistanceResolveType::Zero)
                    .End()
                .Child<SeekNode>("nearest_target_position", "leash_distance", SeekNode::DistanceResolveType::Dynamic)
                .End()
            .End()
        .Sequence(CompositeDecorator::Success) // Shoot
            .InvertChild<TileQueryNode>(kTileIdSafe)
            .Child<PlayerEnergyPercentThresholdNode>(kBulletEnergyCost)
            .Child<VectorSubtractNode>("aimshot", "self_position", "target_direction", true)
            .Child<ShotVelocityQueryNode>(WeaponType::Bullet, "bullet_fire_velocity")
            .Child<RayNode>("self_position", "bullet_fire_velocity", "bullet_fire_ray")
            .Child<svs::DynamicPlayerBoundingBoxQueryNode>("nearest_target", "target_bounds", 3.0f)
            .Child<MoveRectangleNode>("target_bounds", "aimshot", "target_bounds")
            .Child<RayRectangleInterceptNode>("bullet_fire_ray", "target_bounds")
            .Child<InputActionNode>(InputAction::Bullet)
            .End()    
        .Sequence(CompositeDecorator::Success) // Face away from target so it can dodge while waiting for energy
            .Child<ShipWeaponCooldownQueryNode>(WeaponType::Bomb)
            .Child<PerpendicularNode>("nearest_target_position", "self_position", "away_dir", true)
            .Child<VectorSubtractNode>("nearest_target_position", "self_position", "target_direction", true)
            .Child<VectorAddNode>("away_dir", "target_direction", "away_dir", true)
            .Child<VectorAddNode>("self_position", "away_dir", "away_pos")
            .Child<VectorNode>("away_pos", "face_position")
            .End()
        .Sequence(CompositeDecorator::Success)
            .Child<BlackboardSetQueryNode>("face_position")
            .Child<RotationThresholdSetNode>(0.35f)
            .Child<FaceNode>("face_position")
            .Child<BlackboardEraseNode>("face_position")
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

std::unique_ptr<behavior::BehaviorNode> CreateSoloWarbirdTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  BehaviorBuilder builder;

  // clang-format off
  builder
    .Selector()
        .Composite(CreateDefensiveTree(ctx))
        .Sequence() // Fight player or follow path to them.
            .Child<PlayerPositionQueryNode>("self_position")
            .Child<NearestTargetNode>("nearest_target")
            .Child<PlayerPositionQueryNode>("nearest_target", "nearest_target_position")
            .InvertChild<InFlagroomNode>("nearest_target_position")
            .Selector()
                .Composite(CreateChaseTree(ctx))
                .Composite(CreateAimTree(ctx))
                .End()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

}  // namespace tw
}  // namespace zero

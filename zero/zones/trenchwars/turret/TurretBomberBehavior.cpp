#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/behavior/nodes/AimNode.h>
#include <zero/behavior/nodes/InputActionNode.h>
#include <zero/behavior/nodes/MapNode.h>
#include <zero/behavior/nodes/MathNode.h>
#include <zero/behavior/nodes/MoveNode.h>
#include <zero/behavior/nodes/PlayerNode.h>
#include <zero/behavior/nodes/TargetNode.h>
#include <zero/zones/trenchwars/TrenchWars.h>
#include <zero/zones/trenchwars/nodes/BaseNode.h>
#include <zero/zones/trenchwars/nodes/MoveNode.h>

namespace zero {
namespace tw {

static std::unique_ptr<behavior::BehaviorNode> CreateAimTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  BehaviorBuilder builder;

  constexpr float kCloseBombDistance = 15.0f;

  // clang-format off
  builder
    .Sequence()
        .Child<PlayerPositionQueryNode>("self_position")
        .Child<PlayerEnergyQueryNode>("self_energy")
        .Child<AimNode>(WeaponType::Bomb, "nearest_target", "aimshot")
        .Child<FaceNode>("aimshot")
        .Selector() // Shoot bullets when enemy is very close, bombs otherwise
            .Sequence() // Shoot bullet
                .InvertChild<DistanceThresholdNode>("nearest_target_position", kCloseBombDistance)
                .InvertChild<TileQueryNode>(kTileIdSafe)
                .Child<VectorSubtractNode>("aimshot", "self_position", "target_direction", true)
                .Child<ShotVelocityQueryNode>(WeaponType::Bullet, "bullet_fire_velocity")
                .Child<RayNode>("self_position", "bullet_fire_velocity", "bullet_fire_ray")
                .Child<PlayerBoundingBoxQueryNode>("nearest_target", "target_bounds", 3.0f)
                .Child<MoveRectangleNode>("target_bounds", "aimshot", "target_bounds")
                .Child<RayRectangleInterceptNode>("bullet_fire_ray", "target_bounds")
                .Child<InputActionNode>(InputAction::Bullet)
                .End()
            .Sequence(CompositeDecorator::Success) // Shoot bomb
                .InvertChild<TileQueryNode>(kTileIdSafe)
                .Child<VectorSubtractNode>("aimshot", "self_position", "target_direction", true)
                .Child<ShotVelocityQueryNode>(WeaponType::Bomb, "bomb_fire_velocity")
                .Child<RayNode>("self_position", "bomb_fire_velocity", "bomb_fire_ray")
                .Child<PlayerBoundingBoxQueryNode>("nearest_target", "target_bounds", 3.0f)
                .Child<MoveRectangleNode>("target_bounds", "aimshot", "target_bounds")
                .Child<RayRectangleInterceptNode>("bomb_fire_ray", "target_bounds")
                .Child<InputActionNode>(InputAction::Bomb)
                .End()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

std::unique_ptr<behavior::BehaviorNode> CreateTurretBomberBehavior(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  BehaviorBuilder builder;

  // clang-format off
  builder
    .Selector()
        .Sequence() // Attach
            .End()
        .Sequence() // Fight player or follow path to them.
            .Child<PlayerPositionQueryNode>("self_position")
            .Child<NearestTargetNode>("nearest_target", true)
            .Child<PlayerPositionQueryNode>("nearest_target", "nearest_target_position")
            .Selector()
                .Composite(CreateAimTree(ctx))
                .End()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

}  // namespace tw
}  // namespace zero

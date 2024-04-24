#include "TestBehavior.h"

#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/behavior/nodes/AimNode.h>
#include <zero/behavior/nodes/AttachNode.h>
#include <zero/behavior/nodes/BlackboardNode.h>
#include <zero/behavior/nodes/InputActionNode.h>
#include <zero/behavior/nodes/MapNode.h>
#include <zero/behavior/nodes/MathNode.h>
#include <zero/behavior/nodes/MoveNode.h>
#include <zero/behavior/nodes/PlayerNode.h>
#include <zero/behavior/nodes/RegionNode.h>
#include <zero/behavior/nodes/RenderNode.h>
#include <zero/behavior/nodes/ShipNode.h>
#include <zero/behavior/nodes/TargetNode.h>
#include <zero/behavior/nodes/TimerNode.h>
#include <zero/zones/devastation/nodes/BounceShotQueryNode.h>
#include <zero/zones/devastation/nodes/CombatRoleNode.h>
#include <zero/zones/devastation/nodes/GetAttachTargetNode.h>
#include <zero/zones/devastation/nodes/GetSpawnNode.h>
#include <zero/zones/svs/nodes/BurstAreaQueryNode.h>
#include <zero/zones/svs/nodes/IncomingDamageQueryNode.h>

namespace zero {
namespace deva {

static std::unique_ptr<behavior::BehaviorNode> CreateRusherTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  BehaviorBuilder builder;

  // clang-format off
  builder
    .Sequence()
        .Child<CombatRoleQueryNode>(CombatRole::Rusher)
        .Child<BulletDistanceNode>("bullet_distance")
        .Selector()
            .Sequence()
                .Child<VisibilityQueryNode>("nearest_target_position")
                .InvertChild<DistanceThresholdNode>("self_position", "nearest_target_position", "bullet_distance")
                .End()
            .Sequence()
                .Child<PathDistanceQueryNode>("path_distance")
                .Child<LessOrEqualThanNode<float>>("path_distance", "bullet_distance")
                .Child<BounceShotQueryNode>(WeaponType::BouncingBullet, "nearest_target", 1.5f)
                .End()
            .End()
        .Parallel()
            .Sequence(CompositeDecorator::Success) // Use repel when incoming damage will kill us.
                .Child<ShipItemCountThresholdNode>(ShipItemType::Repel)
                .Child<TimerExpiredNode>("defense_timer")
                .Child<svs::IncomingDamageQueryNode>(5.0f, "incoming_damage")
                .Child<PlayerCurrentEnergyQueryNode>("self_energy")
                .Child<ScalarThresholdNode<float>>("incoming_damage", "self_energy")
                .Child<InputActionNode>(InputAction::Repel)
                .Child<TimerSetNode>("defense_timer", 100)
                .End()
            .Sequence(CompositeDecorator::Success) // Use burst when near a wall.
                .Child<ShipItemCountThresholdNode>(ShipItemType::Burst)
                .InvertChild<ShipWeaponCooldownQueryNode>(WeaponType::Bomb)
                .InvertChild<DistanceThresholdNode>("nearest_target_position", 10.0f)
                .Child<svs::BurstAreaQueryNode>()
                .Child<InputActionNode>(InputAction::Burst)
                .End()
            .Sequence() // Aim at target and shoot while seeking them.
                .Child<AimNode>(WeaponType::Bullet, "nearest_target", "aimshot")
                .Child<FaceNode>("aimshot")
                .Child<SeekNode>("aimshot", 0.0f, SeekNode::DistanceResolveType::Static)
                .Sequence(CompositeDecorator::Success) // Determine if a shot should be fired by using weapon trajectory and bounding boxes.
                    .InvertChild<TileQueryNode>(kTileSafeId)
                    .Child<InputActionNode>(InputAction::Bullet)
                    .End()
                .End() // End aim sequence
            .End() // End parallel
        .End(); // End top level
  // clang-format on

  return builder.Build();
}

static std::unique_ptr<behavior::BehaviorNode> CreateAnchorTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  BehaviorBuilder builder;

  // clang-format off
  builder
    .Selector()
        .End();
  // clang-format on

  return builder.Build();
}

std::unique_ptr<behavior::BehaviorNode> TestBehavior::CreateTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  srand((unsigned int)time(nullptr));

  BehaviorBuilder builder;
  Rectangle center_rect = Rectangle::FromPositionRadius(Vector2f(512, 512), 64.0f);

  // clang-format off
  builder
    .Selector()
        .Sequence() // Enter the specified ship if not already in it.
            .InvertChild<ShipQueryNode>("request_ship")
            .Child<ShipRequestNode>("request_ship")
            .End()
        .Sequence() // If we are attached to someone, detach.
            .Child<AttachedQueryNode>()
            .Child<DetachNode>()
            .End()
        .Sequence() // If we are in center safe, try to find a good attach target
            .Child<PlayerPositionQueryNode>("self_position")
            .Child<RectangleContainsNode>(center_rect, "self_position")
            .Child<TimerExpiredNode>("next_attach_tick")
            .Child<TimerSetNode>("next_attach_tick", 50)
            .Child<GetAttachTargetNode>(center_rect, "attach_target")
            .Child<AttachNode>("attach_target")
            .End()
        .Sequence() // Check if we are in enemy safe
            .Child<PlayerStatusQueryNode>(Status_Safety)
            .Child<GetSpawnNode>(GetSpawnNode::Type::Enemy, "enemy_spawn")
            .InvertChild<DistanceThresholdNode>("enemy_spawn", 2.0f)
            .Child<InputActionNode>(InputAction::Bullet)
            .End()
        .Sequence()
            .InvertChild<RectangleContainsNode>(center_rect, "self_position") // Let this return false when in center so we exit this sequence
            .Sequence()
                .Child<PlayerPositionQueryNode>("self_position")
                .Child<NearestTargetNode>("nearest_target") // TODO: Better targeting system for base.
                .Child<PlayerPositionQueryNode>("nearest_target", "nearest_target_position")
                .Child<RegionContainQueryNode>("nearest_target_position")
                .End()
            .Sequence(CompositeDecorator::Success)
                .Composite(CreateRusherTree(ctx))
                .Composite(CreateAnchorTree(ctx))
                .End()
            .Sequence() // Travel to enemy target
                .Child<BlackboardSetQueryNode>("nearest_target")
                .Child<GoToNode>("nearest_target_position")
                .End()
            .End()
        .Sequence() // Travel to enemy spawn
            .Child<GetSpawnNode>(GetSpawnNode::Type::Enemy, "enemy_spawn")
            .Child<GoToNode>("enemy_spawn")
            .Child<RenderPathNode>(Vector3f(0, 0, 0))
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

}  // namespace deva
}  // namespace zero

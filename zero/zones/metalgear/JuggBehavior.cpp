#include "JuggBehavior.h"

#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/behavior/nodes/AimNode.h>
#include <zero/behavior/nodes/BlackboardNode.h>
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

namespace zero {
namespace mg {

struct IncomingBombQueryNode : public behavior::BehaviorNode {
  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    constexpr float kNearbyDistanceSq = 6.0f * 6.0f;
    constexpr int kLevelRequirement = 1;

    float radius = ctx.bot->game->connection.settings.ShipSettings[self->ship].GetRadius();
    Rectangle self_collider = Rectangle::FromPositionRadius(self->position, radius + 3.0f / 16.0f);

    auto& weapon_man = ctx.bot->game->weapon_manager;
    for (size_t i = 0; i < weapon_man.weapon_count; ++i) {
      Weapon& weapon = weapon_man.weapons[i];

      if (weapon.frequency == self->frequency) continue;
      if (weapon.data.type != WeaponType::Bomb && weapon.data.type != WeaponType::ProximityBomb) continue;
      if (weapon.position.DistanceSq(self->position) > kNearbyDistanceSq) continue;
      if (weapon.data.level < kLevelRequirement) continue;

      Ray ray(weapon.position, Normalize(weapon.velocity));

      if (RayBoxIntersect(ray, self_collider, nullptr, nullptr)) {
        return behavior::ExecuteResult::Success;
      }
    }

    return behavior::ExecuteResult::Failure;
  }
};

std::unique_ptr<behavior::BehaviorNode> JuggBehavior::CreateTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  BehaviorBuilder builder;

  const Vector2f center(512, 512);

  // clang-format off
  builder
    .Selector()
        .Sequence() // Enter the specified ship if not already in it.
            .InvertChild<ShipQueryNode>("request_ship")
            .Child<ShipRequestNode>("request_ship")
            .End()
        .Sequence() // Warp back to center.
            .InvertChild<RegionContainQueryNode>(center)
            .Child<WarpNode>()
            .End()
        .Selector() // Choose to fight the player or follow waypoints.
            .Sequence() // Find nearest target and either path to them or seek them directly.
                .Sequence()
                    .Child<PlayerPositionQueryNode>("self_position")
                    .Child<NearestTargetNode>("nearest_target")
                    .Child<PlayerPositionQueryNode>("nearest_target", "nearest_target_position")
                    .End()
                .Selector()
                    .Sequence() // Use repels when in danger
                        .Child<ShipWeaponCapabilityQueryNode>(WeaponType::Repel)
                        .Child<TimerExpiredNode>("defense_timer")
                        .Child<IncomingBombQueryNode>()
                        .Child<InputActionNode>(InputAction::Repel)
                        .Child<TimerSetNode>("defense_timer", 100)
                        .End()
                    .Sequence()
                        .Child<InfluenceMapPopulateWeapons>()
                        .Child<InfluenceMapGradientDodge>()
                        .End()
                    .Sequence() // Path to target if they aren't immediately visible.
                        .InvertChild<VisibilityQueryNode>("nearest_target_position")
                        .Child<GoToNode>("nearest_target_position")
                        .End()
                    .Sequence() // Aim at target and shoot while seeking them.
                        .Child<AimNode>(WeaponType::Bomb, "nearest_target", "aimshot")
                        .Parallel()
                            .Selector() // Select between hovering around a territory position and seeking to enemy.
                                .Sequence()
                                    .Child<FindTerritoryPosition>("nearest_target", "leash_distance", "territory_position")
                                    .Sequence(CompositeDecorator::Success)
                                        .Child<PositionThreatQueryNode>("self_position", "self_threat", 15.0f, 2.25f)
                                        .Child<PositionThreatQueryNode>("territory_position", "territory_threat", 15.0f, 2.25f)
                                        .Child<RenderTextNode>("world_camera", "territory_position", [](ExecuteContext& ctx) {
                                          std::string str = std::string("Threat: ") + std::to_string(ctx.blackboard.ValueOr<float>("territory_threat", 0.0f));

                                          return RenderTextNode::Request(str, TextColor::White, Layer::TopMost, TextAlignment::Center);
                                        })
                                        .Child<ScalarThresholdNode<float>>("territory_threat", 0.2f)
                                        .Child<FindTerritoryPosition>("nearest_target", "leash_distance", "territory_position", true)
                                        .End()
                                    .Sequence(CompositeDecorator::Success)
                                        .InvertChild<ScalarThresholdNode<float>>("self_threat", 0.1f)
                                        .Child<VectorNode>("aimshot", "face_position")
                                        .End()
                                    .Child<ArriveNode>("territory_position", 15.0f)
                                    .Child<RectangleNode>("territory_position", Vector2f(2.0f, 2.0f), "territory_rect")
                                    .Child<RenderRectNode>("world_camera", "territory_rect", Vector3f(0.0f, 1.0f, 0.0f))
                                    .End()                            
                                .Sequence()
                                    .Child<VectorNode>("aimshot", "face_position")
                                    .Child<SeekNode>("aimshot", "leash_distance")
                                    .End()
                                .End()
                            .Sequence(CompositeDecorator::Success)
                                .InvertChild<TileQueryNode>(kTileSafeId)
                                .Sequence()
                                    .Child<ShotVelocityQueryNode>(WeaponType::Bomb, "bomb_fire_velocity")
                                    .Child<RayNode>("self_position", "bomb_fire_velocity", "bomb_fire_ray")
                                    .Child<PlayerBoundingBoxQueryNode>("nearest_target", "target_bounds", 1.5f)
                                    .Child<MoveRectangleNode>("target_bounds", "aimshot", "target_bounds")
                                    .Child<RenderRectNode>("world_camera", "target_bounds", Vector3f(1.0f, 0.0f, 0.0f))
                                    .Child<RenderRayNode>("world_camera", "bomb_fire_ray", 50.0f, Vector3f(1.0f, 1.0f, 0.0f))
                                    .Child<RayRectangleInterceptNode>("bomb_fire_ray", "target_bounds")
                                    .Child<InputActionNode>(InputAction::Bomb)
                                    .End()
                                .End()
                            .Sequence(CompositeDecorator::Success) // Face away from target so it can dodge while waiting for bomb cooldown.
                                .Child<ShipWeaponCooldownQueryNode>(WeaponType::Bomb)
                                .Child<PerpendicularNode>("nearest_target_position", "self_position", "away_dir", true)
                                .Child<VectorSubtractNode>("nearest_target_position", "self_position", "target_direction", true)
                                .Child<VectorAddNode>("away_dir", "target_direction", "away_dir", true)
                                .Child<VectorAddNode>("self_position", "away_dir", "away_pos")
                                .Child<VectorNode>("away_pos", "face_position")
                                .End()
                            .End()
                        .Sequence()
                          .Child<BlackboardSetQueryNode>("face_position")
                          .Child<FaceNode>("face_position")
                          .Child<BlackboardEraseNode>("face_position")
                          .End()
                        .End()
                    .End()
                .End()
            .Sequence() // Follow set waypoints.
                .Child<WaypointNode>("waypoints", "waypoint_index", "waypoint_position", 15.0f)
                .Selector()
                    .Sequence()
                        .InvertChild<VisibilityQueryNode>("waypoint_position")
                        .Child<GoToNode>("waypoint_position")
                        .End()
                    .Parallel()
                        .Child<FaceNode>("waypoint_position")
                        .Child<ArriveNode>("waypoint_position", 1.25f)
                        .End()
                    .End()
                .End()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

}  // namespace mg
}  // namespace zero

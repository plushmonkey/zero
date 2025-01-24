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
#include <zero/zones/svs/nodes/DynamicPlayerBoundingBoxQueryNode.h>
#include <zero/zones/svs/nodes/IncomingDamageQueryNode.h>

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

// Looks for nearby walls, find away vector, and seek to it.
// Returns failure if no wall is nearby.
struct SeekFromWallNode : public behavior::BehaviorNode {
  SeekFromWallNode(float search_distance) : search_distance(search_distance) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    Vector2f pos = self->position;

    constexpr Vector2f kSearchDirections[] = {Vector2f(0, -1), Vector2f(1, 0), Vector2f(0, 1), Vector2f(-1, 0)};

    auto& map = ctx.bot->game->connection.map;

    Vector2f away_vector;

    for (Vector2f direction : kSearchDirections) {
      auto cast = map.CastTo(self->position, self->position + direction * search_distance, self->frequency);

      if (cast.hit) {
        // We hit a wall, so move away from it.
        away_vector -= direction;
      }
    }

    if (away_vector.LengthSq() > 0.0f) {
      ctx.bot->bot_controller->steering.Seek(*ctx.bot->game, self->position + Normalize(away_vector) * 10.0f);
      return behavior::ExecuteResult::Success;
    }

    return behavior::ExecuteResult::Failure;
  }

  float search_distance = 0.0f;
};

std::unique_ptr<behavior::BehaviorNode> JuggBehavior::CreateTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  BehaviorBuilder builder;

  const Vector2f center(512, 512);

  // How fast we need to be moving toward the target in tiles for us to decide to shoot a bomb.
  // This stops us from shooting bombs slowly all the time.
  constexpr float kForwardSpeedBombRequirement = 1.5f;
  // Bypass the forward speed requirement if we are within this many tiles of the target.
  // This is to prevent us from not shooting when very near someone and circling around.
  constexpr float kNearbyBombBypass = 8.0f;

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
                    .Sequence()
                        .Child<DodgeIncomingDamage>(0.6f, 35.0f)
                        .End()
                    .Sequence() // Path to target if they aren't immediately visible.
                        .InvertChild<ShipTraverseQueryNode>("nearest_target_position")
                        .Child<GoToNode>("nearest_target_position")
                        .End()
                    .Sequence()
                        .InvertChild<TileQueryNode>(kTileIdSafe)
                        .Child<SeekFromWallNode>(3.0f) // If we are near a wall, seek away so we don't get stuck and become an easy target.
                        .End()
                    .Sequence() // Aim at target and shoot while seeking them.
                        .Child<AimNode>(WeaponType::Bomb, "nearest_target", "aimshot")
                        .Parallel() // Main update
                            .Sequence(CompositeDecorator::Success) // Determine main movement vectors
                                .Child<VectorNode>("aimshot", "face_position")
                                .Selector()
                                    .Sequence()
                                        .Child<ShipWeaponCooldownQueryNode>(WeaponType::Bomb)
                                        .Child<SeekNode>("nearest_target_position", "leash_distance", SeekNode::DistanceResolveType::Dynamic)
                                        .End()
                                    .Child<SeekNode>("aimshot", 0.0f, SeekNode::DistanceResolveType::Zero)
                                    .End()
                                .End()
                            .Sequence(CompositeDecorator::Success) // Always check incoming damage so we can use it in repel and portal sequences.
                                .Child<RepelDistanceQueryNode>("repel_distance")
                                .Child<svs::IncomingDamageQueryNode>("repel_distance", "incoming_damage")
                                .Child<PlayerCurrentEnergyQueryNode>("self_energy")
                                .End()
                            .Sequence(CompositeDecorator::Success) // Use repel when in danger.
                                .Child<ShipWeaponCapabilityQueryNode>(WeaponType::Repel)
                                .Child<TimerExpiredNode>("defense_timer")
                                .Child<ScalarThresholdNode<float>>("incoming_damage", "self_energy")
                                .Child<InputActionNode>(InputAction::Repel)
                                .Child<TimerSetNode>("defense_timer", 100)
                                .End()
                            .Sequence(CompositeDecorator::Success) // Fire bomb
                                .InvertChild<TileQueryNode>(kTileIdSafe)
                                .InvertChild<ShipWeaponCooldownQueryNode>(WeaponType::Bomb)
                                .Child<VectorSubtractNode>("aimshot", "self_position", "target_direction", true)
                                .Selector()
                                    .InvertChild<DistanceThresholdNode>("nearest_target_position", kNearbyBombBypass)
                                    .Sequence()
                                        .Child<PlayerVelocityQueryNode>("self_velocity")
                                        .Child<VectorDotNode>("self_velocity", "target_direction", "forward_velocity")
                                        .Child<ScalarThresholdNode<float>>("forward_velocity", kForwardSpeedBombRequirement)
                                        .End()
                                    .End()
                                .Child<ShotVelocityQueryNode>(WeaponType::Bomb, "bomb_fire_velocity")
                                .Child<RayNode>("self_position", "bomb_fire_velocity", "bomb_fire_ray")
                                .Child<svs::DynamicPlayerBoundingBoxQueryNode>("nearest_target", "target_bounds", 3.0f)
                                .Child<MoveRectangleNode>("target_bounds", "aimshot", "target_bounds")
                                .Child<RayRectangleInterceptNode>("bomb_fire_ray", "target_bounds")
                                .Child<InputActionNode>(InputAction::Bomb)
                                .End()
                            .Sequence(CompositeDecorator::Success) // Face away from target so it can dodge while waiting for bomb cooldown.
                                .Child<ShipWeaponCooldownQueryNode>(WeaponType::Bomb)
                                .Child<PerpendicularNode>("nearest_target_position", "self_position", "away_dir", true)
                                .Child<VectorSubtractNode>("nearest_target_position", "self_position", "target_direction", true)
                                .Child<VectorAddNode>("away_dir", "target_direction", "away_dir", true)
                                .Child<VectorAddNode>("self_position", "away_dir", "away_pos")
                                .Child<VectorNode>("away_pos", "face_position")
                                .End()
                            .End() // End main update parallel
                        .Sequence()
                          .Child<BlackboardSetQueryNode>("face_position")
                          .Child<RotationThresholdSetNode>(0.35f)
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
                        .InvertChild<ShipTraverseQueryNode>("waypoint_position")
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

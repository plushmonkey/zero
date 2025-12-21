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
#include <zero/behavior/nodes/RenderNode.h>
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
        .Child<DodgeIncomingDamage>(0.6f, 35.0f, 0.0f) 
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
            .Child<RenderPathNode>(Vector3f(1, 0, 0))
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

struct AvoidEnemyAimshotNode : public behavior::BehaviorNode {
  AvoidEnemyAimshotNode(const char* enemy_key) : enemy_key(enemy_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    auto opt_enemy = ctx.blackboard.Value<Player*>(enemy_key);
    if (!opt_enemy) return behavior::ExecuteResult::Failure;

    Player* enemy = *opt_enemy;
    if (!enemy || enemy->ship >= 8) return behavior::ExecuteResult::Failure;

    Game& game = *ctx.bot->game;
    float weapon_speed = behavior::GetWeaponSpeed(game, *enemy, WeaponType::Bullet);
    float alive_time = game.connection.settings.BulletAliveTime / 100.0f;
    Vector2f shot_velocity = enemy->velocity + enemy->GetHeading() * weapon_speed;
    Ray shot_ray(enemy->position, Normalize(shot_velocity));

    // How much we should multiply the radius by so we avoid slightly larger than our own ship bounding box.
    constexpr float kRadiusMultiplier = 1.75f;

    float self_radius = game.connection.settings.ShipSettings[self->ship].GetRadius();
    Vector2f self_half_extents(self_radius * kRadiusMultiplier, self_radius * kRadiusMultiplier);
    Rectangle self_collider(self->position - self_half_extents, self->position + self_half_extents);

    float dist = 0.0f;
    if (RayBoxIntersect(shot_ray, self_collider, &dist, nullptr)) {
      Vector2f hit_position = shot_ray.origin + shot_ray.direction * dist;
      Vector2f away = Perpendicular(shot_ray.direction);
      // TODO: Might be good to include existing velocity in the calculation so we don't end up trying to reverse our
      // high movement.
      // Determine which way to dodge. This will choose the fastest perpendicular movement to make the
      // collider not hit the ray.
      float dot = Normalize(self->position - hit_position).Dot(Perpendicular(shot_ray.direction));

      if (dot < 0.0f) {
        away *= -1.0f;
      }

      // Combine perpendicular dodge with backwards movement to increase chances of dodging.
      Vector2f movement = Normalize(away + shot_ray.direction);
      Vector2f target_position = self->position + movement * 10.0f;

      ctx.bot->bot_controller->steering.Seek(*ctx.bot->game, target_position);

      return behavior::ExecuteResult::Success;
    }

    return behavior::ExecuteResult::Failure;
  }
  const char* enemy_key = nullptr;
};

static std::unique_ptr<behavior::BehaviorNode> CreateAimTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  BehaviorBuilder builder;

  constexpr float kBulletEnergyCost = 0.9f;
  // If we are below this percentage of energy, adjust our heading to be away from the target so we can dodge better
  constexpr float kDodgeFaceEnergyThreshold = 0.65f;
  constexpr float kDodgeEnemyAimshotEnergyThreshold = 0.65f;
  constexpr float kNearDistance = 20.0f;

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
                    .Child<SeekNode>("aimshot", 5.0f, SeekNode::DistanceResolveType::Zero)
                    .End()
                .Sequence() // If enemy is very close to us, ab away fast.
                    .InvertChild<DistanceThresholdNode>("nearest_target_position", kNearDistance)
                    .Child<BlackboardEraseNode>("face_position")
                    .Selector() // Choose how to escape
                        .Sequence() // Dodge away from enemy's heading direction if we have low energy
                            .InvertChild<PlayerEnergyPercentThresholdNode>(kDodgeEnemyAimshotEnergyThreshold)
                            .Child<InputActionNode>(InputAction::Afterburner)
                            .Child<AvoidEnemyAimshotNode>("nearest_target")
                            .End()
                        .Child<SeekNode>("nearest_target_position", "leash_distance", SeekNode::DistanceResolveType::Dynamic)
                        .End()
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
            .InvertChild<PlayerEnergyPercentThresholdNode>(kDodgeFaceEnergyThreshold)
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
            .Child<NearestTargetNode>("nearest_target", true)
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

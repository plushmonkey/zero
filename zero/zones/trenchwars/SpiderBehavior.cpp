#include "SpiderBehavior.h"

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

namespace zero {
namespace tw {

// This looks for a good position to sit inside the flag room and aim.
struct FlagroomAimPositionNode : public behavior::BehaviorNode {
  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    Vector2f aim_position(512, 273);

    if (ctx.bot->game->GetMap().CastTo(self->position, aim_position, self->frequency).hit) {
      return behavior::ExecuteResult::Success;
    }

    return behavior::ExecuteResult::Success;
  }
};

struct InFlagroomNode : public behavior::BehaviorNode {
  InFlagroomNode(const char* position_key) : position_key(position_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto opt_tw = ctx.blackboard.Value<TrenchWars*>("tw");
    if (!opt_tw) return behavior::ExecuteResult::Failure;

    auto opt_position = ctx.blackboard.Value<Vector2f>(position_key);
    if (!opt_position) return behavior::ExecuteResult::Failure;

    bool in_fr = (*opt_tw)->fr_bitset.Test(*opt_position);

    return in_fr ? behavior::ExecuteResult::Success : behavior::ExecuteResult::Failure;
  }

  const char* position_key = nullptr;
};

// Returns success if the target player has some number of teammates within the flag room.
struct FlagroomPresenceNode : public behavior::BehaviorNode {
  FlagroomPresenceNode(u32 count) : count_check(count) {}
  FlagroomPresenceNode(const char* count_key) : count_key(count_key) {}
  FlagroomPresenceNode(const char* count_key, const char* player_key) : count_key(count_key), player_key(player_key) {}
  FlagroomPresenceNode(u32 count, const char* player_key) : count_check(count), player_key(player_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto& pm = ctx.bot->game->player_manager;
    auto player = pm.GetSelf();

    if (player_key) {
      auto opt_player = ctx.blackboard.Value<Player*>(player_key);
      if (!opt_player) return behavior::ExecuteResult::Failure;

      player = *opt_player;
    }

    if (!player) return behavior::ExecuteResult::Failure;

    u32 count = 0;
    u32 count_threshold = count_check;

    if (count_key) {
      auto opt_count_threshold = ctx.blackboard.Value<u32>(count_key);
      if (!opt_count_threshold) return behavior::ExecuteResult::Failure;

      count_threshold = *opt_count_threshold;
    }

    auto opt_tw = ctx.blackboard.Value<TrenchWars*>("tw");
    if (!opt_tw) return behavior::ExecuteResult::Failure;

    const auto& bitset = (*opt_tw)->fr_bitset;

    for (size_t i = 0; i < pm.player_count; ++i) {
      Player* check_player = pm.players + i;

      if (check_player->ship >= 8) continue;
      if (check_player->frequency != player->frequency) continue;
      if (!bitset.Test(check_player->position)) continue;

      if (++count > count_threshold) {
        return behavior::ExecuteResult::Success;
      }
    }

    return behavior::ExecuteResult::Failure;
  }

  u32 count_check = 0;
  const char* player_key = nullptr;
  const char* count_key = nullptr;
};

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
        .Sequence(CompositeDecorator::Success) // Always check incoming damage so we can use it in repel and portal sequences.
            .Child<svs::IncomingDamageQueryNode>(kRepelDistance, "incoming_damage")
            .Child<PlayerCurrentEnergyQueryNode>("self_energy")
            .End()
        .Sequence(CompositeDecorator::Success) // If we are in danger but can't repel, use our portal.
            .InvertChild<ShipItemCountThresholdNode>(ShipItemType::Repel)
            .Child<ShipPortalPositionQueryNode>() // Check if we have a portal down.
            .Child<ScalarThresholdNode<float>>("incoming_damage", "self_energy")
            .Child<TimerExpiredNode>("defense_timer")
            .Child<InputActionNode>(InputAction::Warp)
            .Child<TimerSetNode>("defense_timer", 100)
            .End()
        .Sequence(CompositeDecorator::Success) // Use repel when in danger.
            .Child<ShipWeaponCapabilityQueryNode>(WeaponType::Repel)
            .Child<TimerExpiredNode>("defense_timer")
            .Child<ScalarThresholdNode<float>>("incoming_damage", "self_energy")
            .Child<InputActionNode>(InputAction::Repel)
            .Child<TimerSetNode>("defense_timer", 100)
            .End()
        .Sequence(CompositeDecorator::Invert) // Check if enemy is very low energy and close to use. Don't bother dodging if they are rushing us with low energy.
            .Child<PlayerEnergyQueryNode>("nearest_target", "nearest_target_energy")
            .InvertChild<ScalarThresholdNode<float>>("nearest_target_energy", kLowEnergyThreshold)
            .InvertChild<DistanceThresholdNode>("nearest_target_position", "self_position", kNearbyEnemyThreshold)
            .End()
        .Child<DodgeIncomingDamage>(0.4f, 35.0f)
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
                    .Child<SeekNode>("aimshot", "leash_distance", SeekNode::DistanceResolveType::Dynamic)
                    .End()
                .Child<SeekNode>("aimshot", 0.0f, SeekNode::DistanceResolveType::Zero)
                .End()
            .Sequence(CompositeDecorator::Success) // Determine if a shot should be fired by using weapon trajectory and bounding boxes.
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

std::unique_ptr<behavior::BehaviorNode> SpiderBehavior::CreateTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  BehaviorBuilder builder;

  // This is how far away to check for enemies that are rushing at us with low energy.
  // We will stop dodging and try to finish them off if they are within this distance and low energy.
  constexpr float kNearbyEnemyThreshold = 10.0f;

  // clang-format off
  builder
    .Selector()
        .Sequence() // Enter the specified ship if not already in it.
            .InvertChild<ShipQueryNode>("request_ship")
            .Child<ShipRequestNode>("request_ship")
            .End()
        .Selector() // Choose to fight the player or follow waypoints.
            .Sequence() // Find nearest target and either path to them or seek them directly.
                .Sequence()
                    .Child<PlayerPositionQueryNode>("self_position")
                    .Child<svs::NearestMemoryTargetNode>("nearest_target")
                    .Child<PlayerPositionQueryNode>("nearest_target", "nearest_target_position")
                    .End()
                .Sequence(CompositeDecorator::Success) // If we have a portal but no location, lay one down.
                    .Child<ShipItemCountThresholdNode>(ShipItemType::Portal, 1)
                    .InvertChild<ShipPortalPositionQueryNode>()
                    .Child<InputActionNode>(InputAction::Portal)
                    .End()
                .Selector()
                    .Composite(CreateDefensiveTree())
#if 0
                    .Sequence() // If the nearest enemy is very close to us, attack them.
                        .Child<VisibilityQueryNode>("nearest_target_position")
                        .InvertChild<DistanceThresholdNode>("self_position", "nearest_target_position", kNearbyEnemyThreshold)
                        .Composite(CreateOffensiveTree("nearest_target", "nearest_target_position"))
                        .End()
#endif
                    .Sequence() // Go directly to the flag room if we aren't there.
                        .InvertChild<InFlagroomNode>("self_position")
                        .Child<GoToNode>("tw_flag_position")
                        .Child<RenderPathNode>(Vector3f(0.0f, 1.0f, 0.5f))
                        .End()
                    .Sequence() // Go to enemy and attack if they are in the flag room.
                        .Child<InFlagroomNode>("nearest_target_position")
                        .Selector()
                            .Sequence()
                                .InvertChild<VisibilityQueryNode>("nearest_target_position")
                                .Child<GoToNode>("nearest_target_position")
                                .End()
                            .Composite(CreateOffensiveTree("nearest_target", "nearest_target_position"))
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
                        .Child<RenderPathNode>(Vector3f(0.0f, 0.5f, 1.0f))
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

}  // namespace tw
}  // namespace zero

#include "BaseBehavior.h"

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
#include <zero/zones/extremegames/ExtremeGames.h>
#include <zero/zones/extremegames/nodes/BaseNode.h>
#include <zero/zones/svs/nodes/BurstAreaQueryNode.h>
#include <zero/zones/svs/nodes/DynamicPlayerBoundingBoxQueryNode.h>
#include <zero/zones/svs/nodes/FindNearestGreenNode.h>
#include <zero/zones/svs/nodes/IncomingDamageQueryNode.h>
#include <zero/zones/svs/nodes/MemoryTargetNode.h>
#include <zero/zones/svs/nodes/NearbyEnemyWeaponQueryNode.h>

using namespace zero::behavior;

namespace zero {
namespace eg {

constexpr u32 kAttachCooldown = 100;

// nonflagger_multiplier is how much we should multiply the distance for non-flaggers.
// A value over 1.0f will prioritize flaggers, but won't completely ignore people who are nearby that might attack us
// while we chase the flagger.
// nonflagger_distance_req sets the distance requirement for non-flaggers. It will ignore non-flaggers over this
// distance. This will only find someone on radar.
struct FindBestEnemyCenterNode : public BehaviorNode {
  FindBestEnemyCenterNode(const char* output_key, float nonflagger_multiplier, float nonflagger_distance_req)
      : output_key(output_key),
        nonflagger_multiplier(nonflagger_multiplier),
        nonflagger_distance_req(nonflagger_distance_req) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto& game = ctx.bot->game;

    Player* self = game->player_manager.GetSelf();
    if (!self) return ExecuteResult::Failure;

    RegionRegistry& region_registry = *ctx.bot->bot_controller->region_registry;

    Player* best_target = nullptr;
    float closest_dist_sq = std::numeric_limits<float>::max();

    auto opt_eg = ctx.blackboard.Value<ExtremeGames*>("eg");
    if (!opt_eg) return ExecuteResult::Failure;

    ExtremeGames* eg = *opt_eg;

    for (size_t i = 0; i < game->player_manager.player_count; ++i) {
      Player* player = game->player_manager.players + i;

      if (player->ship >= 8) continue;
      if (player->frequency == self->frequency) continue;
      if (player->IsRespawning()) continue;
      if (player->position == Vector2f(0, 0)) continue;
      if (!game->player_manager.IsSynchronized(*player)) continue;
      if (!region_registry.IsConnected(self->position, player->position)) continue;
      if (!ctx.bot->game->radar.InRadarView(player->position)) continue;
      if (game->GetMap().GetTileId(player->position) == kTileIdSafe) continue;
      if (player->position.DistanceSq(self->position) > nonflagger_distance_req * nonflagger_distance_req) continue;
      if (eg->GetBaseFromPosition(player->position) != -1) continue;

      bool in_safe = game->connection.map.GetTileId(player->position) == kTileIdSafe;
      if (in_safe) continue;

      float dist_sq = player->position.DistanceSq(self->position);

      // Prioritize killing flaggers, but don't ignore people who are nearby that might kill us while we chase flagger.
      if (player->flags == 0) {
        dist_sq *= nonflagger_multiplier;
      }

      if (dist_sq < closest_dist_sq) {
        closest_dist_sq = dist_sq;
        best_target = player;
      }
    }

    if (!best_target) {
      ctx.blackboard.Erase(output_key);
      return ExecuteResult::Failure;
    }

    ctx.blackboard.Set(output_key, best_target);
    return ExecuteResult::Success;
  }

  const char* output_key = nullptr;
  float nonflagger_multiplier = 2.0f;
  float nonflagger_distance_req = 30.0f;
};

static std::unique_ptr<BehaviorNode> CreateCenterAttackTree(ExecuteContext& ctx) {
  BehaviorBuilder builder;

  // Multiply distance of non-flaggers by this in nearest enemy node so it prioritizes flaggers.
  constexpr float kNonflaggerDistanceMultiplier = 2.0f;
  // Ignore non-flaggers over this distance away
  constexpr float kNonflaggerDistanceRequirement = 30.0f;

  // clang-format off
  builder
    .Sequence()
        .Child<FindBestEnemyCenterNode>("nearest_target", kNonflaggerDistanceMultiplier, kNonflaggerDistanceRequirement)
        .Child<PlayerPositionQueryNode>("nearest_target", "nearest_target_position")
        .Child<BulletDistanceNode>("bullet_distance")
        .Sequence(CompositeDecorator::Success) // If we have a portal but no location, lay one down.
            .Child<ShipItemCountThresholdNode>(ShipItemType::Portal, 1)
            .InvertChild<ShipPortalPositionQueryNode>()
            .Child<InputActionNode>(InputAction::Portal)
            .End()
        .Sequence(CompositeDecorator::Success) // Enable multifire if disabled
            .Child<ShipCapabilityQueryNode>(ShipCapability_Multifire)
            .InvertChild<ShipMultifireQueryNode>()
            .Child<InputActionNode>(InputAction::Multifire)
            .End()
        .Selector()
            .Sequence() // Path to target
                .InvertChild<VisibilityQueryNode>("nearest_target_position")
                .Child<GoToNode>("nearest_target_position")
                .End()
            .Sequence()    
                .Child<AimNode>(WeaponType::Bomb, "nearest_target", "bomb_aimshot")
                .Child<AimNode>(WeaponType::Bullet, "nearest_target", "aimshot")
                .Parallel() // Main update
                    .Sequence(CompositeDecorator::Success)
                        .Child<VectorNode>("aimshot", "face_position")
                        .Selector() // Rush or keep distance
                            .Sequence() // Rush at target if we have more energy
                                .Child<PlayerEnergyQueryNode>("nearest_target", "target_energy")
                                .Child<PlayerEnergyQueryNode>("self_energy")
                                .Child<GreaterOrEqualThanNode<float>>("self_energy", "target_energy")
                                .Child<SeekNode>("aimshot")
                                .End()
                            .Child<SeekNode>("aimshot", "leash_distance", SeekNode::DistanceResolveType::Dynamic)
                            .End()
                        .End()
                    .Sequence(CompositeDecorator::Success) // Always check incoming damage so we can use it in repel and portal sequences.
                        .Child<RepelDistanceQueryNode>("repel_distance")
                        .Child<svs::IncomingDamageQueryNode>("repel_distance", "incoming_damage")
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
                    .Sequence(CompositeDecorator::Success) // Fire bomb
                        .InvertChild<TileQueryNode>(kTileIdSafe)
                        .InvertChild<ShipWeaponCooldownQueryNode>(WeaponType::Bomb)
                        .Child<ShotVelocityQueryNode>(WeaponType::Bomb, "bomb_fire_velocity")
                        .Child<RayNode>("self_position", "bomb_fire_velocity", "bomb_fire_ray")
                        .Child<svs::DynamicPlayerBoundingBoxQueryNode>("nearest_target", "target_bounds", 3.0f)
                        .Child<MoveRectangleNode>("target_bounds", "bomb_aimshot", "target_bounds")
                        .Child<RayRectangleInterceptNode>("bomb_fire_ray", "target_bounds")
                        .Child<InputActionNode>(InputAction::Bomb)
                        .End()
                    .Sequence(CompositeDecorator::Success) // Fire bullet
                        .InvertChild<TileQueryNode>(kTileIdSafe)
                        .InvertChild<InputQueryNode>(InputAction::Bomb)
                        .InvertChild<DistanceThresholdNode>("nearest_target_position", "bullet_distance") // Only fire bullets when close
                        .Child<ShotVelocityQueryNode>(WeaponType::Bullet, "bullet_fire_velocity")
                        .Child<RayNode>("self_position", "bullet_fire_velocity", "bullet_fire_ray")
                        .Child<svs::DynamicPlayerBoundingBoxQueryNode>("nearest_target", "target_bounds", 3.0f)
                        .Child<MoveRectangleNode>("target_bounds", "aimshot", "target_bounds")
                        .Child<RayRectangleInterceptNode>("bullet_fire_ray", "target_bounds")
                        .Child<InputActionNode>(InputAction::Bullet)
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
        .End();
  // clang-format on

  return builder.Build();
}

static std::unique_ptr<BehaviorNode> CreateBaseAttackTree(ExecuteContext& ctx) {
  BehaviorBuilder builder;

  // How many tiles we should try to avoid teammtes. This keeps us from clumping up.
  constexpr float kTeamAvoidance = 8.0f;

  // clang-format off
  builder
    .Sequence()
        .Child<FindBestBaseEntranceNode>("best_base_entrance")
        .Child<PlayerPositionQueryNode>("self_position")
        .Sequence(CompositeDecorator::Success) // Avoid team if we aren't in flagroom so we don't get stuck
            .InvertChild<InFlagroomNode>("self_position")
            .Child<AvoidTeamNode>(kTeamAvoidance)
            .End()
        .Selector()
            .Sequence() // If we aren't in base, try attaching or pathing to entrance
                .InvertChild<SameBaseNode>("self_position", "best_base_entrance")
                .Selector()
                    .Sequence() // Try attaching to a teammate if we aren't in base. TODO: Should find most forward player
                        .Child<TimerExpiredNode>("attach_cooldown")
                        .Child<FindBestBaseTeammateNode>("best_attach_teammate") // TODO: Should limit lookup to target base
                        .Child<PlayerPositionQueryNode>("best_attach_teammate", "best_attach_teammate_position")
                        .Child<SameBaseNode>("best_base_entrance", "best_attach_teammate_position") // Only attach if they are in our target base
                        .Child<AttachNode>("best_attach_teammate")
                        .Child<TimerSetNode>("attach_cooldown", kAttachCooldown)
                        .End()
                    .Child<GoToNode>("best_base_entrance")
                    .End()
                .End()
            .Sequence() // If we are in base, attack nearest target. TODO: Implement rushing/anchor
                .Child<FindNearestEnemyInBaseNode>("nearest_player")
                .Child<PlayerPositionQueryNode>("nearest_player", "nearest_target_position")
                .Child<SameBaseNode>("self_position", "nearest_target_position")
                .Sequence(CompositeDecorator::Success) 
                    //.InvertChild<VisibilityQueryNode>("nearest_target_position")
                    .Child<GoToNode>("nearest_target_position")
                    .End()
                .Selector(CompositeDecorator::Success)
                    .Sequence()
                        .Child<ExecuteNode>([](ExecuteContext& ctx) { // Determine if we should be shooting bullets.
                          auto self = ctx.bot->game->player_manager.GetSelf();
                          if (!self) return ExecuteResult::Failure;

                          float path_distance = ctx.bot->bot_controller->current_path.GetRemainingDistance();

                          s32 alive_time = ctx.bot->game->connection.settings.BulletAliveTime;
                          float weapon_speed = GetWeaponSpeed(*ctx.bot->game, *self, WeaponType::Bullet);
                          float weapon_distance = weapon_speed * (alive_time / 100.0f) * 0.75f;

                          Vector2f next = ctx.bot->bot_controller->current_path.GetNext();
                          Vector2f forward = next - self->position;

                          // Don't shoot if we aren't aiming ahead in the path.
                          if (forward.Dot(self->GetHeading()) < 0.0f) return ExecuteResult::Failure;
                          // Don't shoot if we aren't moving forward.
                          if (self->velocity.Dot(forward) < 0.0f) return ExecuteResult::Failure;

                          return path_distance <= weapon_distance ? ExecuteResult::Success : ExecuteResult::Failure;
                        })
                        .Child<InputActionNode>(InputAction::Bullet)
                        .End()
                    .Sequence()
                        .InvertChild<ShipWeaponCooldownQueryNode>(WeaponType::Bomb)
                        .Child<ExecuteNode>([](ExecuteContext& ctx) { // Determine if we should be shooting bombs.
                          auto self = ctx.bot->game->player_manager.GetSelf();
                          if (!self) return ExecuteResult::Failure;

                          float path_distance = ctx.bot->bot_controller->current_path.GetRemainingDistance();

                          s32 alive_time = ctx.bot->game->connection.settings.BombAliveTime;
                          float weapon_speed = GetWeaponSpeed(*ctx.bot->game, *self, WeaponType::Bomb);
                          float weapon_distance = weapon_speed * (alive_time / 100.0f) * 0.75f;

                          Vector2f next = ctx.bot->bot_controller->current_path.GetNext();
                          Vector2f forward = next - self->position;

                          // Don't shoot if we aren't aiming ahead in the path.
                          if (forward.Dot(self->GetHeading()) < 0.0f) return ExecuteResult::Failure;
                          
                          return path_distance <= weapon_distance ? ExecuteResult::Success : ExecuteResult::Failure;
                        })
                        .Child<InputActionNode>(InputAction::Bomb)
                        .End()
                .End()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

static std::unique_ptr<BehaviorNode> CreateFlagPickupTree(ExecuteContext& ctx) {
  BehaviorBuilder builder;

  // Begin by checking if there's a nearby flagger, chase and kill them if they are on radar.
  // Fall back to looking for nearest flag and go pick it up.

  // TODO: Stop cheating and actually have to look for flags.

  // clang-format off
  builder
    .Sequence()
        .Selector() // Choose between attacking and collecting flags
            .Composite(CreateCenterAttackTree(ctx))
            .Sequence()
                .Child<NearestFlagNode>(NearestFlagNode::Type::Unclaimed, "nearest_flag")
                .Child<FlagPositionQueryNode>("nearest_flag", "nearest_flag_position")
                .Child<GoToNode>("nearest_flag_position")
                .End()
        .End();
  // clang-format on

  return builder.Build();
}

// This tree will attach to a teammate if they are in the established base, otherwise it will go find the best base.
static std::unique_ptr<BehaviorNode> CreateFlagProtectTree(ExecuteContext& ctx) {
  BehaviorBuilder builder;

  // If we have at least this many flags, we should try to protect them in a base.
  constexpr u16 kProtectFlagCount = 4;

  // clang-format off
  builder
    .Sequence()
        .Child<FlagCarryCountQueryNode>("self_flag_count")
        .Child<ScalarThresholdNode<u16>>("self_flag_count", kProtectFlagCount)
        .Child<PlayerPositionQueryNode>("self_position")
        .InvertChild<InBaseNode>("self_position")
        .Selector()
            .Sequence() // Try to attach to a teammate in a base.
                .Child<PlayerEnergyPercentThresholdNode>(1.0f)
                .Child<FindBestBaseTeammateNode>("base_teammate")
                .Child<TimerExpiredNode>("attach_cooldown")
                .Child<TimerSetNode>("attach_cooldown", kAttachCooldown)
                .Child<AttachNode>("base_teammate")
                .End()
            .Sequence() // Path to a base.
                .Child<FindBestBaseEntranceNode>("best_entrance")
                .Child<GoToNode>("best_entrance")
                .End()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

static std::unique_ptr<BehaviorNode> CreateFlagManagementTree(ExecuteContext& ctx) {
  BehaviorBuilder builder;

  // If we have at least this many flags, we should try to protect them in a base.
  constexpr u16 kProtectFlagCount = 4;

  // If we are outside of base and have enough flags, try to protect them
  // If we are inside base and have flags, go to flagroom to drop them.
  // Go pickup flags otherwise.

  // clang-format off
  builder
    .Sequence()
        .Child<PlayerPositionQueryNode>("self_position")
        .Child<FlagCarryCountQueryNode>("self_flag_count")
        .Selector()
            .Sequence() // If we are in base with any number of flags, go to flagroom to drop them.
                .Child<InBaseNode>("self_position")
                .Child<ScalarThresholdNode<u16>>("self_flag_count", 1)
                .Selector()
                    .Sequence() // If we have flags and control base, go drop them in fr. TODO: Determine if we should become anchor/drop
                        .Child<BaseTeamControlQueryNode>("self_position", "control_freq")
                        .Child<PlayerFrequencyQueryNode>("self_freq")
                        .Child<EqualityNode<u16>>("self_freq", "control_freq")
                        .Child<BaseFlagroomPositionNode>("self_position", "flagroom_position")
                        .Child<GoToNode>("flagroom_position")
                        .End()
                    .Sequence() // We have flags and we are attacking a base. TODO: Become anchor or neut flags beside anchor
                        .Composite(CreateBaseAttackTree(ctx)) // TODO: Just rush at them for now since we don't have anything implemented.
                        .End()
                    .End()
                .End()
            .Sequence() // If we have too many flags or can't find more, protect them
                .Selector()
                    .Child<ScalarThresholdNode<u16>>("self_flag_count", kProtectFlagCount)
                    .InvertChild<NearestFlagNode>(NearestFlagNode::Type::Unclaimed, "nearest_flag") // Check if there any flags remaining
                    .End()
                .Composite(CreateFlagProtectTree(ctx))
                .End()
            .Composite(CreateFlagPickupTree(ctx))
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

std::unique_ptr<BehaviorNode> BaseBehavior::CreateTree(ExecuteContext& ctx) {
  BehaviorBuilder builder;

  // TODO: This is just a test behavior. Needs to be updated to choose to fight / collect flags / attack / defend base
  // TODO: Fighting in base with anchor and rushers

  // clang-format off
  builder
    .Selector()
        .Sequence() // Enter the specified ship if not already in it.
            .InvertChild<ShipQueryNode>("request_ship")
            .Child<ShipRequestNode>("request_ship")
            .End()
        .Sequence() // Do nothing as a spectator
            .Child<ShipQueryNode>(8)
            .End()
        .Sequence() // Detach if we are attached.
            .Child<AttachedQueryNode>()
            .Child<TimerExpiredNode>("attach_cooldown")
            .Child<TimerSetNode>("attach_cooldown", kAttachCooldown)
            .Child<DetachNode>()
            .End()
        .Parallel()
            .Child<BaseFlagCountQueryNode>()
            .SuccessChild<DodgeIncomingDamage>(0.3f, 14.0f, 0.0f)
            .Selector()
                .Composite(CreateFlagManagementTree(ctx))
                .Composite(CreateBaseAttackTree(ctx))
                .End()
            .End()
        .Sequence() // Fall back to moving to center to try to find some action.
            .Child<GoToNode>(Vector2f(512, 512))
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

}  // namespace eg
}  // namespace zero

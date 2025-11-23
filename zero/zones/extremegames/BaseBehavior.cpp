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

/*

TODO:

This is not complete. It's just the foundation for making it function by detecting bases and collecting flags.

Dropping flags and neuting flags is not properly implemented.
Combat is not really implemented. Both center and basing combat is rudimentary.
Center flag fetching is not very good. The combat is bad and the bots need team-avoidance steering.
Detecting flanking and deciding to flank needs to be implemented.
Should collect flags if the nearest one is very close. Currently just follows basic guideline of collecting some flags and going to a base.

*/

using namespace zero::behavior;

namespace zero {
namespace eg {

enum class GameRole {
  CollectFlags,
  AttackBase,
  DefendBase,
  DropFlags,
  NeutFlags,
};

struct GameRoleDecideNode : public BehaviorNode {
  GameRoleDecideNode(const char* output_key) : output_key(output_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self || self->ship >= 8) return ExecuteResult::Failure;

    auto opt_eg = ctx.blackboard.Value<ExtremeGames*>("eg");
    if (!opt_eg) return ExecuteResult::Failure;

    GameRole role = GameRole::CollectFlags;

    ExtremeGames* eg = *opt_eg;

    size_t base_index = eg->GetBaseFromPosition(self->position);
    if (base_index < eg->bases.size()) {
      MapBase& base = eg->bases[base_index];
      BaseState& state = eg->base_states[base_index];

      bool has_base_control = self->frequency == state.controlling_freq;
      u32 controlling_flag_count = state.flag_controlling_dropped_count + state.flag_controlling_carried_count;
      u32 attacking_flag_count = state.flag_attacking_dropped_count + state.flag_attacking_carried_count;
      u32 total_base_flags = controlling_flag_count + attacking_flag_count + state.flag_unclaimed_dropped_count;

      // The base we're in has flags in it, so stay in it for now.
      if (total_base_flags > 0) {
        role = has_base_control ? GameRole::DefendBase : GameRole::AttackBase;

        if (has_base_control && self->flags > 0) {
          role = GameRole::DropFlags;
        }
      }
    } else {
      // If we have at least this many flags, we should try to protect them in a base.
      constexpr u16 kProtectFlagCount = 4;

      // We aren't in a base, so decide if we should collect flags, attack enemy base, or defend our base.

      if (self->flags >= kProtectFlagCount) {
        role = GameRole::DefendBase;
      } else {
        constexpr float kProtectBasePenetrationThreshold = 0.75f;

        bool enemy_base_exists = false;
        bool team_base_exists = false;

        // Go through each base and see if any we control are dangerously under attack.
        for (size_t i = 0; i < eg->bases.size(); ++i) {
          BaseState& state = eg->base_states[i];

          if (state.controlling_freq == self->frequency &&
              state.attacking_penetration_percent >= kProtectBasePenetrationThreshold) {
            role = GameRole::DefendBase;
          }

          if (state.controlling_freq != 0xFFFF && state.controlling_freq != self->frequency) {
            enemy_base_exists = true;
          } else if (state.controlling_freq == self->frequency) {
            team_base_exists = true;
          }
        }

        // If we still want to collect flags, then make sure there are some flags dropped in public area.
        if (role == GameRole::CollectFlags) {
          bool has_public_flag = false;

          for (size_t i = 0; i < ctx.bot->game->flag_count; ++i) {
            GameFlag* flag = ctx.bot->game->flags + i;

            if ((flag->flags & GameFlag_Dropped) && flag->owner != self->frequency &&
                eg->GetBaseFromPosition(flag->position) == -1) {
              has_public_flag = true;
              break;
            }
          }

          // If there are no public flags to collect, go attack enemy.
          // If there is no enemy then go defend our own base.
          // TODO: Attack flag carriers in public
          if (!has_public_flag) {
            if (enemy_base_exists) {
              // If there are no public flags and the enemy is in a base, attack it.
              role = GameRole::AttackBase;
            } else if (team_base_exists) {
              // If there are no public flags and the enemy isn't in a base, defend ours.
              role = GameRole::DefendBase;
            }
          }
        }
      }
    }

    ctx.blackboard.Set(output_key, role);

    return ExecuteResult::Success;
  }

  const char* output_key = nullptr;
};

struct GameRoleEqualityNode : public BehaviorNode {
  GameRoleEqualityNode(const char* role_key, GameRole role) : role_key(role_key), role(role) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto opt_role_check = ctx.blackboard.Value<GameRole>(role_key);
    if (!opt_role_check) return ExecuteResult::Failure;
    GameRole role_check = *opt_role_check;

    if (role_check != role) return ExecuteResult::Failure;

    return ExecuteResult::Success;
  }

  GameRole role = GameRole::CollectFlags;
  const char* role_key = nullptr;
};

enum class CombatRole {
  Anchor,
  Rusher,
};

// base_position_key is any position inside of the target base.
struct CombatRoleDecideNode : public BehaviorNode {
  CombatRoleDecideNode(const char* base_position_key, const char* output_key, const char* anchor_output_key)
      : base_position_key(base_position_key), output_key(output_key), anchor_output_key(anchor_output_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self || self->ship >= 8) return ExecuteResult::Failure;

    auto opt_base_position = ctx.blackboard.Value<Vector2f>(base_position_key);
    if (!opt_base_position) return ExecuteResult::Failure;

    auto opt_eg = ctx.blackboard.Value<ExtremeGames*>("eg");
    if (!opt_eg) return ExecuteResult::Failure;

    ExtremeGames* eg = *opt_eg;

    size_t base_index = eg->GetBaseFromPosition(*opt_base_position);
    CombatRole role = CombatRole::Rusher;

    if (base_index >= eg->bases.size()) return ExecuteResult::Failure;

    BaseState& state = eg->base_states[base_index];
    Player* anchor = FindBestAnchor(ctx.bot->game->player_manager, *self, state);

    if (anchor) {
      ctx.blackboard.Set(anchor_output_key, anchor);

      if (anchor->id == self->id) {
        role = CombatRole::Anchor;
      }
    } else {
      ctx.blackboard.Erase(anchor_output_key);
    }

    ctx.blackboard.Set(output_key, role);

    return ExecuteResult::Success;
  }

  Player* FindBestAnchor(PlayerManager& pm, const Player& self, BaseState& state) {
    constexpr float kMaxDistanceFromEnemy = 0.2f;

    Player* best_anchor = nullptr;
    float best_distance_from_enemy = 0.0f;

    bool controlling = state.controlling_freq == self.frequency;

    for (auto& player_data : state.player_data) {
      if (player_data.frequency != self.frequency) continue;

      Player* player = pm.GetPlayerById(player_data.player_id);

      if (!player || player->ship >= 8) continue;

      float distance_from_enemy = controlling ? (player_data.position_percent - state.attacking_penetration_percent)
                                              : (state.attacking_penetration_percent - player_data.position_percent);
      if (distance_from_enemy < 0) continue;

      // Choose this player if they are within the target penetration range and our current best is too far away
      bool new_best_closer_within_range =
          distance_from_enemy < kMaxDistanceFromEnemy && best_distance_from_enemy > kMaxDistanceFromEnemy;
      // Choose this player if they are the farthest away found so far within the penetration range.
      bool new_best_farther =
          distance_from_enemy > best_distance_from_enemy && distance_from_enemy < kMaxDistanceFromEnemy;

      if (player->ship == 4 && distance_from_enemy < kMaxDistanceFromEnemy) {
        best_anchor = player;
        break;
      }

      if (best_anchor == nullptr || new_best_closer_within_range || new_best_farther) {
        best_anchor = player;
        best_distance_from_enemy = distance_from_enemy;
      }
    }

    return best_anchor;
  }

  const char* base_position_key = nullptr;
  const char* output_key = nullptr;
  const char* anchor_output_key = nullptr;
};

struct CombatRoleEqualityNode : public BehaviorNode {
  CombatRoleEqualityNode(const char* role_key, CombatRole role) : role_key(role_key), role(role) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto opt_role_check = ctx.blackboard.Value<CombatRole>(role_key);
    if (!opt_role_check) return ExecuteResult::Failure;
    CombatRole role_check = *opt_role_check;

    if (role_check != role) return ExecuteResult::Failure;

    return ExecuteResult::Success;
  }

  CombatRole role = CombatRole::Anchor;
  const char* role_key = nullptr;
};

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

      // Skip non-flaggers that are too far away.
      if (player->flags == 0 &&
          player->position.DistanceSq(self->position) > nonflagger_distance_req * nonflagger_distance_req) {
        continue;
      }

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
    .Selector()
        .Child<DodgeIncomingDamage>(0.3f, 14.0f, 0.0f)
        .Sequence()
            .Child<FindBestEnemyCenterNode>("nearest_target", kNonflaggerDistanceMultiplier, kNonflaggerDistanceRequirement)
            .Child<PlayerPositionQueryNode>("nearest_target", "nearest_target_position")
            .Child<BulletDistanceNode>("bullet_distance")
            .Child<PlayerPositionQueryNode>("self_position")
            .Sequence(CompositeDecorator::Success) // If we have a portal but no location, lay one down.
                .Child<ShipItemCountThresholdNode>(ShipItemType::Portal, 1)
                .InvertChild<ShipPortalPositionQueryNode>()
                .Child<InputActionNode>(InputAction::Portal)
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
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

static std::unique_ptr<BehaviorNode> CreateFlagCollectTree(ExecuteContext& ctx) {
  BehaviorBuilder builder;

  // Begin by checking if there's a nearby flagger, chase and kill them if they are on radar.
  // Fall back to looking for nearest flag and go pick it up.

  // TODO: Stop cheating and actually have to look for flags.
  // TODO: Warp to center when it makes sense.

  // How many tiles we should try to avoid teammates. This keeps us from clumping up.
  constexpr float kTeamAvoidance = 6.0f;

  // clang-format off
  builder
    .Sequence()
        .SuccessChild<AvoidTeamNode>(kTeamAvoidance)
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

static std::unique_ptr<BehaviorNode> CreateAnchorTree(ExecuteContext& ctx) {
  BehaviorBuilder builder;

  // clang-format off
  builder
    .Sequence()
        .Child<FindNearestEnemyInBaseNode>("nearest_player")
        .Child<PlayerPositionQueryNode>("nearest_player", "nearest_target_position")
        .Child<SameBaseNode>("self_position", "nearest_target_position")
        .Child<CalculateAnchorPositionNode>("anchor_target_position")
        .Child<RectangleNode>("anchor_target_position", Vector2f(1, 1), "anchor_rect")
        .Child<RenderRectNode>("world_camera", "anchor_rect", Vector3f(1, 0, 0))
        .Selector(CompositeDecorator::Success) 
            .Sequence()
                .Child<ShipTraverseQueryNode>("anchor_target_position")
                .Child<ArriveNode>("anchor_target_position", 16.0f)
                .End()
            .Sequence()
                .Child<GoToNode>("anchor_target_position")
                .End()
            .End()
        .Selector(CompositeDecorator::Success) // TODO: Improve combat
            .Sequence()
                .Child<PlayerEnergyPercentThresholdNode>(0.8f)
                .Child<InputActionNode>(InputAction::Bomb)
                .End()
        .End();
  // clang-format on

  return builder.Build();
}

static std::unique_ptr<BehaviorNode> CreateRusherTree(ExecuteContext& ctx) {
  BehaviorBuilder builder;

  // clang-format off
  builder
    .Sequence()
        .Child<FindNearestEnemyInBaseNode>("nearest_player")
        .Child<PlayerPositionQueryNode>("nearest_player", "nearest_target_position")
        .Child<SameBaseNode>("self_position", "nearest_target_position")
        .Sequence(CompositeDecorator::Success) 
            //.InvertChild<VisibilityQueryNode>("nearest_target_position")
            .Child<GoToNode>("nearest_target_position")
            .End()
        .Selector(CompositeDecorator::Success) // TODO: Improve combat
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
        .End();
  // clang-format on

  return builder.Build();
}

static std::unique_ptr<BehaviorNode> CreateBaseAttackTree(ExecuteContext& ctx) {
  BehaviorBuilder builder;

  // How many tiles we should try to avoid teammtes. This keeps us from clumping up.
  constexpr float kTeamAvoidance = 8.0f;
  // Don't attach if the best attach target is closer than this many tiles. This is to prevent every bot from attaching
  // right at the base entrance when approaching.
  constexpr float kAttachDistanceRequirement = 45.0f;

  // clang-format off
  builder
    .Sequence()
        .Child<FindEnemyBaseEntranceNode>("best_base_entrance")
        .Child<CombatRoleDecideNode>("best_base_entrance", "combat_role", "anchor")
        .Child<PlayerPositionQueryNode>("self_position")
        .Sequence(CompositeDecorator::Success) // Avoid team if we aren't in flagroom so we don't get stuck
            .InvertChild<InFlagroomNode>("self_position")
            .Child<AvoidTeamNode>(kTeamAvoidance)
            .End()
        .Selector()
            .Sequence() // If we aren't in base, try attaching or pathing to entrance
                .InvertChild<SameBaseNode>("self_position", "best_base_entrance")
                .Selector()
                    .Sequence() // Try attaching to a teammate if we aren't in base.
                        .Child<TimerExpiredNode>("attach_cooldown")
                        .Child<SelectAttackingTeammateNode>("best_base_entrance", "anchor", "best_attach_teammate")
                        .Child<PlayerPositionQueryNode>("best_attach_teammate", "best_attach_teammate_position")
                        .Child<DistanceThresholdNode>("best_attach_teammate_position", kAttachDistanceRequirement)
                        .Child<AttachNode>("best_attach_teammate")
                        .Child<TimerSetNode>("attach_cooldown", kAttachCooldown)
                        .End()
                    // TODO: Defend ourselves while traveling.
                    .Child<GoToNode>("best_base_entrance")
                    .End()
                .End()
            .Selector() // We are in the target base, select tree based on combat role
                .Sequence()
                    .Child<CombatRoleEqualityNode>("combat_role", CombatRole::Anchor)
                    .Composite(CreateAnchorTree(ctx))
                    .End()
                .Sequence() // TODO: Defend anchor while near front of base and slowly move in.
                    .Child<CombatRoleEqualityNode>("combat_role", CombatRole::Rusher)
                    .Composite(CreateRusherTree(ctx))
                    .End()
                .End()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

static std::unique_ptr<BehaviorNode> CreateBaseDefendTree(ExecuteContext& ctx) {
  BehaviorBuilder builder;

  // How many tiles we should try to avoid teammtes. This keeps us from clumping up.
  constexpr float kTeamAvoidance = 8.0f;
  // Don't attach if the best attach target is closer than this many tiles.
  constexpr float kAttachDistanceRequirement = 45.0f;

  // clang-format off
  builder
    .Sequence()
        .Child<FindTeamBaseEntranceNode>("best_base_entrance")
        .Child<CombatRoleDecideNode>("best_base_entrance", "combat_role", "anchor")
        .Child<PlayerPositionQueryNode>("self_position")
        .Sequence(CompositeDecorator::Success) // Avoid team if we aren't in flagroom so we don't get stuck
            .InvertChild<InFlagroomNode>("self_position")
            .Child<AvoidTeamNode>(kTeamAvoidance)
            .End()
        .Selector()
            .Sequence() // If we aren't in base, try attaching or pathing to entrance
                .InvertChild<SameBaseNode>("self_position", "best_base_entrance")
                .Selector()
                    .Sequence() // Try attaching to a teammate if we aren't in base.
                        .Child<TimerExpiredNode>("attach_cooldown")
                        .Child<SelectDefendingTeammateNode>("anchor", "best_attach_teammate")
                        .Child<PlayerPositionQueryNode>("best_attach_teammate", "best_attach_teammate_position")
                        .Child<DistanceThresholdNode>("best_attach_teammate_position", kAttachDistanceRequirement)
                        .Child<AttachNode>("best_attach_teammate")
                        .Child<TimerSetNode>("attach_cooldown", kAttachCooldown)
                        .End()
                    // TODO: Defend ourselves while traveling.
                    .Child<GoToNode>("best_base_entrance")
                    .End()
                .End()
            .Selector()
                .Sequence()
                    .Child<CombatRoleEqualityNode>("combat_role", CombatRole::Anchor)
#if 0
                    .Sequence(CompositeDecorator::Success)
                        .Child<TimerExpiredNode>("chatspam")
                        .Child<TimerSetNode>("chatspam", 100)
                        .Child<ExecuteNode>([](ExecuteContext& ctx) -> ExecuteResult {
                              ctx.bot->game->chat.SendMessage(ChatType::Public, "Executing anchor tree.");
                              return ExecuteResult::Success;
                            })
                        .End()
#endif
                    .Composite(CreateAnchorTree(ctx))
                    .End()
                .Sequence()
                    .Child<CombatRoleEqualityNode>("combat_role", CombatRole::Rusher)
                    .Composite(CreateRusherTree(ctx))
                    .End()
                .End()
            .Sequence() // We are in a base, but no closest enemy. Path to start of base.
                .Child<ExecuteNode>([](ExecuteContext& ctx) {
                  auto self = ctx.bot->game->player_manager.GetSelf();
                  if (!self) return ExecuteResult::Failure;

                  auto opt_eg = ctx.blackboard.Value<ExtremeGames*>("eg");
                  if (!opt_eg) return ExecuteResult::Failure;

                  auto eg = *opt_eg;

                  size_t base_index = eg->GetBaseFromPosition(self->position);
                  if (base_index >= eg->bases.size()) return ExecuteResult::Failure;

                  const auto &path = eg->bases[base_index].path;
                  constexpr const float kIdlePathPercent = 0.1f;
                  constexpr const float kIdleAcceptableAmount = 0.05f;

                  size_t idle_index = (size_t)(path.points.size() * kIdlePathPercent);

                  ctx.blackboard.Set("base_entrance_idle_position", path.points[idle_index]);

                  float distance_from_idle_position = fabsf(eg->base_states[base_index].self_penetration_percent - kIdlePathPercent);
                  if (distance_from_idle_position < kIdleAcceptableAmount) {
                    // If we are very close to the idle position, then bail out so we do nothing.
                    return ExecuteResult::Failure;
                  }

                  return ExecuteResult::Success;
                })
                .Child<GoToNode>("base_entrance_idle_position")
                .End()
            .Sequence() // We are in the base and near the front. Do nothing while waiting. TODO: Line up best waiting position
                .End()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

static std::unique_ptr<BehaviorNode> CreateFlagDropTree(ExecuteContext& ctx) {
  BehaviorBuilder builder;

  constexpr float kNearAnchorDistance = 2.0f;

  // clang-format off
  builder
    .Sequence()
        .Child<FindTeamBaseEntranceNode>("best_base_entrance")
        .Child<CombatRoleDecideNode>("best_base_entrance", "combat_role", "anchor")
        .Selector()
            .Sequence() // Don't drop flags as anchor TODO: Implement something better
                .Child<CombatRoleEqualityNode>("combat_role", CombatRole::Anchor)
                .Composite(CreateBaseDefendTree(ctx), CompositeDecorator::Success)
                .End()
            .Selector() // If we aren't the anchor, neut flags near anchor. TODO: Move behind the anchor and drop in safe area
                .Sequence()
                    .InvertChild<CombatRoleEqualityNode>("combat_role", CombatRole::Anchor)
#if 0
                    .Child<ArenaFlagCountNode>("arena_flag_count")
                    .Child<TeamFlagCountNode>("anchor", "team_flag_count")
                    .Child<EqualityNode<size_t>>("arena_flag_count", "team_flag_count")
#endif
                    .Child<PlayerPositionQueryNode>("anchor", "anchor_position")
                    .Child<GoToNode>("anchor_position")
                    .Sequence(CompositeDecorator::Success)
                        .InvertChild<DistanceThresholdNode>("anchor_position", kNearAnchorDistance)
                        .Selector() // Switch to another ship if we are near the anchor.
                            .Sequence()
                                .Child<ShipQueryNode>(0)
                                .Child<ShipRequestNode>(1)
                                .End()
                            .Sequence()
                                .Child<ShipRequestNode>(0)
                                .End()
                            .End()
                        .End()
                    .End()
                .Sequence()
                    .Child<BaseFlagroomPositionNode>("best_base_entrance", "base_flagroom_position")
                    .Child<GoToNode>("base_flagroom_position")
                    .End()
                .End()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

std::unique_ptr<BehaviorNode> BaseBehavior::CreateTree(ExecuteContext& ctx) {
  BehaviorBuilder builder;

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
            .Child<UpdateBaseStateNode>()
            .SuccessChild<DodgeIncomingDamage>(0.3f, 14.0f, 0.0f)
            .Child<GameRoleDecideNode>("game_role")    
            .Selector()
                .Sequence()
                    .Child<GameRoleEqualityNode>("game_role", GameRole::CollectFlags)
                    .Composite(CreateFlagCollectTree(ctx))
                    .End()
                .Sequence()
                    .Child<GameRoleEqualityNode>("game_role", GameRole::AttackBase)
                    .Composite(CreateBaseAttackTree(ctx))
                    .End()
                .Sequence()
                    .Child<GameRoleEqualityNode>("game_role", GameRole::DefendBase)
                    .Composite(CreateBaseDefendTree(ctx))
                    .End()
                .Sequence()
                    .Child<GameRoleEqualityNode>("game_role", GameRole::DropFlags)
                    .Composite(CreateFlagDropTree(ctx))
                    .End()
                .End()
            .End()
        .Sequence() // Fall back to moving around center to try to find some action.
            .Child<WaypointNode>("base_center_waypoints", "waypoint_index", "waypoint_position", 15.0f)
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
        .End();
  // clang-format on

  return builder.Build();
}

}  // namespace eg
}  // namespace zero

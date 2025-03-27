#include "TerrierBehavior.h"

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
#include <zero/zones/svs/nodes/BurstAreaQueryNode.h>
#include <zero/zones/svs/nodes/DynamicPlayerBoundingBoxQueryNode.h>
#include <zero/zones/svs/nodes/FindNearestGreenNode.h>
#include <zero/zones/svs/nodes/IncomingDamageQueryNode.h>
#include <zero/zones/svs/nodes/MemoryTargetNode.h>
#include <zero/zones/svs/nodes/NearbyEnemyWeaponQueryNode.h>
#include <zero/zones/trenchwars/TrenchWars.h>
#include <zero/zones/trenchwars/nodes/FlagNode.h>

// TODO: The loops over players should probably use the kdtree. Check performance.

namespace zero {
namespace tw {

// This checks the top area and the vertical shaft for enemies.
// TODO: Might need to check other pub maps to see if it's fine.
// TODO: Could probably generate the rect sizes from the map data.
// Returns Success if no enemies found.
struct EmptyEntranceNode : public behavior::BehaviorNode {
  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto& pm = ctx.bot->game->player_manager;

    auto self = pm.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    auto opt_tw = ctx.blackboard.Value<TrenchWars*>("tw");
    if (!opt_tw) return behavior::ExecuteResult::Failure;
    TrenchWars* tw = *opt_tw;

    Vector2f top_center = tw->flag_position + Vector2f(0.5f, 4.5f);
    Rectangle top_rect(top_center - Vector2f(8.5f, 2.5f), top_center + Vector2f(8.5f, 3.5f));
    Rectangle vertical_rect(top_center - Vector2f(4, 3), top_center + Vector2f(4, 11));

    for (size_t i = 0; i < pm.player_count; ++i) {
      Player* player = pm.players + i;

      if (player->ship >= 8) continue;
      if (player->frequency == self->frequency) continue;
      if (player->enter_delay > 0.0f) continue;

      if (top_rect.Contains(player->position) || vertical_rect.Contains(player->position)) {
        return behavior::ExecuteResult::Failure;
      }
    }

    return behavior::ExecuteResult::Success;
  }
};

// Returns success if we are within nearby_threshold tiles of the 'entrance' position.
struct NearEntranceNode : public behavior::BehaviorNode {
  NearEntranceNode(float nearby_threshold) : nearby_threshold(nearby_threshold) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    if (!ctx.bot->bot_controller->pathfinder) return behavior::ExecuteResult::Failure;

    auto opt_tw = ctx.blackboard.Value<TrenchWars*>("tw");
    if (!opt_tw) return behavior::ExecuteResult::Failure;

    float radius = ctx.bot->game->connection.settings.ShipSettings[self->ship].GetRadius();
    TrenchWars* tw = *opt_tw;

    path::Path entrance_path = ctx.bot->bot_controller->pathfinder->FindPath(
        ctx.bot->game->GetMap(), self->position, tw->entrance_position, radius, self->frequency);

    if (entrance_path.GetRemainingDistance() < nearby_threshold) {
      return behavior::ExecuteResult::Success;
    }

    return behavior::ExecuteResult::Failure;
  }

  float nearby_threshold;
};

// Goes over the two paths into the base to find which one is less contested.
struct SelectBestEntranceSideNode : public behavior::BehaviorNode {
  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto& pm = ctx.bot->game->player_manager;
    auto self = pm.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    if (!ctx.bot->bot_controller->pathfinder) return behavior::ExecuteResult::Failure;

    auto opt_tw = ctx.blackboard.Value<TrenchWars*>("tw");
    if (!opt_tw) return behavior::ExecuteResult::Failure;

    TrenchWars* tw = *opt_tw;

    if (tw->left_entrance_path.Empty() || tw->right_entrance_path.Empty()) return behavior::ExecuteResult::Failure;

    size_t left_index = 0;
    size_t right_index = 0;

    float left_threat = GetPathThreat(ctx, *self, tw->left_entrance_path, &left_index);
    float right_threat = GetPathThreat(ctx, *self, tw->right_entrance_path, &right_index);

    // If we make it this far through a path, continue with it into the flagroom.
    constexpr float kPathTravelPercentBruteForce = 0.25f;

    if (left_index / (float)tw->left_entrance_path.points.size() >= kPathTravelPercentBruteForce) {
      ctx.bot->bot_controller->current_path = tw->left_entrance_path;
      ctx.bot->bot_controller->current_path.index = left_index;
      return behavior::ExecuteResult::Success;
    }

    if (right_index / (float)tw->right_entrance_path.points.size() >= kPathTravelPercentBruteForce) {
      ctx.bot->bot_controller->current_path = tw->right_entrance_path;
      ctx.bot->bot_controller->current_path.index = right_index;
      return behavior::ExecuteResult::Success;
    }

    if (left_threat > self->energy && right_threat > self->energy) {
      return behavior::ExecuteResult::Failure;
    }

    path::Path old_path = ctx.bot->bot_controller->current_path;

    float chosen_threat = left_threat;

    if (left_threat < right_threat) {
      ctx.bot->bot_controller->current_path = tw->left_entrance_path;
      ctx.bot->bot_controller->current_path.index = left_index;
      chosen_threat = left_threat;
    } else {
      ctx.bot->bot_controller->current_path = tw->right_entrance_path;
      ctx.bot->bot_controller->current_path.index = right_index;
      chosen_threat = right_threat;
    }

    // The amount of path points we should look forward to see how many teammates surround us.
    constexpr size_t kForwardPointCount = 3;
    constexpr float kNearbyDistanceSq = 3.0f * 3.0f;

    auto& path = ctx.bot->bot_controller->current_path;

    size_t nearest_point = FindNearestPathPoint(path, self->position);
    size_t forward_point = nearest_point + kForwardPointCount;

    if (forward_point > path.points.size() - 1) {
      forward_point = path.points.size() - 1;
    }

    size_t forward_teammate_count = 0;
    size_t total_teammate_count = 0;

    for (size_t i = 0; i < pm.player_count; ++i) {
      Player* p = pm.players + i;

      if (p->id == self->id) continue;
      if (p->ship >= 8) continue;
      if (p->frequency != self->frequency) continue;
      if (p->enter_delay > 0.0f) continue;

      ++total_teammate_count;
      if (p->position.DistanceSq(path.points[forward_point]) < kNearbyDistanceSq) {
        ++forward_teammate_count;
      }
    }

    if (total_teammate_count > 0 && forward_teammate_count == 0 && chosen_threat > 1.0f) {
      ctx.bot->bot_controller->current_path = old_path;
      return behavior::ExecuteResult::Failure;
    }

    return behavior::ExecuteResult::Success;
  }

  float GetPathThreat(behavior::ExecuteContext& ctx, Player& self, const path::Path& path, size_t* index) {
    size_t start_index = FindNearestPathPoint(path, self.position);
    size_t half_index = path.points.size() / 2;
    size_t end_index = start_index + 15;

    // Don't bother checking the threat inside of the flagroom, only the entranceway.
    if (end_index > half_index) end_index = half_index;

    *index = start_index;

    float total_threat = 0.0f;

    for (size_t i = start_index; i < end_index; ++i) {
      Vector2f point = path.points[i];

      float threat = ctx.bot->bot_controller->influence_map.GetValue(point);
      total_threat += threat;
    }

    return total_threat;
  }

  size_t FindNearestPathPoint(const path::Path& path, Vector2f position) {
    size_t index = 0;
    float last_dist_sq = 1024.0f * 1024.0f;

    // Loop through the nodes until we start getting farther away from the position, then use the last node.
    for (index = 0; index < path.points.size(); ++index) {
      float dist_sq = path.points[index].DistanceSq(position);

      if (index > 0 && dist_sq > last_dist_sq) {
        return index - 1;
      }

      last_dist_sq = dist_sq;
    }

    return 0;
  }
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
        .Sequence(CompositeDecorator::Success) // Always check incoming damage
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
        .Child<DodgeIncomingDamage>(0.1f, 35.0f)
        .End();
  // clang-format on

  return builder.Build();
}

static std::unique_ptr<behavior::BehaviorNode> CreateBurstSequence() {
  using namespace behavior;

  BehaviorBuilder builder;

  // clang-format off
  builder
    .Sequence()
        .Sequence(CompositeDecorator::Success) // Use burst when near a wall.
            .Child<ShipWeaponCapabilityQueryNode>(WeaponType::Burst)
            .InvertChild<ShipWeaponCooldownQueryNode>(WeaponType::Bomb)
            .Child<svs::BurstAreaQueryNode>()
            .Child<InputActionNode>(InputAction::Burst)
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
  constexpr float kNearbyEnemyThreshold = 10.0f;

  BehaviorBuilder builder;

  // clang-format off
  builder
    .Sequence() // Aim at target and shoot while seeking them.
        .Child<AimNode>(WeaponType::Bullet, nearest_target_key, "aimshot")
        .Parallel()
            .Child<FaceNode>("aimshot")
            .Child<SeekNode>("aimshot", "leash_distance", SeekNode::DistanceResolveType::Dynamic)
            .Sequence(CompositeDecorator::Success) // Determine if a shot should be fired by using weapon trajectory and bounding boxes.
                .InvertChild<DistanceThresholdNode>(nearest_target_position_key, kNearbyEnemyThreshold)
                .Composite(CreateBurstSequence())
                .Child<PlayerEnergyPercentThresholdNode>(0.9f)
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

// This is a tree to handle progressing through the entrance area.
// It will attempt to move forward when safe and fall back when in danger.
static std::unique_ptr<behavior::BehaviorNode> CreateEntranceBehavior() {
  using namespace behavior;

  BehaviorBuilder builder;

  constexpr float kNearEntranceDistance = 15.0f;

  // clang-format off
  builder
    .Sequence()
        .Child<NearEntranceNode>(kNearEntranceDistance)
        .Child<InfluenceMapPopulateWeapons>()
        .Child<InfluenceMapPopulateEnemies>(3.0f, 5000.0f, false)
        .SuccessChild<DodgeIncomingDamage>(0.1f, 35.0f)
        .Composite(CreateBurstSequence())
        .Selector()
            .Sequence()
                .Child<SelectBestEntranceSideNode>()
                .Child<FollowPathNode>()
                .Child<RenderPathNode>(Vector3f(0.0f, 0.0f, 0.75f))
                .End()
            .Selector()
                .Sequence()
                    .InvertChild<EmptyEntranceNode>()
                    .Child<ExecuteNode>([](ExecuteContext& ctx) { // Go to the area below the entrance
                      Vector2f entrance_position = ctx.blackboard.ValueOr<Vector2f>("tw_entrance_position", Vector2f(512, 281));

                      entrance_position.y += 8;

                      GoToNode node(entrance_position);

                      return node.Execute(ctx);
                    })
                    .Child<RenderPathNode>(Vector3f(1.0f, 0.0f, 0.0f))
                    .End()
                .Sequence()
                    .InvertChild<ShipTraverseQueryNode>("tw_entrance_position")
                    .Child<GoToNode>("tw_entrance_position")
                    .Child<RenderPathNode>(Vector3f(1.0f, 0.0f, 0.0f))
                    .End()
                .Child<ArriveNode>("tw_entrance_position", 1.25f)
                .End()
            .End()
        .End()
        .End();
  // clang-format on

  return builder.Build();
}

// This is a tree to handle staying safe below the entrance until it is cleared and safe to move up.
static std::unique_ptr<behavior::BehaviorNode> CreateBelowEntranceBehavior() {
  using namespace behavior;

  BehaviorBuilder builder;

  constexpr float kEntranceNearbyDistance = 15.0f;
  // How many teammates must be in the flagroom before we start moving up.
  constexpr u32 kFlagroomPresenceRequirement = 1;

  // clang-format off
  builder
    .Sequence()
        .Sequence() // A sequence that contains the conditionals for testing that we are below the entrance.
            .Child<PlayerPositionQueryNode>("self_position")
            .InvertChild<InFlagroomNode>("self_position") // We must not be in the flagroom
            .InvertChild<DistanceThresholdNode>("tw_entrance_position", kEntranceNearbyDistance) // We must be near the 'entrance' position.
            .Child<ExecuteNode>([](ExecuteContext& ctx) { // We must be below the entrance position
              auto self = ctx.bot->game->player_manager.GetSelf();
              if (!self || self->ship >= 8) return ExecuteResult::Failure;

              auto opt_entrance_pos = ctx.blackboard.Value<Vector2f>("tw_entrance_position");
              if (!opt_entrance_pos) return ExecuteResult::Failure;
              Vector2f entrance_pos = *opt_entrance_pos;

              return (self->position.y > entrance_pos.y) ? ExecuteResult::Success : ExecuteResult::Failure;
            })
            .End()
        .Selector()
            .Sequence() // Check if the above area is clear to move up
                .Selector() // Check if we have a team presence above us so we don't rush in alone.
                    .Sequence() // If we are alone on a frequency, consider it enough to move forward.
                        .Child<PlayerFrequencyCountQueryNode>("self_freq_size")
                        .InvertChild<ScalarThresholdNode<size_t>>("self_freq_size", 2)
                        .End()
                    .Child<FlagroomPresenceNode>(kFlagroomPresenceRequirement)
                    .End()
                .Child<EmptyEntranceNode>()
                .Child<GoToNode>("tw_entrance_position") // Move into entrance area so other tree takes over.
                .End()
            .Sequence(CompositeDecorator::Success) // Above area is not yet safe, sit still and dodge. TODO: Move into safe area.
                .Child<SeekNode>("self_position", 0.0f, SeekNode::DistanceResolveType::Zero)
                .Child<DodgeIncomingDamage>(0.1f, 15.0f)
                .End()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

static std::unique_ptr<behavior::BehaviorNode> CreateFlagroomTravelBehavior() {
  using namespace behavior;

  BehaviorBuilder builder;

  constexpr float kNearFlagroomDistance = 45.0f;

  // clang-format off
  builder
    .Sequence()
        .Child<PlayerSelfNode>("self")
        .Child<PlayerPositionQueryNode>("self_position")
        .Sequence(CompositeDecorator::Success) // Use afterburners to get to flagroom faster.
            .InvertChild<InFlagroomNode>("self_position")
            .Child<ExecuteNode>([](ExecuteContext& ctx) {
              auto self = ctx.bot->game->player_manager.GetSelf();
              if (!self || self->ship >= 8) return ExecuteResult::Failure;

              float max_energy = (float)ctx.bot->game->ship_controller.ship.energy;

              auto& input = *ctx.bot->bot_controller->input;
              auto& last_input = ctx.bot->bot_controller->last_input;

              // Keep using afterburners above 50%. Disable until full energy, then enable again.
              if (last_input.IsDown(InputAction::Afterburner)) {
                input.SetAction(InputAction::Afterburner, self->energy > max_energy * 0.5f);
              } else if (self->energy >= max_energy) {
                input.SetAction(InputAction::Afterburner, true);
              }

              return ExecuteResult::Success;
            })
            .End()
        .Selector()
            .Sequence() // Go directly to the flag room if we aren't there.
                .InvertChild<InFlagroomNode>("self_position")
                .Child<GoToNode>("tw_flag_position")
                //.Child<RenderPathNode>(Vector3f(0.0f, 1.0f, 0.5f))
                .End()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

std::unique_ptr<behavior::BehaviorNode> TerrierBehavior::CreateTree(behavior::ExecuteContext& ctx) {
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
        .Composite(CreateBelowEntranceBehavior())
        .Composite(CreateFlagroomTravelBehavior())
        .Selector() // Choose to fight the player or follow waypoints.
            .Composite(CreateEntranceBehavior())
            .Sequence()
                .Sequence() // Find an enemy
                    .Child<PlayerPositionQueryNode>("self_position")
                    .Child<svs::NearestMemoryTargetNode>("nearest_target")
                    .Child<PlayerPositionQueryNode>("nearest_target", "nearest_target_position")
                    .End()
                .Sequence(CompositeDecorator::Success) // If we have a portal but no location, lay one down.
                    .Child<InFlagroomNode>("self_position")
                    .Child<InFlagroomNode>("nearest_target_position")
                    .Child<ShipItemCountThresholdNode>(ShipItemType::Portal, 1)
                    .InvertChild<ShipPortalPositionQueryNode>()
                    .Child<InputActionNode>(InputAction::Portal)
                    .End()
                .Selector()
                    .Composite(CreateDefensiveTree())
                    .Sequence() // TODO: Find safest area in the flagroom and travel there.
                        .Child<InFlagroomNode>("nearest_target_position")
                        .Composite(CreateOffensiveTree("nearest_target", "nearest_target_position"))
                        .End()
                    .End()
                .End()
            .Sequence() // Follow set waypoints.
                .Child<WaypointNode>("waypoints", "waypoint_index", "waypoint_position", 15.0f)
                .Selector()
                    .Sequence()
                        .InvertChild<ShipTraverseQueryNode>("waypoint_position")
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

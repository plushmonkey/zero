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
#include <zero/zones/svs/nodes/BurstAreaQueryNode.h>
#include <zero/zones/svs/nodes/DynamicPlayerBoundingBoxQueryNode.h>
#include <zero/zones/svs/nodes/FindNearestGreenNode.h>
#include <zero/zones/svs/nodes/IncomingDamageQueryNode.h>
#include <zero/zones/svs/nodes/MemoryTargetNode.h>
#include <zero/zones/svs/nodes/NearbyEnemyWeaponQueryNode.h>

using namespace zero::behavior;

namespace zero {
namespace eg {

struct InBaseNode : public BehaviorNode {
  InBaseNode(const char* position_key) : position_key(position_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto opt_eg = ctx.blackboard.Value<ExtremeGames*>("eg");
    if (!opt_eg) return ExecuteResult::Failure;

    ExtremeGames* eg = *opt_eg;

    auto opt_position = ctx.blackboard.Value<Vector2f>(position_key);
    if (!opt_position) return ExecuteResult::Failure;

    Vector2f position = *opt_position;

    if (eg->GetBaseFromPosition(position) == -1) {
      return ExecuteResult::Failure;
    }

    return ExecuteResult::Success;
  }

  const char* position_key = nullptr;
};

// Returns a position in the flagroom of the base that contains the provided position
struct BaseFlagroomPositionNode : public BehaviorNode {
  BaseFlagroomPositionNode(const char* position_key, const char* output_key)
      : position_key(position_key), output_key(output_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto opt_eg = ctx.blackboard.Value<ExtremeGames*>("eg");
    if (!opt_eg) return ExecuteResult::Failure;

    ExtremeGames* eg = *opt_eg;

    auto opt_position = ctx.blackboard.Value<Vector2f>(position_key);
    if (!opt_position) return ExecuteResult::Failure;

    Vector2f position = *opt_position;

    size_t base_index = eg->GetBaseFromPosition(position);
    if (base_index == -1) return ExecuteResult::Failure;

    ctx.blackboard.Set(output_key, eg->bases[base_index].flagroom_position);

    return ExecuteResult::Success;
  }

  const char* position_key = nullptr;
  const char* output_key = nullptr;
};

// Go through each base and determine which one we should path to.
// This will prioritize bases with existing flags.
struct FindBestBaseEntranceNode : public BehaviorNode {
  FindBestBaseEntranceNode(const char* output_key) : output_key(output_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto& pm = ctx.bot->game->player_manager;
    auto self = pm.GetSelf();

    if (!self || self->ship >= 8) return ExecuteResult::Failure;

    auto opt_eg = ctx.blackboard.Value<ExtremeGames*>("eg");
    if (!opt_eg) return ExecuteResult::Failure;

    ExtremeGames* eg = *opt_eg;

    std::vector<size_t> flag_counts(eg->bases.size());

    auto& game = *ctx.bot->game;
    for (size_t i = 0; i < game.flag_count; ++i) {
      GameFlag* game_flag = game.flags + i;

      if (game_flag->flags & GameFlag_Dropped) {
        size_t base_index = eg->GetBaseFromPosition(game_flag->position);
        if (base_index != -1) {
          ++flag_counts[base_index];
        }
      }
    }

    for (size_t i = 0; i < game.player_manager.player_count; ++i) {
      auto player = game.player_manager.players + i;

      if (player->flags > 0) {
        size_t base_index = eg->GetBaseFromPosition(player->position);
        if (base_index != -1) {
          flag_counts[base_index] += player->flags;
        }
      }
    }

    size_t best_index = 0;
    size_t best_count = 0;

    for (size_t i = 0; i < flag_counts.size(); ++i) {
      if (flag_counts[i] > best_count) {
        best_index = i;
        best_count = flag_counts[i];
      }
    }

    ctx.blackboard.Set(output_key, eg->bases[best_index].entrance_position);

    return ExecuteResult::Success;
  }

  const char* output_key = nullptr;
};

struct FindBestBaseTeammateNode : public BehaviorNode {
  FindBestBaseTeammateNode(const char* output_key) : output_key(output_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto& pm = ctx.bot->game->player_manager;
    auto self = pm.GetSelf();

    if (!self || self->ship >= 8) return ExecuteResult::Failure;

    auto opt_eg = ctx.blackboard.Value<ExtremeGames*>("eg");
    if (!opt_eg) return ExecuteResult::Failure;

    ExtremeGames* eg = *opt_eg;

    for (size_t i = 0; i < pm.player_count; ++i) {
      auto player = pm.players + i;

      if (player->id == self->id) continue;
      if (player->ship >= 8) continue;
      if (player->frequency != self->frequency) continue;
      if (player->IsRespawning()) continue;
      if (!pm.IsSynchronized(*player)) continue;
      if (eg->GetBaseFromPosition(player->position) == -1) continue;

      ctx.blackboard.Set(output_key, player);

      return ExecuteResult::Success;
    }

    return ExecuteResult::Failure;
  }

  const char* output_key = nullptr;
};

struct FindNearestEnemyFlaggerNode : public BehaviorNode {
  FindNearestEnemyFlaggerNode(const char* output_key) : output_key(output_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto& game = ctx.bot->game;

    Player* self = game->player_manager.GetSelf();
    if (!self) return ExecuteResult::Failure;

    RegionRegistry& region_registry = *ctx.bot->bot_controller->region_registry;

    Player* best_target = nullptr;
    float closest_dist_sq = std::numeric_limits<float>::max();

    for (size_t i = 0; i < game->player_manager.player_count; ++i) {
      Player* player = game->player_manager.players + i;

      if (player->ship >= 8) continue;
      if (player->flags == 0) continue;
      if (player->frequency == self->frequency) continue;
      if (player->IsRespawning()) continue;
      if (player->position == Vector2f(0, 0)) continue;
      if (!game->player_manager.IsSynchronized(*player)) continue;
      if (!region_registry.IsConnected(self->position, player->position)) continue;

      bool in_safe = game->connection.map.GetTileId(player->position) == kTileIdSafe;
      if (in_safe) continue;

      float dist_sq = player->position.DistanceSq(self->position);
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
};

static std::unique_ptr<BehaviorNode> CreateFlagPickupTree(ExecuteContext& ctx) {
  BehaviorBuilder builder;

  // clang-format off
  builder
    .Sequence()
        .Child<NearestFlagNode>(NearestFlagNode::Type::Unclaimed, "nearest_flag")
        .Child<FlagPositionQueryNode>("nearest_flag", "nearest_flag_position")
        .Child<GoToNode>("nearest_flag_position")
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
                .Child<TimerSetNode>("attach_cooldown", 100)
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
                .Child<BaseFlagroomPositionNode>("self_position", "flagroom_position")
                .Child<GoToNode>("flagroom_position")
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

static std::unique_ptr<BehaviorNode> CreateAttackTree(ExecuteContext& ctx) {
  BehaviorBuilder builder;

  // clang-format off
  builder
    .Sequence()
        .Child<FindNearestEnemyFlaggerNode>("nearest_player")
        .Child<PlayerPositionQueryNode>("nearest_player", "nearest_target_position")
        .Sequence()
            .Sequence(CompositeDecorator::Success) 
                //.InvertChild<VisibilityQueryNode>("nearest_target_position")
                .Child<GoToNode>("nearest_target_position")
                .End()
            .Sequence(CompositeDecorator::Success)
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
        .Sequence() // Detach if we are attached.
            .Child<AttachedQueryNode>()
            .Child<TimerExpiredNode>("attach_cooldown")
            .Child<TimerSetNode>("attach_cooldown", 100)
            .Child<DetachNode>()
            .End()
        .Parallel()
            .Child<DodgeIncomingDamage>(0.3f, 14.0f, 0.0f)
            .Selector()
                .Composite(CreateFlagManagementTree(ctx))
                .Composite(CreateAttackTree(ctx))
                .End()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

}  // namespace eg
}  // namespace zero

#include "WarzoneBehavior.h"

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
#include <zero/behavior/nodes/FlagNode.h>

namespace zero {
namespace svs {

static behavior::ExecuteResult FindNearestEnemyFlagger(behavior::ExecuteContext& ctx) {
  const char* player_key = "nearest_player";

  auto& game = ctx.bot->game;
  
  Player* self = game->player_manager.GetSelf();
  if (!self) return behavior::ExecuteResult::Failure;

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
    ctx.blackboard.Erase(player_key);
    return behavior::ExecuteResult::Failure;
  }

  ctx.blackboard.Set(player_key, best_target);

  return behavior::ExecuteResult::Success;
}

std::unique_ptr<behavior::BehaviorNode> WarzoneBehavior::CreateTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  BehaviorBuilder builder;

  const Vector2f center(448, 545);

  // clang-format off
  builder
    .Selector()
        .Sequence() // Enter the specified ship if not already in it.
            .InvertChild<ShipQueryNode>("request_ship")
            .Child<ShipRequestNode>("request_ship")
            .End()
        .Sequence() // Find nearest flag and collect it.
            .Child<NearestFlagNode>(NearestFlagNode::Type::Unclaimed, "nearest_flag")
            .Child<FlagPositionQueryNode>("nearest_flag", "nearest_flag_position")
            .Child<GoToNode>("nearest_flag_position")
            .End()
        .Sequence()
            .Child<ExecuteNode>(FindNearestEnemyFlagger)
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
            .End()
        .Sequence() // Go to base if we have flags
            .Child<FlagCarryCountQueryNode>("self_flag_count")
            .Child<ScalarThresholdNode<u16>>("self_flag_count", 1)
            .Child<GoToNode>(Vector2f(476, 418))
        .End();
  // clang-format on

  return builder.Build();
}

}  // namespace svs
}  // namespace zero

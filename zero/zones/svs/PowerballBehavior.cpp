#include "PowerballBehavior.h"

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

namespace zero {
namespace svs {

struct PowerballTeamGoalQueryNode : public behavior::BehaviorNode {
  PowerballTeamGoalQueryNode(const char* team_key, const char* output_key)
      : team_key(team_key), output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto opt_team = ctx.blackboard.Value<u16>(team_key);
    if (!opt_team) return behavior::ExecuteResult::Failure;

    u16 team = *opt_team;

    auto opt_goal_position = FindGoal(ctx, team);
    if (!opt_goal_position) return behavior::ExecuteResult::Failure;

    Vector2f goal_position = *opt_goal_position;

    ctx.blackboard.Set<Vector2f>(output_key, goal_position);

    return behavior::ExecuteResult::Success;
  }

  const char* team_key = nullptr;
  const char* output_key = nullptr;

 private:
  static std::optional<Vector2f> FindGoal(behavior::ExecuteContext& ctx, u16 team) {
    const char* position_key = nullptr;

    // Use a lookup key to cache team goal position.
    if (team == 0) {
      position_key = "warbird_goal_position";
    } else if (team == 1) {
      position_key = "javelin_goal_position";
    } else {
      return {};
    }

    if (!ctx.blackboard.Has(position_key)) {
      auto& map = ctx.bot->game->GetMap();

      size_t goal_animation_index = (size_t)AnimatedTile::Goal;

      Vector2f average_position;
      size_t position_count = 0;

      for (size_t i = 0; i < map.animated_tiles[goal_animation_index].count; ++i) {
        Tile tile = map.animated_tiles[goal_animation_index].tiles[i];
        Vector2f position((float)tile.x, (float)tile.y);

        if (ctx.bot->game->soccer.IsTeamGoal(team, position)) {
          average_position += position + Vector2f(0.5f, 0.5f);
          ++position_count;
        }
      }

      if (position_count == 0) return {};

      average_position = average_position * (1.0f / position_count);

      // Cache the position for fast lookups.
      ctx.blackboard.Set<Vector2f>(position_key, average_position);
    }

    auto opt_goal_position = ctx.blackboard.Value<Vector2f>(position_key);
    if (!opt_goal_position) return {};

    return *opt_goal_position;
  }
};

std::unique_ptr<behavior::BehaviorNode> PowerballBehavior::CreateTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  BehaviorBuilder builder;

  Vector2f center(512, 512);

  // clang-format off
  builder
    .Selector()
        .Sequence() // Enter the game into one of the two frequencies if not already playing.
            .Child<ExecuteNode>([](ExecuteContext& ctx) {
              auto self = ctx.bot->game->player_manager.GetSelf();
              if (!self) return ExecuteResult::Failure;

              // If we aren't in one of the two allowed ships, continue with the sequence and request to join.
              if (self->ship != 0 && self->ship != 1) {
                return ExecuteResult::Success;
              }

              return ExecuteResult::Failure;
            })
            .Child<ShipRequestNode>(0)
            .End()
        .Sequence() // If we aren't carrying a ball, find it and go to it.
            .InvertChild<PowerballCarryQueryNode>()
            .Child<PowerballClosestQueryNode>("powerball_position", true)
            .Child<GoToNode>("powerball_position")
            .End()
        .Sequence()
            .Child<PowerballCarryQueryNode>()
            .Sequence() // Find team and enemy goals
                .Child<PlayerFrequencyQueryNode>("self_freq")
                .Child<PowerballTeamGoalQueryNode>("self_freq", "team_goal")
                .Child<ExecuteNode>([](ExecuteContext& ctx) {
                  u16 freq = ctx.blackboard.ValueOr<u16>("self_freq", 0);
                  freq ^= 1;
                  ctx.blackboard.Set<u16>("enemy_freq", freq);
                  return ExecuteResult::Success;
                })
                .Child<PowerballTeamGoalQueryNode>("enemy_freq", "enemy_goal")
            .End()
            .Sequence(CompositeDecorator::Success) // If we can shoot directly into a goal, do it.
                .Child<PowerballGoalPathQuery>("projected_ball_position", "goal_scored", false)
                .Child<BlackboardSetQueryNode>("goal_scored")
                .Child<PowerballFireNode>()
                .End()
            .Sequence(CompositeDecorator::Success)
                .Child<PowerballRemainingTimeQueryNode>("powerball_remaining_time")
                .InvertChild<ScalarThresholdNode<float>>("powerball_remaining_time", 0.25f)
                .Child<PowerballGoalPathQuery>("projected_ball_reverse_position", "goal_scored", true)
                .Child<DistanceNode>("projected_ball_position", "enemy_goal", "projected_ball_goal_distance", true)
                .Child<DistanceNode>("projected_ball_reverse_position", "enemy_goal", "projected_ball_reverse_goal_distance", true)
                .Sequence(CompositeDecorator::Success) // If the projected distance of a forward shot is closer than reverse, then shoot now to prevent back firing.
                    .Child<LessThanNode<float>>("projected_ball_goal_distance", "projected_ball_reverse_goal_distance")
                    .Child<PowerballFireNode>()
                    .End()
                .End()
            .Child<GoToNode>("enemy_goal")
            .End()
        .Sequence() // Go back to grab the ball that will be spawning soon.
            .Child<GoToNode>(center)
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

}  // namespace svs
}  // namespace zero

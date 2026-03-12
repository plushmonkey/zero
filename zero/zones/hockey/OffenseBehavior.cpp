#include "OffenseBehavior.h"

#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/behavior/nodes/AimNode.h>
#include <zero/behavior/nodes/InputActionNode.h>
#include <zero/behavior/nodes/MapNode.h>
#include <zero/behavior/nodes/MathNode.h>
#include <zero/behavior/nodes/MoveNode.h>
#include <zero/behavior/nodes/PlayerNode.h>
#include <zero/behavior/nodes/PowerballNode.h>
#include <zero/behavior/nodes/ShipNode.h>
#include <zero/zones/hockey/HockeyZone.h>
#include <zero/zones/hockey/nodes/PowerballNode.h>
#include <zero/zones/hockey/nodes/RinkNode.h>
#include <zero/zones/svs/nodes/DynamicPlayerBoundingBoxQueryNode.h>
#include <zero/zones/trenchwars/nodes/MoveNode.h>

namespace zero {
namespace hz {

// This is just a simple solution that isn't very good. TODO: Improve
struct FindPassWaitPosition : public behavior::BehaviorNode {
  FindPassWaitPosition(const char* carrier_player_key, const char* output_key)
      : carrier_player_key(carrier_player_key), output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    Player* self = ctx.bot->game->player_manager.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    auto opt_carrier = ctx.blackboard.Value<Player*>(carrier_player_key);
    if (!opt_carrier) return behavior::ExecuteResult::Failure;

    Player* carrier = *opt_carrier;
    if (!carrier || carrier->ship >= 8) return behavior::ExecuteResult::Failure;

    if (carrier->frequency != self->frequency) return behavior::ExecuteResult::Failure;

    auto& pm = ctx.bot->game->player_manager;

    Vector2f average;
    size_t count = 0;

    const Vector2f kWorldUp(0, -1);

    for (size_t i = 0; i < pm.player_count; ++i) {
      Player* player = pm.players + i;

      if (player->frequency != carrier->frequency) continue;

      Vector2f difference = Normalize(player->position - carrier->position).Dot(kWorldUp) * kWorldUp;
      average += difference;
      ++count;
    }

    if (count > 0) {
      average *= (1.0f / count);
    }

    average *= -1.0f;

    if (average.LengthSq() <= 0.00001f) {
      average = Vector2f(0, 1);
    }

    Vector2f position = carrier->position + average * 16.0f;

    ctx.blackboard.Set(output_key, position);

    return behavior::ExecuteResult::Success;
  }

  const char* carrier_player_key = nullptr;
  const char* output_key = nullptr;
};

static std::unique_ptr<behavior::BehaviorNode> CreateShootTree(const char* nearest_target_key) {
  using namespace behavior;

  BehaviorBuilder builder;

  constexpr float kShotAttemptDistance = 6.0f;

  // clang-format off
  builder
    .Sequence()
        .Sequence(CompositeDecorator::Success) // Determine if a shot should be fired by using weapon trajectory and bounding boxes.
            .Child<AimNode>(WeaponType::Bullet, nearest_target_key, "aimshot")
            .Child<PlayerPositionQueryNode>("self_position")
            .Child<PlayerPositionQueryNode>(nearest_target_key, "nearest_target_position")
            .InvertChild<DistanceThresholdNode>("nearest_target_position", kShotAttemptDistance)
            .Child<AimNode>(WeaponType::Bullet, nearest_target_key, "aimshot")
            .Child<svs::DynamicPlayerBoundingBoxQueryNode>(nearest_target_key, "target_bounds", 4.0f)
            .Child<MoveRectangleNode>("target_bounds", "aimshot", "target_bounds")
            .InvertChild<ShipWeaponCooldownQueryNode>(WeaponType::Bullet)
            .InvertChild<TileQueryNode>(kTileIdSafe)
            .Child<ShotVelocityQueryNode>(WeaponType::Bullet, "bullet_fire_velocity")
            .Child<RayNode>("self_position", "bullet_fire_velocity", "bullet_fire_ray")
            .Child<svs::DynamicPlayerBoundingBoxQueryNode>(nearest_target_key, "target_bounds", 4.0f)
            .Child<MoveRectangleNode>("target_bounds", "aimshot", "target_bounds")
            .Child<RayRectangleInterceptNode>("bullet_fire_ray", "target_bounds")
            .Child<InputActionNode>(InputAction::Bullet)
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

std::unique_ptr<behavior::BehaviorNode> OffenseBehavior::CreateTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  BehaviorBuilder builder;

  constexpr float kCreaseDistance = 16.0f;

  // clang-format off
  builder
    .Selector()
        .Sequence() // Enter the specified ship if not already in it.
            .InvertChild<ShipQueryNode>("request_ship")
            .Child<ShipRequestNode>("request_ship")
            .End()
        .Selector()
            .Sequence() // If we have ball, go to goal and shoot. TODO: Passing
                .Child<PowerballCarryQueryNode>()
                .Child<FindEnemyGoalRectNode>("enemy_goal_rect")
                .Child<RectangleCenterNode>("enemy_goal_rect", "enemy_goal_center")
                .Selector()
                    .Sequence() // If we are within crease, leave it.
                        .InvertChild<DistanceThresholdNode>("enemy_goal_center", kCreaseDistance)
                        .Child<RinkCenterNode>("rink_center")
                        .Child<GoToNode>("rink_center")
                        .End()
                    .Sequence()
                        .Child<AvoidEnemyNode>(8.0f)
                        .Child<tw::AfterburnerThresholdNode>(0.3f, 0.85f)
                        .Selector()
                            .Sequence()
                                .Child<ShipTraverseQueryNode>("enemy_goal_center")
                                .Child<SeekNode>("enemy_goal_center")
                                .End()
                            .Child<GoToNode>("enemy_goal_center")
                            .End()
                        .Sequence(CompositeDecorator::Success)
                            .InvertChild<DistanceThresholdNode>("enemy_goal_center", 40.0f)
                            .Child<DistanceThresholdNode>("enemy_goal_center", kCreaseDistance)
                            .Child<FaceNode>("enemy_goal_center")
                            .Child<PowerballGoalPathQuery>("projected_ball_position", "goal_scored", false)
                            .Child<EqualityNode<bool>>("goal_scored", true)
                            .Child<PowerballFireNode>()
                            .End()
                        .End()
                    .End()
                .End()
            .Sequence() // If ball is uncarried, grab it
                .Child<GetNearestBallNode>("target_ball", BallState::World)
                .Child<PowerballPositionQueryNode>("target_ball", "target_ball_position")
                .Child<tw::AfterburnerThresholdNode>(0.3f, 0.85f)
                .Child<AvoidEnemyNode>(8.0f)
                .Child<AvoidTeamNode>(12.0f)
                .Selector()
                    .Sequence()
                        .Child<ShipTraverseQueryNode>("target_ball_position")
                        .Child<SeekNode>("target_ball_position")
                        .End()
                    .Child<GoToNode>("target_ball_position")
                    .End()
                .End()
            .Sequence() // If ball is carried, kill if enemy or go forward for teammate
                .Child<GetNearestBallNode>("target_ball", BallState::Carried)
                .Child<PowerballPositionQueryNode>("target_ball", "target_ball_position")
                .Child<tw::AfterburnerThresholdNode>(0.3f, 0.85f)
                .Selector() // Determine what to do depending on team ownership
                    .Sequence() // If own team owns it, find a good place for pass and move there
                        .Child<PowerballTeamOwnedNode>("target_ball")
                        .Child<AvoidEnemyNode>(8.0f)
                        .Child<AvoidTeamNode>(12.0f)
                        .Child<PowerballCarrierQueryNode>("target_ball", "carrier_player")
                        .Child<FindPassWaitPosition>("carrier_player", "pass_wait_position")
                        .Child<GoToNode>("pass_wait_position")
                        .End()
                    .Sequence() // Enemy must own the ball, so kill them. TODO: Cover nearby enemies instead of all chasing
                        .Child<AvoidTeamNode>(12.0f)
                        .Selector()
                            .Sequence()
                                .Child<ShipTraverseQueryNode>("target_ball_position")
                                .Child<SeekNode>("target_ball_position")
                                .End()
                            .Child<GoToNode>("target_ball_position")
                            .End()
                        .Sequence(CompositeDecorator::Success)
                            .Child<PowerballCarrierQueryNode>("target_ball", "nearest_target")
                            .Composite(CreateShootTree("nearest_target"))
                            .End()
                        .End()
                    .End()
                .End()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

}  // namespace hz
}  // namespace zero

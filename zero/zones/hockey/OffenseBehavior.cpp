#include "OffenseBehavior.h"

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
#include <zero/behavior/nodes/ShipNode.h>
#include <zero/zones/hockey/HockeyZone.h>
#include <zero/zones/svs/nodes/DynamicPlayerBoundingBoxQueryNode.h>
#include <zero/zones/trenchwars/nodes/MoveNode.h>

namespace zero {
namespace hz {

static inline bool IsRinkClosed(Map& map, Rink& rink) {
  TileId tile_id = map.GetTileId(rink.east_door_x, (u16)rink.center.y);
  return tile_id >= kTileIdFirstDoor && tile_id <= kTileIdLastDoor;
}

struct PowerballCarrierQueryNode : public behavior::BehaviorNode {
  PowerballCarrierQueryNode(const char* ball_key, const char* output_key)
      : ball_key(ball_key), output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto opt_ball = ctx.blackboard.Value<Powerball*>(ball_key);
    if (!opt_ball) return behavior::ExecuteResult::Failure;

    Powerball* ball = *opt_ball;
    if (!ball) return behavior::ExecuteResult::Failure;

    if (ball->state != BallState::Carried) return behavior::ExecuteResult::Failure;

    Player* carrier = ctx.bot->game->player_manager.GetPlayerById(ball->carrier_id);

    if (!carrier || carrier->ship >= 8) return behavior::ExecuteResult::Failure;

    ctx.blackboard.Set(output_key, carrier);

    return behavior::ExecuteResult::Success;
  }

  const char* ball_key = nullptr;
  const char* output_key = nullptr;
};
struct PowerballPositionQueryNode : public behavior::BehaviorNode {
  PowerballPositionQueryNode(const char* ball_key, const char* output_key)
      : ball_key(ball_key), output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto opt_ball = ctx.blackboard.Value<Powerball*>(ball_key);
    if (!opt_ball) return behavior::ExecuteResult::Failure;

    Powerball* ball = *opt_ball;
    if (!ball) return behavior::ExecuteResult::Failure;

    Vector2f position = ctx.bot->game->soccer.GetBallPosition(*ball, GetMicrosecondTick());

    ctx.blackboard.Set(output_key, position);

    return behavior::ExecuteResult::Success;
  }

  const char* ball_key = nullptr;
  const char* output_key = nullptr;
};

struct PowerballTeamOwnedNode : public behavior::BehaviorNode {
  PowerballTeamOwnedNode(const char* ball_key) : ball_key(ball_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();

    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    auto opt_ball = ctx.blackboard.Value<Powerball*>(ball_key);
    if (!opt_ball) return behavior::ExecuteResult::Failure;

    Powerball* ball = *opt_ball;
    if (!ball) return behavior::ExecuteResult::Failure;

    if (ball->state == BallState::World && ball->frequency == self->frequency) {
      return behavior::ExecuteResult::Success;
    }

    if (ball->state == BallState::Carried) {
      Player* carrier = ctx.bot->game->player_manager.GetPlayerById(ball->carrier_id);
      if (carrier && carrier->ship < 8 && carrier->frequency == self->frequency) {
        return behavior::ExecuteResult::Success;
      }
    }

    return behavior::ExecuteResult::Failure;
  }

  const char* ball_key = nullptr;
};

struct RectangleCenterNode : public behavior::BehaviorNode {
  RectangleCenterNode(const char* rect_key, const char* output_key) : rect_key(rect_key), output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto opt_rect = ctx.blackboard.Value<Rectangle>(rect_key);
    if (!opt_rect) return behavior::ExecuteResult::Failure;

    Rectangle rect = *opt_rect;
    Vector2f center = (rect.min + rect.max) * 0.5f;

    ctx.blackboard.Set(output_key, center);

    return behavior::ExecuteResult::Success;
  }

  const char* rect_key = nullptr;
  const char* output_key = nullptr;
};

// Finds the closest uncarried ball that's within the current rink.
struct GetNearestBallNode : public behavior::BehaviorNode {
  GetNearestBallNode(const char* output_key, BallState required_state)
      : output_key(output_key), required_state(required_state) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    Player* self = ctx.bot->game->player_manager.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    auto opt_hz = ctx.blackboard.Value<HockeyZone*>("hz");
    if (!opt_hz) return behavior::ExecuteResult::Failure;

    HockeyZone* hz = *opt_hz;

    Rink* rink = hz->GetRinkFromTile(self->position);
    if (!rink) return behavior::ExecuteResult::Failure;

    Soccer& soccer = ctx.bot->game->soccer;

    for (size_t i = 0; i < ZERO_ARRAY_SIZE(soccer.balls); ++i) {
      Powerball* ball = soccer.balls + i;

      if (ball->id == kInvalidBallId) continue;

      Vector2f position = soccer.GetBallPosition(*ball, GetMicrosecondTick());
      RegionIndex ball_region_index =
          ctx.bot->bot_controller->region_registry->GetRegionIndex(MapCoord((u16)position.x, (u16)position.y));

      if (ball_region_index != rink->region_index) continue;
      if (ball->state == required_state) {
        ctx.blackboard.Set(output_key, ball);
        return behavior::ExecuteResult::Success;
      }
    }

    return behavior::ExecuteResult::Failure;
  }

  const char* output_key = nullptr;
  BallState required_state = BallState::Carried;
};

struct FindEnemyGoalRectNode : public behavior::BehaviorNode {
  FindEnemyGoalRectNode(const char* output_key) : output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    Player* self = ctx.bot->game->player_manager.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    auto opt_hz = ctx.blackboard.Value<HockeyZone*>("hz");
    if (!opt_hz) return behavior::ExecuteResult::Failure;

    HockeyZone* hz = *opt_hz;

    Rink* rink = hz->GetRinkFromTile(self->position);
    if (!rink) return behavior::ExecuteResult::Failure;

    Map& map = ctx.bot->game->GetMap();

    if (IsRinkClosed(map, *rink)) {
      // Determine which side of the rink we're on if the doors are closed because we can only use that goal.
      if (self->position.x < rink->center.x) {
        ctx.blackboard.Set(output_key, rink->west_goal_rect);
      } else {
        ctx.blackboard.Set(output_key, rink->east_goal_rect);
      }
    } else {
      if (self->frequency & 1) {
        ctx.blackboard.Set(output_key, rink->west_goal_rect);
      } else {
        ctx.blackboard.Set(output_key, rink->east_goal_rect);
      }
    }

    return behavior::ExecuteResult::Success;
  }

  const char* output_key = nullptr;
};

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

static std::unique_ptr<behavior::BehaviorNode> CreateOffensiveTree(const char* nearest_target_key,
                                                                   const char* nearest_target_position_key) {
  using namespace behavior;

  BehaviorBuilder builder;

  // clang-format off
  builder
    .Sequence() // Aim at target and shoot while seeking them.
        .Child<AimNode>(WeaponType::Bullet, nearest_target_key, "aimshot")
        .Parallel()
            .Child<FaceNode>("aimshot")
            .Child<SeekNode>("aimshot", 0.0f, SeekNode::DistanceResolveType::Static)
            .Composite(CreateShootTree(nearest_target_key))
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

std::unique_ptr<behavior::BehaviorNode> OffenseBehavior::CreateTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  BehaviorBuilder builder;

  const Vector2f center(512, 512);

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
                .Child<AvoidEnemyNode>(8.0f)
                .Child<RectangleCenterNode>("enemy_goal_rect", "enemy_goal_center")
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
                    .Child<DistanceThresholdNode>("enemy_goal_center", 16.0f) // Crease
                    .Child<FaceNode>("enemy_goal_center")
                    .Child<PowerballGoalPathQuery>("projected_ball_position", "goal_scored", false)
                    .Child<EqualityNode<bool>>("goal_scored", true)
                    .Child<PowerballFireNode>()
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
                            .Composite(CreateOffensiveTree("nearest_target", "target_ball_position"))
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

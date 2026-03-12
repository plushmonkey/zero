#include "GoalieBehavior.h"

#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/behavior/nodes/AimNode.h>
#include <zero/behavior/nodes/InputActionNode.h>
#include <zero/behavior/nodes/MapNode.h>
#include <zero/behavior/nodes/MathNode.h>
#include <zero/behavior/nodes/MoveNode.h>
#include <zero/behavior/nodes/PlayerNode.h>
#include <zero/behavior/nodes/PowerballNode.h>
#include <zero/behavior/nodes/RenderNode.h>
#include <zero/behavior/nodes/ShipNode.h>
#include <zero/zones/hockey/HockeyZone.h>
#include <zero/zones/hockey/nodes/PowerballNode.h>
#include <zero/zones/hockey/nodes/RinkNode.h>
#include <zero/zones/trenchwars/nodes/MoveNode.h>

namespace zero {
namespace hz {

// Handle goalie movement with a custom actuator so it will reverse into position instead of always turning around.
struct GoalieActuateNode : public behavior::BehaviorNode {
  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    InputState& input = ctx.bot->input;
    float required_forward_vector_to_thrust = 0.65f;
    float rotation_threshold = 0.75f;

    Game& game = *ctx.bot->game;
    Player* self = game.player_manager.GetSelf();

    if (!self || self->ship >= 8) return behavior::ExecuteResult::Success;

    Vector2f force = ctx.bot->bot_controller->steering.force;
    float rotation = ctx.bot->bot_controller->steering.rotation;

    ctx.bot->bot_controller->steering.force = Vector2f(0, 0);
    ctx.bot->bot_controller->steering.rotation = 0.0f;

    Vector2f heading = self->GetHeading();

    Vector2f steering_direction = heading;

    bool has_force = force.LengthSq() > 0.0f;
    bool has_rotation = rotation != 0.0f;

    // If we have some steering force, set that as the target orientation.
    if (has_force) {
      steering_direction = Normalize(force);
    }

    Vector2f rotate_target = steering_direction;

    // Rotate from the heading by the rotation target amount.
    if (has_rotation) {
      rotate_target = Rotate(self->GetHeading(), -rotation);
    }

    // If there was no force, then hard set the target orientation as the rotation target.
    if (!has_force) {
      steering_direction = rotate_target;
    }

    Vector2f perp = Perpendicular(heading);
    bool behind = steering_direction.Dot(heading) < 0;
    bool leftside = steering_direction.Dot(perp) < 0;

    // If our target orientation is far from the rotate target, keep it close by rotating back toward it.
    // This is used as a way to rectify having a force and rotation target. Having both means that there must be some
    // blend of the two. It keeps the heading locked around the rotate target with some wiggle room for also aiming at
    // the steering direction.
    if (steering_direction.Dot(rotate_target) < rotation_threshold) {
      float rotation = 0.1f;
      int sign = leftside ? 1 : -1;

      // If the steering orientation target is behind us, then flip the rotation direction so it can reverse into it.
      if (behind) sign *= -1;

      // Hard set the steering target orientation to being within the rotate target orientation wiggle room.
      steering_direction = Rotate(rotate_target, rotation * sign);

      // Check again which direction we need to rotate to reach the steering orientation target.
      leftside = steering_direction.Dot(perp) < 0;
    }

    // If our target is behind us, calculate how long it would take to rotate toward it.
    // If it takes too long, just go backwards there, otherwise rotate and go forward.
    if (behind && !has_rotation) {
      float forward_seconds = CalculateRotatedTravelTime(game, *self, heading, steering_direction, force);
      float reverse_seconds = CalculateRotatedTravelTime(game, *self, -heading, steering_direction, force);

      if (reverse_seconds < forward_seconds) {
        heading = -heading;

        bool clockwise = leftside;
        if (heading.Dot(steering_direction) < 0.996f) {
          input.SetAction(InputAction::Right, clockwise);
          input.SetAction(InputAction::Left, !clockwise);
        }

        if (heading.Dot(steering_direction) >= required_forward_vector_to_thrust) {
          input.SetAction(InputAction::Backward, true);
        }

        return behavior::ExecuteResult::Success;
      }
    }

    bool clockwise = !leftside;

    if (has_force) {
      bool move_reverse = has_rotation || -heading.Dot(steering_direction) >= required_forward_vector_to_thrust;
      bool move_forward = has_rotation || heading.Dot(steering_direction) >= required_forward_vector_to_thrust;

      if (behind && move_reverse) {
        input.SetAction(InputAction::Backward, true);
      } else if (!behind && move_forward) {
        input.SetAction(InputAction::Forward, true);
      }
    }

    if (heading.Dot(steering_direction) < 0.996f) {
      input.SetAction(InputAction::Right, clockwise);
      input.SetAction(InputAction::Left, !clockwise);
    }

    return behavior::ExecuteResult::Success;
  }

  static float CalculateRotatedTravelTime(Game& game, const Player& self, const Vector2f& heading,
                                          const Vector2f& steering_direction, const Vector2f& force) {
    float seconds_per_rotation = 1.0f;

    if (game.ship_controller.ship.rotation > 0) {
      seconds_per_rotation = 400.0f / game.ship_controller.ship.rotation;
    }

    // Calculate the percentage of an entire ship rotation that is necessary to reach the requested steering direction.
    float rotate_percent = (1.0f - heading.Dot(steering_direction)) * 0.25f;

    // How long it takes to reach a rotation that would directly point at the steering direction.
    float seconds_to_rotate = rotate_percent * seconds_per_rotation;

    float distance = force.Length();
    float speed = self.velocity.Dot(steering_direction) * self.velocity.Length();
    float maxspeed = game.connection.settings.ShipSettings[self.ship].MaximumSpeed / 10.0f / 16.0f;

    float thrust = game.ship_controller.ship.thrust * (10.0f / 16.0f);

    // Assume we apply no thrust while rotating, so we adjust remaining distance by inertia speed.
    distance -= speed * seconds_to_rotate;

    float travel_seconds = seconds_to_rotate;

    while (distance > 0.0f) {
      speed = speed + thrust;
      if (speed > maxspeed) speed = maxspeed;

      if (speed > distance) {
        travel_seconds += distance / speed;
      } else {
        travel_seconds += 1.0f;
      }

      distance -= speed;
    }

    return travel_seconds;
  }
};

// Output is a Player* to pass to.
struct FindPassTarget : public behavior::BehaviorNode {
  FindPassTarget(const char* output_key) : output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    Player* self = ctx.bot->game->player_manager.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    auto& pm = ctx.bot->game->player_manager;
    Map& map = ctx.bot->game->GetMap();

    RegionIndex self_region = ctx.bot->bot_controller->region_registry->GetRegionIndex(self->position);

    std::vector<Rectangle> enemy_colliders;

    constexpr float kMovementSlop = 2.0f;

    for (size_t i = 0; i < pm.player_count; ++i) {
      Player* player = pm.players + i;

      if (player->ship >= 8) continue;
      if (player->frequency == self->frequency) continue;

      RegionIndex region_index = ctx.bot->bot_controller->region_registry->GetRegionIndex(player->position);

      if (region_index != self_region) continue;

      float pickup_radius =
          ctx.bot->game->connection.settings.ShipSettings[player->ship].SoccerBallProximity / 16.0f + kMovementSlop;

      Vector2f collider_min = player->position - Vector2f(pickup_radius, pickup_radius);
      Vector2f collider_max = player->position + Vector2f(pickup_radius, pickup_radius);
      Rectangle collider(collider_min, collider_max);

      enemy_colliders.push_back(collider);
    }

    for (size_t i = 0; i < pm.player_count; ++i) {
      Player* player = pm.players + i;

      if (player->frequency != self->frequency) continue;
      if (player->ship >= 8) continue;
      if (player->id == self->id) continue;

      Vector2f direction = Normalize(player->position - self->position);
      float distance = player->position.Distance(self->position);
      Ray ray(self->position, direction);

      bool safe_pass = true;

      for (Rectangle& collider : enemy_colliders) {
        float collide_dist = 0.0f;

        if (RayBoxIntersect(ray, collider, &collide_dist, nullptr)) {
          if (collide_dist < distance) {
            safe_pass = false;
            break;
          }
        }
      }

      if (safe_pass) {
        ctx.blackboard.Set(output_key, player);
        return behavior::ExecuteResult::Success;
      }
    }

    return behavior::ExecuteResult::Failure;
  }

  const char* output_key = nullptr;
};

struct FindDefensePositionNode : public behavior::BehaviorNode {
  FindDefensePositionNode(const char* goal_rect_key, const char* ball_key, const char* output_key)
      : goal_rect_key(goal_rect_key), ball_key(ball_key), output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    Player* self = ctx.bot->game->player_manager.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    auto opt_goal_rect = ctx.blackboard.Value<Rectangle>(goal_rect_key);
    if (!opt_goal_rect) return behavior::ExecuteResult::Failure;

    auto opt_ball = ctx.blackboard.Value<Powerball*>(ball_key);
    if (!opt_ball) return behavior::ExecuteResult::Failure;

    auto opt_hz = ctx.blackboard.Value<HockeyZone*>("hz");
    if (!opt_hz) return behavior::ExecuteResult::Failure;

    HockeyZone* hz = *opt_hz;

    Rectangle goal_rect = *opt_goal_rect;
    Powerball* ball = *opt_ball;

    if (!ball || ball->id == kInvalidBallId) return behavior::ExecuteResult::Failure;

    Vector2f goal_center = goal_rect.GetCenter();

    Vector2f position = ctx.bot->game->soccer.GetBallPosition(*ball, GetMicrosecondTick());
    Vector2f direction;

    Rink* rink = hz->GetRinkFromTile(position);
    if (!rink) return behavior::ExecuteResult::Failure;

    if (ball->state == BallState::World) {
      direction.x = ball->vel_x / 160.0f;
      direction.y = ball->vel_y / 160.0f;

      if (ball->vel_x == 0 && ball->vel_y == 0) {
        direction = goal_center - position;
      }
    } else {
      Player* carrier = ctx.bot->game->player_manager.GetPlayerById(ball->carrier_id);

      if (carrier && carrier->ship < 8 && ctx.bot->game->player_manager.IsSynchronized(*carrier)) {
        float speed = ctx.bot->game->connection.settings.ShipSettings[carrier->ship].SoccerBallSpeed / 10.0f / 16.0f;
        u8 discrete_rotation = (u8)(carrier->orientation * 40.0f);

        constexpr u8 kMaxRotation = 5;

        s8 min_rotation = (s8)(discrete_rotation - kMaxRotation);
        s8 max_rotation = (s8)(discrete_rotation + kMaxRotation);

        size_t count = 0;

        for (s8 rotation = min_rotation; rotation < max_rotation; ++rotation) {
          u8 check_rotation = (rotation + 40) % 40;

          Vector2f heading = OrientationToHeading(check_rotation);
          Vector2f ray_direction = Normalize(carrier->velocity + heading * speed);
          Ray ray(position, ray_direction);

          float dist = 0.0f;
          if (RayBoxIntersect(ray, goal_rect, &dist, nullptr)) {
            direction += ray_direction;
          }
        }
        if (count > 0) {
          direction *= 1.0f / count;
        }
      }
    }

    if (direction.LengthSq() <= 0.0f) {
      direction = goal_center - position;
    }

    constexpr float kCreaseRadius = 12.0f;

    Vector2f goal_normal = Normalize(rink->center - goal_center);
    Ray ray(position, Normalize(direction));
    float dist = 0.0f;
    Vector2f target = goal_center + goal_normal * kCreaseRadius;

    if (RayBoxIntersect(ray, goal_rect, &dist, nullptr)) {
      Vector2f collision = ray.origin + ray.direction * dist;

      target = collision;
      
      float traveled = fabsf(collision.x - goal_center.x);
      float remaining = kCreaseRadius - traveled;

      if (remaining > 0.0f) {
        target = collision + Normalize(position - collision) * remaining;
      }
    } else {
      target = goal_center + Normalize(position - goal_center) * kCreaseRadius;
    }

    Vector2f goal_back = Vector2f(goal_center.x - (goal_rect.max.x - goal_rect.min.x) / 2.0f, goal_center.y);

    // If the target position is behind the goal, defend from either above or below while staying inside crease.
    if ((target - goal_back).Dot(goal_normal) < 0.0) {
      Vector2f new_direction;

      if (target.y < goal_back.y) {
        new_direction = Vector2f(0.3f * goal_normal.x, -0.8f);
      } else {
        new_direction = Vector2f(0.3f * goal_normal.x, 0.8f);
      }

      target = goal_center + Normalize(new_direction) * kCreaseRadius;
    }

    ctx.blackboard.Set(output_key, target);

    return behavior::ExecuteResult::Success;
  }

  const char* goal_rect_key = nullptr;
  const char* ball_key = nullptr;
  const char* output_key = nullptr;
};

std::unique_ptr<behavior::BehaviorNode> GoalieBehavior::CreateTree(behavior::ExecuteContext& ctx) {
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
            .Sequence() // If we have the ball, find someone to pass to
                .Child<PowerballCarryQueryNode>()
                .Selector()
                    .Sequence()
                        .Child<FindPassTarget>("pass_target")
                        .Child<PlayerPositionQueryNode>("pass_target", "pass_target_position")
                        .Child<FaceNode>("pass_target_position")
                        .Child<GoToNode>("pass_target_position")
                        .Sequence(CompositeDecorator::Success)
                            .Child<PlayerPositionQueryNode>("self_position")
                            .Child<PlayerBoundingBoxQueryNode>("pass_target", "pass_target_bounds", 1.5f)
                            .Child<ShotVelocityQueryNode>(WeaponType::Bullet, "bullet_fire_velocity")
                            .Child<RayNode>("self_position", "bullet_fire_velocity", "bullet_fire_ray")
                            .Child<RayRectangleInterceptNode>("bullet_fire_ray", "pass_target_bounds")
                            .Child<PowerballFireNode>()
                            .End()
                        .End()
                    .Sequence() // No pass target, just fire toward rink center
                        .Child<RinkCenterNode>("rink_center")
                        .Child<FaceNode>("rink_center")
                        .Child<GoToNode>("rink_center")
                        .Sequence(CompositeDecorator::Success)
                            .Child<PlayerPositionQueryNode>("self_position")
                            .Child<RectangleNode>("rink_center", Vector2f(3.5f, 3.5f), "center_collider")
                            .Child<ShotVelocityQueryNode>(WeaponType::Bullet, "bullet_fire_velocity") 
                            .Child<RayNode>("self_position", "bullet_fire_velocity", "ball_fire_ray")
                            .Child<RayRectangleInterceptNode>("ball_fire_ray", "center_collider")
                            .Child<PowerballFireNode>()
                            .End()
                        .End()
                    .End()
                .End()
            .Sequence() // We don't have the ball, so find a good position near goal to defend
                .Child<FindTeamGoalRectNode>("team_goal_rect")
                .Selector()
                    .Sequence()
                        .Child<GetNearestBallNode>("target_ball", BallState::Carried)
                        .Child<PowerballPositionQueryNode>("target_ball", "target_ball_position")
                        .End()
                    .Sequence()
                        .Child<GetNearestBallNode>("target_ball", BallState::World)
                        .Child<PowerballPositionQueryNode>("target_ball", "target_ball_position")
                        .End()
                    .End()
                .Child<FindDefensePositionNode>("team_goal_rect", "target_ball", "defend_position")
                .Child<RectangleNode>("defend_position", Vector2f(1.0f, 1.0f), "defend_rect")
                .Child<RenderRectNode>("world_camera", "defend_rect", Vector3f(1.0f, 0.0f, 0.0f))
                .Selector()
                    .Sequence()
                        .Child<ShipTraverseQueryNode>("defend_position")
                        .Child<ArriveNode>("defend_position", 3.0f)
                        .Child<tw::AfterburnerThresholdNode>(0.25f, 0.75f)
                        .Child<GoalieActuateNode>()
                        .End()
                    .Child<GoToNode>("defend_position")
                    .End()
                .End()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

}  // namespace hz
}  // namespace zero

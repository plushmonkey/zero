#include "FrogRaceBehavior.h"

#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/behavior/nodes/AimNode.h>
#include <zero/behavior/nodes/AttachNode.h>
#include <zero/behavior/nodes/BlackboardNode.h>
#include <zero/behavior/nodes/InputActionNode.h>
#include <zero/behavior/nodes/MapNode.h>
#include <zero/behavior/nodes/MathNode.h>
#include <zero/behavior/nodes/MoveNode.h>
#include <zero/behavior/nodes/PlayerNode.h>
#include <zero/behavior/nodes/RegionNode.h>
#include <zero/behavior/nodes/RenderNode.h>
#include <zero/behavior/nodes/ShipNode.h>
#include <zero/behavior/nodes/TargetNode.h>
#include <zero/behavior/nodes/TimerNode.h>
#include <zero/zones/devastation/nodes/BounceShotQueryNode.h>
#include <zero/zones/devastation/nodes/CombatRoleNode.h>
#include <zero/zones/devastation/nodes/GetAttachTargetNode.h>
#include <zero/zones/devastation/nodes/GetSpawnNode.h>
#include <zero/zones/svs/nodes/BurstAreaQueryNode.h>
#include <zero/zones/svs/nodes/IncomingDamageQueryNode.h>

namespace zero {
namespace deva {

enum class TrackQueryType {
  Warper,
  Target,
};

struct GetTrackPosition : public behavior::BehaviorNode {
  GetTrackPosition(TrackQueryType query_type, const char* output_key)
      : query_type(query_type), output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto opt_track = ctx.blackboard.Value<size_t>(kSelectedTrackKey);
    if (!opt_track) return behavior::ExecuteResult::Failure;

    size_t track_index = *opt_track;

    const char* position_key = query_type == TrackQueryType::Warper ? kRaceWarpersKey : kRaceTargetsKey;

    auto opt_positions = ctx.blackboard.Value<std::vector<Vector2f>>(position_key);
    if (!opt_positions) return behavior::ExecuteResult::Failure;

    std::vector<Vector2f>& positions = *opt_positions;

    ctx.blackboard.Set(output_key, positions[track_index]);

    return behavior::ExecuteResult::Success;
  }

  TrackQueryType query_type = TrackQueryType::Warper;
  const char* output_key = nullptr;
};

struct InTrackRegion : public behavior::BehaviorNode {
  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto opt_track = ctx.blackboard.Value<size_t>(kSelectedTrackKey);
    if (!opt_track) return behavior::ExecuteResult::Failure;

    size_t track_index = *opt_track;

    const char* position_key = kRaceTargetsKey;

    auto opt_positions = ctx.blackboard.Value<std::vector<Vector2f>>(position_key);
    if (!opt_positions) return behavior::ExecuteResult::Failure;

    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self) return behavior::ExecuteResult::Failure;

    std::vector<Vector2f>& positions = *opt_positions;
    Vector2f position = positions[track_index];

    bool in_region = ctx.bot->bot_controller->region_registry->IsConnected(self->position, position);

    return in_region ? behavior::ExecuteResult::Success : behavior::ExecuteResult::Failure;
  }
};

std::unique_ptr<behavior::BehaviorNode> FrogRaceBehavior::CreateTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  srand((unsigned int)time(nullptr));

  BehaviorBuilder builder;
  Rectangle center_rect = Rectangle::FromPositionRadius(Vector2f(512, 512), 64.0f);

  // clang-format off
  builder
    .Selector()
        .Sequence() // Enter the specified ship if not already in it.
            .InvertChild<ShipQueryNode>("request_ship")
            .Child<ShipRequestNode>("request_ship")
            .End()
        .Sequence() // If we are attached to someone, detach.
            .Child<AttachedQueryNode>()
            .Child<DetachNode>()
            .End()
        .Sequence() // If we are in center safe, move toward the warper for the selected base.
            .Child<PlayerPositionQueryNode>("self_position")
            .Child<RectangleContainsNode>(center_rect, "self_position")
            .Child<GetTrackPosition>(TrackQueryType::Warper, "track_warper")
            .Child<GoToNode>("track_warper")
            .End()
        .Sequence() // If we aren't running the center tree, then we should be in the track's region. Warp if not.
            .InvertChild<InTrackRegion>()
            .Child<WarpNode>()
            .End()
        .Sequence()
            .Child<GetTrackPosition>(TrackQueryType::Target, "track_target")
            .Child<PlayerPositionQueryNode>("self_position")
            .Selector()
                .Sequence()
                    .InvertChild<DistanceThresholdNode>("self_position", "track_target", 1.5f)
                    .Child<PlayerStatusQueryNode>(Status_Safety)
                    .Child<InputActionNode>(InputAction::Bullet) // Stop in safe
                    .Sequence(CompositeDecorator::Success) // Add a warp timer if we don't have one
                        .InvertChild<BlackboardSetQueryNode>("race_end_warp_timer")
                        .Child<TimerSetNode>("race_end_warp_timer", 300)
                        .End()
                    .Sequence(CompositeDecorator::Success) // Warp out and clear timer
                        .Child<TimerExpiredNode>("race_end_warp_timer")
                        .Child<WarpNode>()
                        .End()
                    .End()
                .Sequence()
                    .Child<BlackboardEraseNode>("race_end_warp_timer")
                    .Child<GoToNode>("track_target")
                    .End()
                .End()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

}  // namespace deva
}  // namespace zero

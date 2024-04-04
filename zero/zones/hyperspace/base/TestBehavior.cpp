#include "TestBehavior.h"

#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/behavior/nodes/BlackboardNode.h>
#include <zero/behavior/nodes/ChatNode.h>
#include <zero/behavior/nodes/FlagNode.h>
#include <zero/behavior/nodes/MapNode.h>
#include <zero/behavior/nodes/MathNode.h>
#include <zero/behavior/nodes/PlayerNode.h>
#include <zero/behavior/nodes/RenderNode.h>
#include <zero/behavior/nodes/ShipNode.h>
#include <zero/behavior/nodes/TimerNode.h>
#include <zero/zones/hyperspace/nodes/SectorNode.h>
#include <zero/zones/hyperspace/nodes/GlobalGoToNode.h>

namespace zero {
namespace hyperspace {

struct SetRandomPositionNode : public behavior::BehaviorNode {
  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    static const Vector2f kPositions[] = {
        Vector2f(818, 220), Vector2f(840, 530), Vector2f(740, 825), Vector2f(480, 835), Vector2f(290, 815),
        Vector2f(155, 585), Vector2f(210, 205), Vector2f(370, 229), Vector2f(505, 615), Vector2f(50, 50),
    };

    int index = rand() % ZERO_ARRAY_SIZE(kPositions);

    Vector2f next = kPositions[index];

    Log(LogLevel::Info, "Setting random_position");

    ctx.blackboard.Set("random_position", next);

    return behavior::ExecuteResult::Success;
  }
};

std::unique_ptr<behavior::BehaviorNode> TestBehavior::CreateTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  srand((unsigned int)time(nullptr));

  BehaviorBuilder builder;

  // clang-format off
  builder
    .Selector()
        .Sequence() // Enter the specified ship if not already in it.
            .InvertChild<ShipQueryNode>("request_ship")
            .Child<ShipRequestNode>("request_ship")
            .End()
        .Sequence()
            .Selector()
                .Selector() // Check if we are on a flagging frequency.
                    .Sequence()
                        .Child<PlayerFrequencyQueryNode>("self_freq")
                        .Child<EqualityNode<u16>>("self_freq", 90)
                        .End()
                    .Sequence()
                        .Child<PlayerFrequencyQueryNode>("self_freq")
                        .Child<EqualityNode<u16>>("self_freq", 91)
                        .End()
                    .End()
                .Sequence() // We aren't on a flagging frequency, so try to join one.
                    .Child<TimerExpiredNode>("join_flag_timer")
                    .Child<TimerSetNode>("join_flag_timer", 300)
                    .InvertChild<ChatMessageNode>(ChatMessageNode::Public("?flag")) // Invert so this fails and freq is reevaluated.
                    .End()
                .End()
            .Selector()
                .Sequence() // If we have less than 3 flags, find nearest one and path to it.
                    .Child<FlagCarryCountQueryNode>("flags_carried")
                    .Child<LessThanNode<u16>>("flags_carried", 3)
                    .Child<NearestFlagNode>(NearestFlagNode::Type::Unclaimed, "nearest_flag")
                    .Child<FlagPositionQueryNode>("nearest_flag", "flag_position")
                    .Child<GlobalGoToNode>("flag_position")
                    .Child<RenderPathNode>(Vector3f(0, 0, 0))
                    .End()
                .Sequence() // Traverse to the best sector's flag room.
                    .Child<BestFlagSectorNode>("best_sector")
                    .Child<SectorFlagRoomQueryNode>("best_sector", "flagroom_position")
                    .Child<GlobalGoToNode>("flagroom_position")
                    .Child<RenderPathNode>(Vector3f(0, 0, 0))
                    .End()
                .End()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

}  // namespace hyperspace
}  // namespace zero

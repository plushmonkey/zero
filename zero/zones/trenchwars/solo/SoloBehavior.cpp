#include "SoloBehavior.h"

#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/behavior/nodes/InputActionNode.h>
#include <zero/behavior/nodes/MapNode.h>
#include <zero/behavior/nodes/MathNode.h>
#include <zero/behavior/nodes/MoveNode.h>
#include <zero/behavior/nodes/PlayerNode.h>
#include <zero/behavior/nodes/ShipNode.h>
#include <zero/behavior/nodes/TimerNode.h>
#include <zero/behavior/nodes/WaypointNode.h>
#include <zero/zones/trenchwars/TrenchWars.h>
#include <zero/zones/trenchwars/solo/SoloWarbirdBehavior.h>

namespace zero {
namespace tw {

std::unique_ptr<behavior::BehaviorNode> SoloBehavior::CreateTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  BehaviorBuilder builder;

  // clang-format off

  // Render traversability grid for testing.
#if 0
  builder
    .Selector()
        .Sequence() // Enter the specified ship if not already in it.
            .InvertChild<ShipQueryNode>("request_ship")
            .Child<ShipRequestNode>("request_ship")
            .End()
        .Child<ExecuteNode>([](ExecuteContext& ctx) {
            auto self = ctx.bot->game->player_manager.GetSelf();
            if (!self || self->ship >= 8) return ExecuteResult::Failure;

            auto& game = *ctx.bot->game;
            auto& pathfinder = *ctx.bot->bot_controller->pathfinder;
            auto& processor = pathfinder.GetProcessor();
            
            auto pos = self->position;
            const float kShipRadius = 14.0f / 16.0f;
            const u16 radius = 30;

            //processor.SetDoorSolidMethod(path::DoorSolidMethod::AlwaysSolid);

            for (u16 y = pos.y - radius; y < pos.y + radius; ++y) {
              for (u16 x = pos.x - radius; x < pos.x + radius; ++x) {
                auto node = processor.GetNode(path::NodePoint(x, y));
                path::EdgeSet set = processor.FindEdges(node, kShipRadius);

                //processor.UpdateDynamicNode(node, kShipRadius, 0xFFFF);
                Vector2f start(x + 0.5f, y + 0.5f);
                Vector3f color(0, 1, 0);

                for (size_t i = 0; i < 4; ++i) {
                  path::CoordOffset offset = path::CoordOffset::FromIndex(i);
                  auto other_node = processor.GetNode(path::NodePoint(x + offset.x, y + offset.y));
                  path::EdgeSet other_set = processor.FindEdges(other_node, kShipRadius);

                  processor.UpdateDynamicNode(other_node, kShipRadius, 0xFFFF);

                  size_t opposite_i = i ^ 1;

                  //if (set.IsSet(i) && other_set.IsSet(opposite_i)) {
                  if (set.IsSet(i)) {
                    Vector2f end = start + Vector2f(offset.x, offset.y);

                    game.line_renderer.PushLine(start, color, end, color);
                  }
                }
              }
            }

            self->position = Vector2f(512, 269);

            game.line_renderer.Render(game.camera);

            return ExecuteResult::Success;
        })
        .End();
  return builder.Build();
#endif

  builder
    .Selector()
        .Sequence() // Enter the specified ship if not already in it.
            .InvertChild<ShipQueryNode>("request_ship")
            .Child<ShipRequestNode>("request_ship")
            .End()
        .Sequence() // Do nothing while waiting for spawn cooldown
            .InvertChild<TimerExpiredNode>(TrenchWars::SpawnExecuteCooldownKey())
            .End()
        .Sequence() // Switch to own frequency when possible.
            .Child<PlayerFrequencyCountQueryNode>("self_freq_count")
            .Child<ScalarThresholdNode<size_t>>("self_freq_count", 2)
            .Child<PlayerEnergyPercentThresholdNode>(1.0f)
            .Child<TimerExpiredNode>("next_freq_change_tick")
            .Child<TimerSetNode>("next_freq_change_tick", 300)
            .Child<RandomIntNode<u16>>(100, 9000, "random_freq")
            .Child<PlayerChangeFrequencyNode>("random_freq")
            .End()
        .Sequence() // If we are in spec, do nothing
            .Child<ShipQueryNode>(8)
            .End()
        .Sequence()
            .Child<ShipQueryNode>(0)
            .Composite(CreateSoloWarbirdTree(ctx))
            .End()
        .Sequence() // TODO: Jav
            .Child<ShipQueryNode>(1)
            .Composite(CreateSoloWarbirdTree(ctx))
            .End()
        .Sequence() // TODO: Spider
            .Child<ShipQueryNode>(2)
            .Composite(CreateSoloWarbirdTree(ctx))
            .End()
        .Sequence() // TODO: Levi
            .Child<ShipQueryNode>(3)
            .Composite(CreateSoloWarbirdTree(ctx))
            .End()
        .Sequence() // TODO: Terrier
            .Child<ShipQueryNode>(4)
            .Composite(CreateSoloWarbirdTree(ctx))
            .End()
        .Sequence() // TODO: Weasel
            .Child<ShipQueryNode>(5)
            .Composite(CreateSoloWarbirdTree(ctx))
            .End()
        .Sequence() // TODO: Lancaster
            .Child<ShipQueryNode>(6)
            .Composite(CreateSoloWarbirdTree(ctx))
            .End()
        .Sequence() // TODO: Shark
            .Child<ShipQueryNode>(7)
            .Composite(CreateSoloWarbirdTree(ctx))
            .End()
        .Sequence() // Follow set waypoints.
            .Child<WaypointNode>("waypoints", "waypoint_index", "waypoint_position", 15.0f)
            .Selector()
                .Sequence()
                    .Child<ShipTraverseQueryNode>("waypoint_position")
                    .Child<FaceNode>("waypoint_position")
                    .Child<ArriveNode>("waypoint_position", 1.25f)
                    .End()
                .Sequence()
                    .Child<GoToNode>("waypoint_position")
                    .End()                
                .End()
            .End()
        .Sequence() // Warp out if all above sequences failed. We must be in a non-traversable area because waypoint following failed to build a path.
            .Child<WarpNode>()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

}  // namespace tw
}  // namespace zero

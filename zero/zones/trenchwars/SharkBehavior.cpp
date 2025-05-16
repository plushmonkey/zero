#include "SharkBehavior.h"

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
#include <zero/zones/trenchwars/nodes/AttachNode.h>
#include <zero/zones/trenchwars/nodes/FlagNode.h>

namespace zero {
namespace tw {

static std::unique_ptr<behavior::BehaviorNode> CreateDefensiveTree() {
  using namespace behavior;

  BehaviorBuilder builder;

  // clang-format off
  builder
    .Sequence() // Attempt to dodge and use defensive items.
        .Sequence(CompositeDecorator::Success) // Always check incoming damage 
            .Child<RepelDistanceQueryNode>("repel_distance")
            .Child<svs::IncomingDamageQueryNode>("repel_distance", "incoming_damage")
            .Child<PlayerCurrentEnergyQueryNode>("self_energy")
            .End()
        .Sequence(CompositeDecorator::Success) // Use repel when in danger.
            .Child<ShipWeaponCapabilityQueryNode>(WeaponType::Repel)
            .Child<TimerExpiredNode>("defense_timer")
            .Child<ScalarThresholdNode<float>>("incoming_damage", 1.0f)
            .Child<InputActionNode>(InputAction::Repel)
            .Child<TimerSetNode>("defense_timer", 100)
            .End()
        .Child<DodgeIncomingDamage>(0.1f, 20.0f)
        .End();
  // clang-format on

  return builder.Build();
}

static std::unique_ptr<behavior::BehaviorNode> CreateOffensiveTree(const char* nearest_target_key,
                                                                   const char* nearest_target_position_key) {
  using namespace behavior;

  // How close to enemies we should be to lay a mine.
  constexpr float kNearEnemyMineDistance = 8.0f;

  BehaviorBuilder builder;

  // clang-format off
  builder
    .Sequence()
        .Parallel()
            .Sequence() // Always rush toward enemy
                .Child<SeekNode>(nearest_target_position_key, 0.0f, SeekNode::DistanceResolveType::Static)
                .End()
            .Sequence(CompositeDecorator::Success) // Lay a mine if we are very close to an enemy and not near teammate.
                .InvertChild<ShipWeaponCooldownQueryNode>(WeaponType::Bomb)
                .InvertChild<DistanceThresholdNode>(nearest_target_position_key, kNearEnemyMineDistance)
                .Selector() // Find the nearest terrier and don't lay a mine if it's near us.
                    .InvertChild<BestAttachQueryNode>(false, "nearest_terrier_player") // Invert this so we mark this selector as true when no terrier exists.
                    .Sequence() // If we had a nearest_terrier_player, check nearby distance.
                        .Child<PlayerPositionQueryNode>("nearest_terrier_player", "nearest_terrier_player_position")
                        .Child<DistanceThresholdNode>("nearest_terrier_player_position", 16.0f)
                        .End()
                    .End()
                .Selector() // Choose between bomb and mine
                    .Sequence()// Choose a mine if we have mines available
                        .Child<ShipMineCapableQueryNode>()
                        .Child<InputActionNode>(InputAction::Mine)
                        .End()
                    .Child<InputActionNode>(InputAction::Bomb)
                    .End()
                .End()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

// Lay mines in important areas such as entrance and on top of flags.
// TODO: Implement
static std::unique_ptr<behavior::BehaviorNode> CreateMineAreaBehavior() {
  return nullptr;
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
            .Sequence() // Attach to teammate if possible
                .InvertChild<AttachedQueryNode>("self")
                .Child<DistanceThresholdNode>("tw_flag_position", kNearFlagroomDistance)
                .Child<TimerExpiredNode>("attach_cooldown")
                .Child<BestAttachQueryNode>("best_attach_player")
                .Child<AttachNode>("best_attach_player")
                .Child<TimerSetNode>("attach_cooldown", 100)
                .End()
            .Sequence() // Detach when near flag room
                .Child<AttachedQueryNode>("self")
                .InvertChild<DistanceThresholdNode>("tw_flag_position", kNearFlagroomDistance)
                .Child<TimerExpiredNode>("attach_cooldown")
                .Child<DetachNode>()
                .Child<TimerSetNode>("attach_cooldown", 100)
                .End()
            .Sequence() // Go directly to the flag room if we aren't there.
                .InvertChild<InFlagroomNode>("self_position")
                .Child<GoToNode>("tw_flag_position")
                .Child<RenderPathNode>(Vector3f(0.0f, 1.0f, 0.5f))
                .End()
#if 0 // TODO: Enable this once a smarter version is created. As it is, sharks will just circle around it not attacking each other.
            .Sequence() // If we are the closest player to the unclaimed flag, touch it.
                .Child<InFlagroomNode>("self_position")
                .Child<NearestFlagNode>(NearestFlagNode::Type::Unclaimed, "nearest_flag")
                .Child<FlagPositionQueryNode>("nearest_flag", "nearest_flag_position")
                .Child<BestFlagClaimerNode>()
                .Selector()
                    .Sequence()
                        .InvertChild<ShipTraverseQueryNode>("nearest_flag_position")
                        .Child<GoToNode>("nearest_flag_position")
                        .End()
                    .Child<ArriveNode>("nearest_flag_position", 1.25f)
                    .End()
                .End()
#endif
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

std::unique_ptr<behavior::BehaviorNode> CreateSharkTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  BehaviorBuilder builder;

  // clang-format off
  builder
    .Selector()
        .Composite(CreateFlagroomTravelBehavior())
        .Sequence() // Find nearest target and either path to them or seek them directly.
            .Sequence() // Find an enemy
                .Child<PlayerPositionQueryNode>("self_position")
                .Child<svs::NearestMemoryTargetNode>("nearest_target")
                .Child<PlayerPositionQueryNode>("nearest_target", "nearest_target_position")
                .End()
            .Selector()
                .Composite(CreateDefensiveTree())
                .Sequence() // Go to enemy and attack if they are in the flag room.
                    .Child<InFlagroomNode>("nearest_target_position")
                    .Selector()
                        .Sequence()
                            .InvertChild<VisibilityQueryNode>("nearest_target_position")
                            .Child<GoToNode>("nearest_target_position")
                            .End()
                        .Composite(CreateOffensiveTree("nearest_target", "nearest_target_position"))
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

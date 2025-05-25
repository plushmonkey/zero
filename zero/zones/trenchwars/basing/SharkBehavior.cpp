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
#include <zero/zones/trenchwars/nodes/BaseNode.h>
#include <zero/zones/trenchwars/nodes/MoveNode.h>

namespace zero {
namespace tw {

static inline bool IsMine(Weapon& weapon) {
  return weapon.data.alternate &&
         (weapon.data.type == WeaponType::Bomb || weapon.data.type == WeaponType::ProximityBomb);
}

static inline bool IsMineNearby(const std::vector<Weapon*>& mines, Vector2f position) {
  for (auto weapon : mines) {
    if (weapon->position.DistanceSq(position) <= 0.5f) return true;
  }
  return false;
}

// Returns true if there are enemies with a lower Y value than us, so they are above us in the base.
struct EnemiesAboveNode : public behavior::BehaviorNode {
  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto& pm = ctx.bot->game->player_manager;
    auto self = pm.GetSelf();
    if (!self) return behavior::ExecuteResult::Failure;

    for (size_t i = 0; i < pm.player_count; ++i) {
      Player* player = pm.players + i;

      if (player->frequency == self->frequency) continue;
      if (player->IsRespawning()) continue;
      if (player->position.y >= self->position.y) continue;
      if (!pm.IsSynchronized(*player)) continue;
      if (player->position == Vector2f(0, 0)) continue;

      return behavior::ExecuteResult::Success;
    }

    return behavior::ExecuteResult::Failure;
  }
};

// Get all of the mines and store them in the blackboard.
// This is done to avoid expensive weapon iteration.
struct QueryMinesNode : public behavior::BehaviorNode {
  QueryMinesNode(const char* output_key) : output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto& wm = ctx.bot->game->weapon_manager;

    std::vector<Weapon*> mines;

    for (size_t i = 0; i < wm.weapon_count; ++i) {
      Weapon* weapon = wm.weapons + i;

      if (!IsMine(*weapon)) continue;

      mines.push_back(weapon);
    }

    ctx.blackboard.Set(output_key, mines);

    return behavior::ExecuteResult::Success;
  }

  const char* output_key = nullptr;
};

// Requires the mines from QueryMinesNode
struct FlagRequiresMinesNode : public behavior::BehaviorNode {
  FlagRequiresMinesNode(const char* mines_key, float search_distance, size_t desired_mine_count)
      : mines_key(mines_key),
        search_distance_sq(search_distance * search_distance),
        desired_mine_count(desired_mine_count) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self) return behavior::ExecuteResult::Failure;

    auto opt_mines = ctx.blackboard.Value<std::vector<Weapon*>>(mines_key);
    if (!opt_mines) return behavior::ExecuteResult::Failure;

    auto opt_tw = ctx.blackboard.Value<TrenchWars*>("tw");
    if (!opt_tw) return behavior::ExecuteResult::Failure;

    TrenchWars* tw = *opt_tw;
    std::vector<Weapon*> mines = *opt_mines;

    size_t count = 0;

    for (size_t i = 0; i < mines.size(); ++i) {
      if (mines[i]->frequency != self->frequency) continue;
      if (mines[i]->position.y > tw->flag_position.y) continue;  // Only count mines above flag
      if (mines[i]->position.DistanceSq(tw->flag_position) > search_distance_sq) continue;

      ++count;
    }

    return count < desired_mine_count ? behavior::ExecuteResult::Success : behavior::ExecuteResult::Failure;
  }

  const char* mines_key = nullptr;
  float search_distance_sq = 0.0f;
  size_t desired_mine_count = 3;
};

// Finds the best location to lay a mine around an area.
// Requires the mines from QueryMinesNode
struct FindBestMinePositionNode : public behavior::BehaviorNode {
  FindBestMinePositionNode(float y_offset, const char* mines_key, const char* output_key)
      : y_offset(y_offset), mines_key(mines_key), output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self) return behavior::ExecuteResult::Failure;

    auto opt_mines = ctx.blackboard.Value<std::vector<Weapon*>>(mines_key);
    if (!opt_mines) return behavior::ExecuteResult::Failure;

    auto opt_tw = ctx.blackboard.Value<TrenchWars*>("tw");
    if (!opt_tw) return behavior::ExecuteResult::Failure;

    TrenchWars* tw = *opt_tw;
    const std::vector<Weapon*>& mines = *opt_mines;

    Vector2f center = tw->flag_position + Vector2f(0, y_offset);
    Vector2f left = center + Vector2f(-2, 0);
    Vector2f right = center + Vector2f(2, 0);

    if (!IsMineNearby(mines, left)) {
      ctx.blackboard.Set(output_key, left);
      return behavior::ExecuteResult::Success;
    }

    if (!IsMineNearby(mines, center)) {
      ctx.blackboard.Set(output_key, center);
      return behavior::ExecuteResult::Success;
    }

    if (!IsMineNearby(mines, right)) {
      ctx.blackboard.Set(output_key, right);
      return behavior::ExecuteResult::Success;
    }

    return behavior::ExecuteResult::Success;
  }

  float y_offset = 0.0f;
  const char* mines_key = nullptr;
  const char* output_key = nullptr;
};

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
        .Child<DodgeIncomingDamage>(0.1f, 16.0f, 0.0f)
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
static std::unique_ptr<behavior::BehaviorNode> CreateMineAreaBehavior() {
  using namespace behavior;

  BehaviorBuilder builder;

  constexpr float kFlagMineDistance = 4.0f;
  constexpr size_t kFlagDesiredMineCount = 3;

  // clang-format off
  builder
    .Sequence()
        .Child<ShipMineCapableQueryNode>() // Check if we can currently lay a mine
        .Child<QueryMinesNode>("mines")
        .Sequence(CompositeDecorator::Success) // Make sure we are detached when we want to go find a place to lay a mine.
            .Child<AttachedQueryNode>()
            .Child<TimerExpiredNode>("attach_cooldown")
            .Child<DetachNode>()
            .Child<TimerSetNode>("attach_cooldown", 100)
            .End()
        .Selector()
            .Sequence() // Try to lay a node near flags
                .Child<FlagRequiresMinesNode>("mines", kFlagMineDistance, kFlagDesiredMineCount)
                .Child<FindBestMinePositionNode>(-1.5f, "mines", "mine_position")
                .End()
            .Sequence() // Try to lay a mine in the fr lower entrance
                .Child<FindBestMinePositionNode>(18.0f, "mines", "mine_position")
                .End()
            .Sequence() // Try to lay a mine in the fr upper entrance
                .Child<FindBestMinePositionNode>(6.0f, "mines", "mine_position")
                .End()
            .End()
        .Selector() // We have a mine location, so go lay it there.
            .Sequence()
                .InvertChild<ShipTraverseQueryNode>("mine_position")
                .Child<GoToNode>("mine_position")
                .End()
            .Sequence()
                .Child<ArriveNode>("mine_position", 1.25f)
                .Sequence(CompositeDecorator::Success)
                    .InvertChild<DistanceThresholdNode>("mine_position", 0.5f)
                    .Child<InputActionNode>(InputAction::Mine)
                    .End()
                .End()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

static std::unique_ptr<behavior::BehaviorNode> CreateFlagroomTravelBehavior() {
  using namespace behavior;

  BehaviorBuilder builder;

  constexpr float kNearFlagroomDistance = 45.0f;

  // clang-format off
  builder
    .Selector()
        .Composite(CreateDefensiveTree()) // Use repels to defend ourselves and terrier.
        .Sequence() // If there are no enemies above us, go mining
            .InvertChild<EnemiesAboveNode>()
            .InvertChild<DistanceThresholdNode>("tw_flag_position", kNearFlagroomDistance)
            .SuccessChild<svs::NearestMemoryTargetNode>("nearest_target", true)
            .SuccessChild<PlayerPositionQueryNode>("nearest_target", "nearest_target_position")
            .Selector(CompositeDecorator::Invert) // Invert twice here so the sequence fails and falls through in non-mine case.
                .Child<InFlagroomNode>("nearest_target_position") // We don't want to mine when we have a target in fr, and we don't want this sequence to succeed.
                .Composite(CreateMineAreaBehavior(), CompositeDecorator::Invert)
                .End()
            .End()
        .Sequence()
            .Child<PlayerSelfNode>("self")
            .Child<PlayerPositionQueryNode>("self_position")
            .Sequence(CompositeDecorator::Success) // Use afterburners to get to flagroom faster.
                .InvertChild<InFlagroomNode>("self_position")
                .Child<AfterburnerThresholdNode>()
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
                .Child<svs::NearestMemoryTargetNode>("nearest_target", true)
                .Child<PlayerPositionQueryNode>("nearest_target", "nearest_target_position")
                .End()
            .Selector()
                .Composite(CreateDefensiveTree())
                .Sequence() // Go to enemy and attack if they are in the flag room.
                    .Child<InFlagroomNode>("nearest_target_position")
                    .Selector()
                        .Sequence()
                            .InvertChild<ShipTraverseQueryNode>("nearest_target_position")
                            .Child<GoToNode>("nearest_target_position")
                            .End()
                        .Composite(CreateOffensiveTree("nearest_target", "nearest_target_position"))
                        .End()
                    .End()
                .Composite(CreateMineAreaBehavior()) // No enemy, so mine areas if possible
                .End()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

}  // namespace tw
}  // namespace zero

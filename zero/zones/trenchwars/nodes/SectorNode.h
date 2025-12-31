#pragma once

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/zones/trenchwars/TrenchWars.h>

namespace zero {
namespace tw {

// Gets the Trench Wars sector from the provided position_key. If no position_key is provided, self->position is used.
// Stores Sector in output_key.
// Use SectorEqualityNode to compare kind.
struct SectorQueryNode : public behavior::BehaviorNode {
  SectorQueryNode(const char* output_key) : output_key(output_key) {}
  SectorQueryNode(const char* position_key, const char* output_key)
      : position_key(position_key), output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    auto opt_tw = ctx.blackboard.Value<TrenchWars*>("tw");
    if (!opt_tw) return behavior::ExecuteResult::Failure;

    TrenchWars* tw = *opt_tw;

    Vector2f position = self->position;

    if (position_key) {
      auto opt_position = ctx.blackboard.Value<Vector2f>(position_key);
      if (!opt_position) return behavior::ExecuteResult::Failure;

      position = *opt_position;
    }

    Sector sector = tw->GetSector(position);

    ctx.blackboard.Set(output_key, sector);

    return behavior::ExecuteResult::Success;
  }

  const char* position_key = nullptr;
  const char* output_key = nullptr;
};

struct SectorEqualityNode : public behavior::BehaviorNode {
  SectorEqualityNode(const char* sector_key, Sector check) : sector_key(sector_key), sector_check(check) {}
  SectorEqualityNode(const char* sector_key, const char* check_key) : sector_key(sector_key), check_key(check_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    Sector check = sector_check;

    if (check_key) {
      auto opt_check = ctx.blackboard.Value<Sector>(check_key);
      if (!opt_check) return behavior::ExecuteResult::Failure;

      check = *opt_check;
    }

    auto opt_sector = ctx.blackboard.Value<Sector>(sector_key);
    if (!opt_sector) return behavior::ExecuteResult::Failure;

    Sector sector = *opt_sector;

    return sector == check ? behavior::ExecuteResult::Success : behavior::ExecuteResult::Failure;
  }

  const char* sector_key = nullptr;

  Sector sector_check = Sector::Flagroom;
  const char* check_key = nullptr;
};

}  // namespace tw
}  // namespace zero

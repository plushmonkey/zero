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

// Returns the Sector that is directly above this Sector.
struct SectorAboveNode : public behavior::BehaviorNode {
  SectorAboveNode(const char* sector_key, const char* output_key) : sector_key(sector_key), output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto opt_sector = ctx.blackboard.Value<Sector>(sector_key);
    if (!opt_sector) return behavior::ExecuteResult::Failure;

    Sector sector = *opt_sector;
    Sector above_sector = GetAboveSector(sector);

    ctx.blackboard.Set(output_key, above_sector);

    return behavior::ExecuteResult::Success;
  }

  const char* sector_key = nullptr;
  const char* output_key = nullptr;
};

// Returns the Sector that is directly below this Sector.
struct SectorBelowNode : public behavior::BehaviorNode {
  SectorBelowNode(const char* sector_key, const char* output_key) : sector_key(sector_key), output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto opt_sector = ctx.blackboard.Value<Sector>(sector_key);
    if (!opt_sector) return behavior::ExecuteResult::Failure;

    Sector sector = *opt_sector;
    Sector above_sector = GetBelowSector(sector);

    ctx.blackboard.Set(output_key, above_sector);

    return behavior::ExecuteResult::Success;
  }

  const char* sector_key = nullptr;
  const char* output_key = nullptr;
};

// Outputs a Vector2f into output_key for the bottom part of the sector in the horizontal center of the base.
// For example, Middle sector would be the top of the tunnel in the bottom part of the base.
struct SectorBottomCoordNode : public behavior::BehaviorNode {
  SectorBottomCoordNode(const char* sector_key, const char* output_key)
      : sector_key(sector_key), output_key(output_key) {}
  SectorBottomCoordNode(Sector sector, const char* output_key) : sector(sector), output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto opt_tw = ctx.blackboard.Value<TrenchWars*>("tw");
    if (!opt_tw) return behavior::ExecuteResult::Failure;

    TrenchWars* tw = *opt_tw;

    Sector find_sector = sector;
    if (sector_key) {
      auto opt_sector = ctx.blackboard.Value<Sector>(sector_key);
      if (!opt_sector) return behavior::ExecuteResult::Failure;

      find_sector = *opt_sector;
    }

    Vector2f position(tw->flag_position.x, 0);

    switch (sector) {
      case Sector::Entrance:
      case Sector::Flagroom: {
        position.y = (float)tw->fr_bottom_y;
      } break;
      case Sector::West:
      case Sector::East:
      case Sector::Middle: {
        position.y = (float)tw->middle_bottom_y;
      } break;
      case Sector::Bottom: {
        position.y = (float)tw->base_bottom_y;
      } break;
      default: {
        return behavior::ExecuteResult::Failure;
      }
    }

    ctx.blackboard.Set(output_key, position);

    return behavior::ExecuteResult::Success;
  }

  Sector sector = Sector::Middle;
  const char* sector_key = nullptr;
  const char* output_key = nullptr;
};

}  // namespace tw
}  // namespace zero

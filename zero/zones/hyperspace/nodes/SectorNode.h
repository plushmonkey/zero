#pragma once

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/Behavior.h>
#include <zero/zones/hyperspace/Hyperspace.h>
//
#include <time.h>

namespace zero {
namespace hyperspace {

struct SectorUnclaimedFlagCountNode : public behavior::BehaviorNode {
  SectorUnclaimedFlagCountNode(size_t sector, const char* output_key) : sector(sector), output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self) return behavior::ExecuteResult::Failure;

    if (sector >= ZERO_ARRAY_SIZE(kSectorPositions)) return behavior::ExecuteResult::Failure;

    Vector2f sector_position = kSectorPositions[sector];
    size_t count = 0;

    for (size_t i = 0; i < ctx.bot->game->flag_count; ++i) {
      GameFlag* flag = ctx.bot->game->flags + i;

      if (!(flag->flags & GameFlag_Dropped) && !(flag->flags & GameFlag_Turf)) continue;

      if (ctx.bot->bot_controller->region_registry->IsConnected(flag->position, sector_position)) {
        ++count;
      }
    }

    ctx.blackboard.Set(output_key, count);

    return behavior::ExecuteResult::Success;
  }

  size_t sector = 0;
  const char* output_key = nullptr;
};

// Returns a position within the sector's flag room.
struct SectorFlagRoomQueryNode : public behavior::BehaviorNode {
  SectorFlagRoomQueryNode(size_t sector, const char* output_key) : sector(sector), output_key(output_key) {}
  SectorFlagRoomQueryNode(const char* sector_key, const char* output_key)
      : sector_key(sector_key), output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    size_t query_sector = sector;

    if (sector_key) {
      auto opt_sector = ctx.blackboard.Value<size_t>(sector_key);
      if (!opt_sector) return behavior::ExecuteResult::Failure;
      query_sector = *opt_sector;
    }

    if (query_sector > 6) return behavior::ExecuteResult::Failure;

    ctx.blackboard.Set(output_key, kSectorPositions[query_sector]);

    return behavior::ExecuteResult::Success;
  }

  size_t sector = 0;
  const char* sector_key = nullptr;
  const char* output_key = nullptr;
};

// Finds the best sector for the flag game.
// It will prioritize sectors that have a majority of the flags within it, otherwise it uses time to get a deterministic
// unique one so the bots can synchronize.
// The bots should continue to use the same flag room during a game, but once it finishes then it should find a new
// base.
struct BestFlagSectorNode : public behavior::BehaviorNode {
  BestFlagSectorNode(const char* output_key) : output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self) return behavior::ExecuteResult::Failure;

    int counts[7] = {};
    bool external_flag = false;

    for (size_t i = 0; i < ctx.bot->game->flag_count; ++i) {
      int sector = GetSectorFromPosition(*ctx.bot->bot_controller->region_registry, ctx.bot->game->flags[i].position);

      if (sector < ZERO_ARRAY_SIZE(counts)) {
        counts[sector]++;
        external_flag = true;
      }
    }

    if (external_flag) {
      int best_sector = 0;
      int best_sector_count = 0;

      // Loop through the counts to find which sector has the most flags. Skip center and sector 8.
      for (size_t i = 0; i < ZERO_ARRAY_SIZE(counts); ++i) {
        if (counts[i] > best_sector_count) {
          best_sector_count = counts[i];
          best_sector = (int)i;
        }
      }

      ctx.blackboard.Set<size_t>(output_key, best_sector);

      return behavior::ExecuteResult::Success;
    }

    time_t now = time(nullptr);
    if (now == -1) return behavior::ExecuteResult::Failure;

    tm* utc_tm = gmtime(&now);
    if (!utc_tm) return behavior::ExecuteResult::Failure;

    int day = utc_tm->tm_wday;
    int hour = utc_tm->tm_hour;

    // Multiply by primes to get a unique sector
    int value = day * 6691 + hour * 433;
    int target_sector = (value % 7);

    ctx.blackboard.Set<size_t>(output_key, target_sector);

    return behavior::ExecuteResult::Success;
  }

  const char* output_key = nullptr;
};

}  // namespace hyperspace
}  // namespace zero

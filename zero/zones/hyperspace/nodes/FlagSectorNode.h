#pragma once

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/Behavior.h>
#include <zero/zones/hyperspace/Hyperspace.h>
//
#include <time.h>

namespace zero {
namespace hyperspace {

// Finds the best sector for the flag game.
// It will prioritize sectors that have a majority of the flags within it, otherwise it uses time to get a deterministic
// unique one so the bots can synchronize.
// The bots should continue to use the same flag room during a game, but once it finishes then it should find a new
// base.
struct FlagSectorNode : public behavior::BehaviorNode {
  FlagSectorNode(const char* output_key) : output_key(output_key) {}

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
      int best_sector = 1;
      int best_sector_count = 0;

      // Loop through the counts to find which sector has the most flags. Skip center and sector 8.
      for (size_t i = 0; i < ZERO_ARRAY_SIZE(counts); ++i) {
        if (counts[i] > best_sector_count) {
          best_sector_count = counts[i];
          best_sector = (int)(i + 1);
        }
      }

      ctx.blackboard.Set(output_key, best_sector);

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
    int target_sector = (value % 7) + 1;

    ctx.blackboard.Set(output_key, target_sector);

    return behavior::ExecuteResult::Success;
  }

  const char* output_key = nullptr;
};

}  // namespace hyperspace
}  // namespace zero

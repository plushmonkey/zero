#pragma once

#include <zero/ChatQueue.h>
#include <zero/Math.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/Behavior.h>
#include <zero/behavior/BehaviorBuilder.h>
#include <zero/commands/CommandSystem.h>
#include <zero/game/GameEvent.h>

namespace zero {
namespace deva {

constexpr const char* const kSelectedTrackKey = "race_base_index";
constexpr const char* const kRaceWarpersKey = "race_warpers";
constexpr const char* const kRaceTargetsKey = "race_targets";
constexpr size_t kRaceTrackCount = 22;

struct RaceCommand : public CommandExecutor {
  void Execute(CommandSystem& cmd, ZeroBot& bot, const std::string& sender, const std::string& arg) override {
    if (arg.empty()) {
      SendUsage(sender);
      return;
    }

    int track_index = (int)strtol(arg.data(), nullptr, 10) - 1;
    if (track_index < 0 || track_index > (kRaceTrackCount - 1)) {
      SendUsage(sender);
      return;
    }

    bot.execute_ctx.blackboard.Set<size_t>(kSelectedTrackKey, (size_t)track_index);
  }

  void SendUsage(const std::string& target_player) {
    std::string usage = "Requires the track index to be the first argument.";
    Event::Dispatch(ChatQueueEvent::Private(target_player.data(), usage.data()));
  }

  CommandAccessFlags GetAccess() override { return CommandAccess_Private; }
  std::vector<std::string> GetAliases() override { return {"race"}; }
  std::string GetDescription() override { return "Sets the race track."; }
};

struct FrogRaceBehavior : public behavior::Behavior {
  void OnInitialize(behavior::ExecuteContext& ctx) override {
    // Setup blackboard here for this specific behavior
    ctx.blackboard.Set("request_ship", 1);
    ctx.blackboard.Set<size_t>(kSelectedTrackKey, 0);

    Vector2f warpers[kRaceTrackCount] = {};

    for (size_t i = 0; i < 10; ++i) {
      warpers[i] = Vector2f(476.0f + 8.0f * i, 500.0f);
      warpers[i + 10] = Vector2f(476.0f + 8.0f * i, 520.0f);
    }

    warpers[20] = Vector2f(554.0f, 513.0f);
    warpers[21] = Vector2f(554.0f, 505.0f);

    ctx.blackboard.Set(kRaceWarpersKey, std::vector<Vector2f>(warpers, warpers + ZERO_ARRAY_SIZE(warpers)));

    Vector2f race_targets[kRaceTrackCount] = {
        Vector2f(111, 183),   // 1
        Vector2f(488, 75),    // 2
        Vector2f(695, 124),   // 3
        Vector2f(834, 103),   // 4
        Vector2f(125, 371),   // 5
        Vector2f(223, 429),   // 6
        Vector2f(524, 300),   // 7
        Vector2f(746, 398),   // 8
        Vector2f(977, 350),   // 9
        Vector2f(175, 754),   // 10
        Vector2f(387, 475),   // 11
        Vector2f(686, 558),   // 12
        Vector2f(970, 573),   // 13
        Vector2f(231, 986),   // 14
        Vector2f(367, 827),   // 15
        Vector2f(339, 962),   // 16
        Vector2f(589, 911),   // 17
        Vector2f(905, 905),   // 18
        Vector2f(886, 1008),  // 19
        Vector2f(923, 915),   // 20
        Vector2f(242, 866),   // 21
        Vector2f(150, 518),   // 22
    };

    ctx.blackboard.Set(kRaceTargetsKey,
                       std::vector<Vector2f>(race_targets, race_targets + ZERO_ARRAY_SIZE(race_targets)));
  }

  std::unique_ptr<behavior::BehaviorNode> CreateTree(behavior::ExecuteContext& ctx) override;
};

}  // namespace deva
}  // namespace zero

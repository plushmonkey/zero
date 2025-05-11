#pragma once

#include <zero/BotController.h>
#include <zero/RegionRegistry.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

namespace zero {
namespace nexus {

struct PlayerByNameNode : public behavior::BehaviorNode {
  PlayerByNameNode(const char* output_key) : output_key(output_key) {}
  PlayerByNameNode(const char* name_key, const char* output_key) : name_key(name_key), output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto& player_manager = ctx.bot->game->player_manager;
    auto player = player_manager.GetSelf();

    if (name_key) {
      auto opt_name = ctx.blackboard.Value<std::string>(name_key);
      if (!opt_name) return behavior::ExecuteResult::Failure;
      player = player_manager.GetPlayerByName(opt_name->c_str());
    }

    if (!player) return behavior::ExecuteResult::Failure;

    ctx.blackboard.Set(output_key, player);

    return behavior::ExecuteResult::Success;
  }

  const char* name_key = nullptr;
  const char* output_key = nullptr;
};

}  // namespace nexus
}  // namespace zero

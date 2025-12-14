#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/game/GameEvent.h>
#include <zero/game/Logger.h>
#include <zero/zones/ZoneController.h>
#include <zero/zones/devastation/BaseManager.h>
#include <zero/zones/devastation/FrogRaceBehavior.h>
#include <zero/zones/devastation/base/TestBehavior.h>
#include <zero/zones/devastation/center/CenterBehavior.h>
#include <zero/zones/extremegames/BaseBehavior.h>
#include <zero/zones/extremegames/CenterBehavior.h>
#include <zero/zones/extremegames/ExtremeGames.h>
#include <zero/zones/trenchwars/TrenchWars.h>
#include <zero/zones/trenchwars/basing/BasingBehavior.h>
#include <zero/zones/trenchwars/solo/SoloBehavior.h>
#include <zero/zones/trenchwars/team/TeamBehavior.h>
#include <zero/zones/trenchwars/turret/TurretBehavior.h>

#include <string>
#include <string_view>
#include <unordered_map>

namespace zero {
namespace deva {

enum class ArenaType {
  Public,
  PublicExtremeGames,
  PublicTrenchWars,
  BaseduelDevastation,
  BaseduelExtremeGames,
};

static inline std::string to_string(ArenaType arena_type) {
  const char* kTypes[] = {"Public", "PublicExtremeGames", "PublicTrenchWars", "BaseduelDevastation",
                          "BaseduelExtremeGames"};

  size_t index = (size_t)arena_type;
  if (index < sizeof(kTypes) / sizeof(*kTypes)) {
    return kTypes[index];
  }

  return "Unknown";
}

static const std::unordered_map<std::string_view, ArenaType> kArenaTypes = {
    {"bdeg", ArenaType::BaseduelExtremeGames}, {"bd", ArenaType::BaseduelDevastation},
    {"bdd", ArenaType::BaseduelDevastation},   {"egpub", ArenaType::PublicExtremeGames},
    {"trench", ArenaType::PublicTrenchWars},
};

static ArenaType GetArenaTypeFromName(const char* arena_name, ArenaType default_type) {
  auto iter = kArenaTypes.find(arena_name);
  if (iter == kArenaTypes.end()) return default_type;
  return iter->second;
}

struct Devastation {
  std::unique_ptr<tw::TrenchWars> trench_wars;
  std::unique_ptr<eg::ExtremeGames> extreme_games;
  std::unique_ptr<BaseManager> base_manager;

  void Clear(behavior::ExecuteContext& ctx) {
    trench_wars = nullptr;
    extreme_games = nullptr;
    base_manager = nullptr;

    ctx.blackboard.Erase("eg");
    ctx.blackboard.Erase("tw");
    ctx.blackboard.Erase("tw_flag_position");
    ctx.blackboard.Erase("base_manager");
  }
};

struct LoadArenaCommand : public CommandExecutor {
  void Execute(CommandSystem& cmd, ZeroBot& bot, const std::string& sender, const std::string& arg) override;

  void SendUsage(const std::string& target_player) {
    const char* usage = "Requires an arena name. Ex: !loadarena egpub";
    Event::Dispatch(ChatQueueEvent::Private(target_player.data(), usage));
  }

  CommandAccessFlags GetAccess() override { return CommandAccess_Private; }
  std::vector<std::string> GetAliases() override { return {"loadarena"}; }
  std::string GetDescription() override { return "Loads a set of behaviors from an arena name."; }
};

struct WarpToCommand : public CommandExecutor {
  void Execute(CommandSystem& cmd, ZeroBot& bot, const std::string& sender, const std::string& arg) override {
    auto split_pos = arg.find(',');

    if (split_pos == std::string::npos) {
      SendUsage(sender);
      return;
    }

    int first_coord = (int)strtol(arg.data(), nullptr, 10);
    int second_coord = (int)strtol(arg.data() + split_pos + 1, nullptr, 10);

    if (first_coord < 0 || second_coord < 0 || first_coord > 1023 || second_coord > 1023) {
      SendUsage(sender);
      return;
    }

    auto self = bot.game->player_manager.GetSelf();
    if (self) {
      self->position = Vector2f((float)first_coord + 0.5f, (float)second_coord + 0.5f);
      self->togglables |= Status_Flash;

      Event::Dispatch(TeleportEvent(*self));
    }
  }

  void SendUsage(const std::string& target_player) {
    std::string usage = "Requires coordinate in x, y form. Ex: warpto 512, 600";
    Event::Dispatch(ChatQueueEvent::Private(target_player.data(), usage.data()));
  }

  CommandAccessFlags GetAccess() override { return CommandAccess_Private; }
  std::vector<std::string> GetAliases() override { return {"warpto"}; }
  std::string GetDescription() override { return "Warps to a provided coord."; }
};

struct DevastationController : ZoneController {
  Devastation devastation;

  bool IsZone(Zone zone) override {
    bot->execute_ctx.blackboard.Erase("base_manager");
    devastation.Clear(bot->execute_ctx);
    return zone == Zone::Devastation;
  }

  void CreateBehaviors(const char* arena_name) override;
  void LoadArenaType(ArenaType arena_type);
};

static DevastationController controller;

void DevastationController::CreateBehaviors(const char* arena_name) {
  // Create behaviors depending on arena name
  Log(LogLevel::Info, "Registering Devastation behaviors.");

  bot->commands->RegisterCommand(std::make_shared<WarpToCommand>());
  bot->commands->SetCommandSecurityLevel("warpto", 10);

  bot->commands->RegisterCommand(std::make_shared<LoadArenaCommand>());
  bot->commands->SetCommandSecurityLevel("loadarena", 10);

  ArenaType arena_type = GetArenaTypeFromName(arena_name, ArenaType::Public);

  LoadArenaType(arena_type);
}

void DevastationController::LoadArenaType(ArenaType arena_type) {
  bot->bot_controller->behaviors.Clear();
  bot->bot_controller->energy_tracker.estimate_type = EnergyHeuristicType::Maximum;

  devastation.Clear(bot->execute_ctx);

  Log(LogLevel::Debug, "Loading arena type: %s", to_string(arena_type).data());

  auto& repo = bot->bot_controller->behaviors;

  switch (arena_type) {
    case ArenaType::Public: {
      repo.Add("center", std::make_unique<CenterBehavior>());

      SetBehavior("center");
    } break;
    case ArenaType::PublicExtremeGames: {
      devastation.extreme_games = std::make_unique<eg::ExtremeGames>();
      devastation.extreme_games->CreateBases(*bot);
      bot->execute_ctx.blackboard.Set("eg", devastation.extreme_games.get());

      repo.Add("center", std::make_unique<eg::CenterBehavior>());
      repo.Add("base", std::make_unique<eg::BaseBehavior>());

      SetBehavior("base");

      bot->bot_controller->energy_tracker.estimate_type = EnergyHeuristicType::Average;
    } break;
    case ArenaType::PublicTrenchWars: {
      bot->bot_controller->energy_tracker.estimate_type = EnergyHeuristicType::Maximum;
      devastation.trench_wars = std::make_unique<tw::TrenchWars>(*bot);
      devastation.trench_wars->BuildFlagroom(*bot);
      bot->execute_ctx.blackboard.Set("tw", devastation.trench_wars.get());

      repo.Add("basing", std::make_unique<tw::BasingBehavior>());
      repo.Add("solo", std::make_unique<tw::SoloBehavior>());
      repo.Add("turret", std::make_unique<tw::TurretBehavior>());
      repo.Add("team", std::make_unique<tw::TeamBehavior>());

      SetBehavior("basing");
    } break;
    case ArenaType::BaseduelExtremeGames:
    case ArenaType::BaseduelDevastation: {
      devastation.base_manager = std::make_unique<BaseManager>(*this->bot);
      bot->execute_ctx.blackboard.Set("base_manager", devastation.base_manager.get());

      repo.Add("center", std::make_unique<CenterBehavior>());
      repo.Add("test", std::make_unique<TestBehavior>());

      SetBehavior("center");
    } break;
    default: {
      repo.Add("center", std::make_unique<CenterBehavior>());
      SetBehavior("center");
    } break;
  }
}

void LoadArenaCommand::Execute(CommandSystem& cmd, ZeroBot& bot, const std::string& sender, const std::string& arg) {
  if (arg.empty()) {
    SendUsage(sender);
    return;
  }

  ArenaType arena_type = GetArenaTypeFromName(arg.data(), ArenaType::Public);

  controller.LoadArenaType(arena_type);

  std::string message = "Loaded behaviors from arena type: " + to_string(arena_type);
  Event::Dispatch(ChatQueueEvent::Private(sender.data(), message.data()));
}

}  // namespace deva
}  // namespace zero

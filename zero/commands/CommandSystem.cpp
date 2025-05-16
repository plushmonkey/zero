#include "CommandSystem.h"

#include <stdlib.h>
#include <zero/BotController.h>
#include <zero/ChatQueue.h>
#include <zero/Utility.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/Behavior.h>
#include <zero/game/Game.h>
#include <zero/game/Logger.h>
#include <zero/game/net/PacketDispatcher.h>

namespace zero {

static inline std::string GetCurrentBehaviorName(ZeroBot& bot) {
  if (bot.bot_controller && !bot.bot_controller->behavior_name.empty()) {
    return bot.bot_controller->behavior_name;
  }

  return "none";
}

class ReloadCommand : public CommandExecutor {
  void Execute(CommandSystem& cmd, ZeroBot& bot, const std::string& sender, const std::string& arg) override {
    if (!bot.config) {
      Event::Dispatch(ChatQueueEvent::Private(sender.data(), "Failed to reload config: Unknown file path."));
      return;
    }

    std::unique_ptr<Config> new_cfg = zero::Config::Load(bot.config->filepath.data());
    if (!new_cfg) {
      Event::Dispatch(ChatQueueEvent::Private(sender.data(), "Failed to reload config: Config parsing failed."));
      return;
    }

    bot.config = std::move(new_cfg);

    cmd.LoadSecurityLevels();

    Event::Dispatch(ChatQueueEvent::Private(sender.data(), "Config reload successful."));
  }

  CommandAccessFlags GetAccess() override { return CommandAccess_Standard; }
  std::vector<std::string> GetAliases() override { return {"reload"}; }
  std::string GetDescription() override { return "Reloads the config file."; }
};

class QuitCommand : public CommandExecutor {
  void Execute(CommandSystem& cmd, ZeroBot& bot, const std::string& sender, const std::string& arg) override {
    bot.game->connection.SendDisconnect();
    exit(0);
  }

  CommandAccessFlags GetAccess() override { return CommandAccess_Standard; }
  std::vector<std::string> GetAliases() override { return {"quit"}; }
  std::string GetDescription() override { return "Shuts the bot down completely."; }
};

class BehaviorsCommand : public CommandExecutor {
 public:
  void Execute(CommandSystem& cmd, ZeroBot& bot, const std::string& sender, const std::string& arg) override {
    std::string current = std::string("Current: ") + GetCurrentBehaviorName(bot);
    Event::Dispatch(ChatQueueEvent::Private(sender.data(), current.data()));

    auto& map = bot.bot_controller->behaviors.behaviors;
    std::string output = "none";

    if (map.empty()) {
      Event::Dispatch(ChatQueueEvent::Private(sender.data(), output.data()));
      return;
    }

    // The max size of a message combining the behaviors. This will cause it to split into new messages when it would go
    // over this limit.
    constexpr size_t kMaxMessageSize = 200;

    for (auto& kv : map) {
      size_t total_size = output.size() + kv.first.size() + 2;

      if (total_size >= kMaxMessageSize) {
        Event::Dispatch(ChatQueueEvent::Private(sender.data(), output.data()));
        output.clear();
        output = kv.first;
      } else {
        output += ", " + kv.first;
      }
    }

    if (!output.empty()) {
      Event::Dispatch(ChatQueueEvent::Private(sender.data(), output.data()));
    }
  }

  CommandAccessFlags GetAccess() override { return CommandAccess_Private | CommandAccess_RemotePrivate; }
  std::vector<std::string> GetAliases() override { return {"behaviors"}; }
  std::string GetDescription() override { return "Lists all possible behaviors."; }
};

class BehaviorCommand : public CommandExecutor {
 public:
  void Execute(CommandSystem& cmd, ZeroBot& bot, const std::string& sender, const std::string& arg) override {
    bool success = false;

    if (arg.empty()) {
      std::string current = std::string("Current: ") + GetCurrentBehaviorName(bot);
      Event::Dispatch(ChatQueueEvent::Private(sender.data(), current.data()));
      return;
    }

    behavior::Behavior* find_behavior = nullptr;

    if (arg == "none") {
      success = true;

      bot.bot_controller->SetBehavior("none", nullptr);
    } else {
      find_behavior = bot.bot_controller->behaviors.Find(arg);

      if (find_behavior) {
        find_behavior->OnInitialize(bot.execute_ctx);

        bot.bot_controller->SetBehavior(arg, find_behavior->CreateTree(bot.execute_ctx));

        success = true;
      }
    }

    if (success) {
      std::string response = std::string("Behavior set to '") + arg + "'.";
      Event::Dispatch(ChatQueueEvent::Private(sender.data(), response.data()));
    } else {
      std::string response = std::string("Failed to find behavior '") + arg + "'. PM !behaviors to see the full list.";
      Event::Dispatch(ChatQueueEvent::Private(sender.data(), response.data()));
    }
  }

  CommandAccessFlags GetAccess() override { return CommandAccess_Standard; }
  std::vector<std::string> GetAliases() override { return {"behavior", "b"}; }
  std::string GetDescription() override { return "Sets the active behavior."; }
};

class SayCommand : public CommandExecutor {
 public:
  void Execute(CommandSystem& cmd, ZeroBot& bot, const std::string& sender, const std::string& arg) override {
    const char* kFilters[] = {"?password", "?passwd", "?squad"};

    for (size_t i = 0; i < ZERO_ARRAY_SIZE(kFilters); ++i) {
      if (arg.find(kFilters[i]) != std::string::npos) {
        Event::Dispatch(ChatQueueEvent::Private(sender.data(), "Message filtered."));
        return;
      }
    }

    // Handle private messages
    if (!arg.empty() && arg[0] == ':') {
      size_t pm_end = arg.find(':', 1);
      if (pm_end != std::string::npos) {
        std::string to = arg.substr(1, pm_end - 1);

        Event::Dispatch(ChatQueueEvent::Private(to.data(), arg.data() + pm_end + 1));
        return;
      }
    }

    Event::Dispatch(ChatQueueEvent::Public(arg.data()));
  }

  CommandAccessFlags GetAccess() override { return CommandAccess_Standard; }
  std::vector<std::string> GetAliases() override { return {"say"}; }
  std::string GetDescription() override { return "Repeats a message publicly"; }
};

class GoCommand : public CommandExecutor {
 public:
  void Execute(CommandSystem& cmd, ZeroBot& bot, const std::string& sender, const std::string& arg) override {
    auto& connection = bot.game->connection;
    auto login_args = ParseLoginArena(arg);

    connection.SendArenaLogin(8, 0, 1920, 1080, login_args.first, login_args.second.data());
  }

  CommandAccessFlags GetAccess() override { return CommandAccess_Standard; }
  std::vector<std::string> GetAliases() override { return {"go"}; }
  std::string GetDescription() override { return "Moves to another arena."; }
};

class InfoCommand : public CommandExecutor {
 public:
  void Execute(CommandSystem& cmd, ZeroBot& bot, const std::string& sender, const std::string& arg) override {
    if (sender.empty()) return;

    Player* self = bot.game->player_manager.GetSelf();
    if (!self) return;

    u32 map_coord_x = (u32)floor(self->position.x / (1024 / 20.0f));
    u32 map_coord_y = (u32)floor(self->position.y / (1024 / 20.0f)) + 1;

    u32 tile_x = (u32)(self->position.x);
    u32 tile_y = (u32)(self->position.y);

    const char* zone_name = to_string(bot.server_info.zone);

    char location_message[256];
    snprintf(location_message, sizeof(location_message), "Location: '%s:%s' %c%u (%u, %u)", zone_name,
             bot.game->arena_name, 'A' + map_coord_x, map_coord_y, tile_x, tile_y);

    Event::Dispatch(ChatQueueEvent::Private(sender.data(), location_message));

    std::string behavior_name = GetCurrentBehaviorName(bot);
    std::string behavior_message = std::string("Behavior: ") + behavior_name;

    Event::Dispatch(ChatQueueEvent::Private(sender.data(), behavior_message.data()));
  }

  CommandAccessFlags GetAccess() override { return CommandAccess_Standard; }
  std::vector<std::string> GetAliases() override { return {"info", "i"}; }
  std::string GetDescription() override { return "Displays current state of bot."; }
};

class SetShipCommand : public CommandExecutor {
 public:
  void Execute(CommandSystem& cmd, ZeroBot& bot, const std::string& sender, const std::string& arg) override {
    if (sender.empty()) return;

    int parsed_ship_num = 0;

    auto args = Tokenize(arg, ' ');

    if (args.size() != 1 || args[0].empty()) {
      SendUsage(bot, sender.data());
      return;
    }

    char value = args[0][0];

    // Return to previous ship.
    if (value == 'p' || value == 'P') {
      auto opt_previous = bot.execute_ctx.blackboard.Value<int>("previous_ship");

      if (opt_previous) {
        // Toggle request_ship and previous_ship
        SetPreviousShip(bot);
        bot.execute_ctx.blackboard.Set<int>("request_ship", *opt_previous);
      }

      return;
    }

    if (!isdigit(value)) {
      SendUsage(bot, sender.data());
      return;
    }

    parsed_ship_num = value - '0';

    if (parsed_ship_num < 1 || parsed_ship_num > 9) {
      SendUsage(bot, sender.data());
      return;
    }

    SetPreviousShip(bot);
    bot.execute_ctx.blackboard.Set<int>("request_ship", parsed_ship_num - 1);
  }

  void SetPreviousShip(ZeroBot& bot) {
    auto opt_request_ship = bot.execute_ctx.blackboard.Value<int>("request_ship");

    if (opt_request_ship) {
      // Use our "request_ship" value as our previous ship.
      bot.execute_ctx.blackboard.Set<int>("previous_ship", *opt_request_ship);
    } else {
      auto self = bot.game->player_manager.GetSelf();

      if (self) {
        // Use our current ship as our previous ship in case we don't have "request_ship" set.
        bot.execute_ctx.blackboard.Set<int>("previous_ship", self->ship);
      }
    }
  }

  void SendUsage(ZeroBot& bot, const char* player) {
    Event::Dispatch(ChatQueueEvent::Private(player, "Usage: !setship [shipNum 1-9|previous]"));
  }

  CommandAccessFlags GetAccess() override { return CommandAccess_Standard; }
  std::vector<std::string> GetAliases() override { return {"setship", "ss"}; }
  std::string GetDescription() override { return "Sets the ship"; }
};

class HelpCommand : public CommandExecutor {
 public:
  void Execute(CommandSystem& cmd, ZeroBot& bot, const std::string& sender, const std::string& arg) override {
    if (sender.empty()) return;

    Event::Dispatch(ChatQueueEvent::Private(sender.data(), "-- !commands {.c} -- see command list (pm)"));
    Event::Dispatch(ChatQueueEvent::Private(sender.data(), "Code: https://github.com/plushmonkey/zero"));
    auto owner_msg = std::string("Owner: ") + bot.owner;
    Event::Dispatch(ChatQueueEvent::Private(sender.data(), owner_msg.data()));
  }

  CommandAccessFlags GetAccess() override { return CommandAccess_Private | CommandAccess_RemotePrivate; }
  std::vector<std::string> GetAliases() override { return {"help", "h"}; }
  std::string GetDescription() override { return "Helps"; }
};

class CommandsCommand : public CommandExecutor {
 public:
  void Execute(CommandSystem& cmd, ZeroBot& bot, const std::string& sender, const std::string& arg) override {
    if (sender.empty()) return;

    int requester_level = cmd.GetSecurityLevel(sender);
    Commands& commands = cmd.GetCommands();
    // Store a list of executors so multiple triggers linking to the same executor aren't displayed on different lines.
    std::vector<CommandExecutor*> executors;

    // Store the triggers so they can be sorted before output
    struct Trigger {
      CommandExecutor* executor;
      std::string triggers;

      Trigger(CommandExecutor* executor, const std::string& triggers) : executor(executor), triggers(triggers) {}
    };

    std::vector<Trigger> triggers;

    // Loop through every registered command and store the combined triggers
    for (auto& entry : commands) {
      CommandExecutor* executor = entry.second.get();

      if (std::find(executors.begin(), executors.end(), executor) == executors.end()) {
        std::string output;

        if (requester_level >= executor->GetSecurityLevel()) {
          std::vector<std::string> aliases = executor->GetAliases();

          // Combine all the aliases
          for (std::size_t i = 0; i < aliases.size(); ++i) {
            if (i != 0) {
              output += "/";
            }

            output += "-- !" + aliases[i];
          }

          triggers.emplace_back(executor, output);
        }

        executors.push_back(executor);
      }
    }

    // Sort triggers alphabetically
    std::sort(triggers.begin(), triggers.end(),
              [](const Trigger& l, const Trigger& r) { return l.triggers.compare(r.triggers) < 0; });

    // Display the stored triggers
    for (Trigger& trigger : triggers) {
      std::string desc = trigger.executor->GetDescription();
      int security = trigger.executor->GetSecurityLevel();

      std::string output = trigger.triggers + " - " + desc + " [" + std::to_string(security) + "]";

      Event::Dispatch(ChatQueueEvent::Private(sender.data(), output.data()));
    }
  }

  CommandAccessFlags GetAccess() override { return CommandAccess_Private; }
  std::vector<std::string> GetAliases() override { return {"commands", "c"}; }
  std::string GetDescription() override { return "Shows available commands"; }
};

static inline std::string Lowercase(std::string_view str) {
  std::string result;

  std::string_view name = str;

  // remove "^" that gets placed on names when biller is down
  if (!name.empty() && name[0] == '^') {
    name = name.substr(1);
  }

  result.resize(name.size());

  std::transform(name.begin(), name.end(), result.begin(), ::tolower);

  return result;
}

static void RawOnChatPacket(void* user, u8* pkt, size_t size) {
  CommandSystem* cmd = (CommandSystem*)user;

  cmd->OnChatPacket(pkt, size);
}

CommandSystem::CommandSystem(ZeroBot& bot, PacketDispatcher& dispatcher) : bot(bot) {
  dispatcher.Register(ProtocolS2C::Chat, RawOnChatPacket, this);

  default_commands_.emplace_back(std::make_shared<HelpCommand>());
  default_commands_.emplace_back(std::make_shared<CommandsCommand>());
  default_commands_.emplace_back(std::make_shared<SetShipCommand>());
  default_commands_.emplace_back(std::make_shared<InfoCommand>());
  default_commands_.emplace_back(std::make_shared<SayCommand>());
  default_commands_.emplace_back(std::make_shared<GoCommand>());
  default_commands_.emplace_back(std::make_shared<BehaviorCommand>());
  default_commands_.emplace_back(std::make_shared<BehaviorsCommand>());
  default_commands_.emplace_back(std::make_shared<QuitCommand>());
  default_commands_.emplace_back(std::make_shared<ReloadCommand>());

  Reset();
}

void CommandSystem::Reset() {
  commands_.clear();

  for (auto& cmd : default_commands_) {
    RegisterCommand(cmd);
  }
}

int CommandSystem::GetSecurityLevel(const std::string& player) {
  auto iter = operators_.find(Lowercase(player));

  if (iter != operators_.end()) {
    return iter->second;
  }

  return 0;
}

void CommandSystem::OnChatPacket(const u8* pkt, size_t size) {
  ChatType type = (ChatType) * (pkt + 1);
  u16 sender_id = *(u16*)(pkt + 3);
  char* msg_raw = (char*)(pkt + 5);
  size_t msg_size = size - 6;

  std::string_view msg(msg_raw, msg_size);
  std::string_view sender_name;

  // We need to parse the name and message from a remote private message.
  if (type == ChatType::RemotePrivate) {
    size_t name_end = msg.find(")>");
    if (name_end == std::string_view::npos) return;

    sender_name = msg.substr(1, name_end - 1);
    msg = msg.substr(name_end + 2);
  }

  if (msg.empty()) return;
  if (msg[0] != '!' && msg[0] != '.') return;

  msg = msg.substr(1);

  if (sender_id != 0xFFFF) {
    Player* player = bot.game->player_manager.GetPlayerById(sender_id);
    if (!player) return;

    // Ignore self
    if (player->id == bot.game->player_manager.player_id) return;

    sender_name = player->name;
  }

  std::vector<std::string_view> tokens = Tokenize(msg, ';');
  bool executed = false;

  for (std::string_view current_msg : tokens) {
    std::size_t split = current_msg.find(' ');
    std::string trigger = Lowercase(current_msg.substr(0, split));
    std::string arg;

    if (split != std::string::npos) {
      arg = current_msg.substr(split + 1);
    }

    auto iter = commands_.find(trigger);
    if (iter != commands_.end()) {
      CommandExecutor& command = *iter->second;
      u32 access_request = (1 << (u32)type);

      /*
       * bitfield evaluation example (showing 4 bits):
       * chat type is 0000 for arena message
       * request_access evaluates to 0001
       * command access can be 0001 for arena or 0101 for both arena and public
       * 0001 & 0101 = 0001 which evaluates to true or non zero
       */
      if (access_request & command.GetAccess()) {
        int security_level = default_security_level_;

        if (type == ChatType::Arena) {
          security_level = arena_security_level_;
        } else {
          auto op_iter = operators_.find(Lowercase(sender_name));

          if (op_iter != operators_.end()) {
            security_level = op_iter->second;
          }
        }

        if (security_level >= command.GetSecurityLevel()) {
          auto& bb = bot.execute_ctx.blackboard;

          // If the command is lockable, bot is locked, and requester isn't an operator then ignore it.
          if (!(command.GetFlags() & CommandFlag_Lockable) || !bb.ValueOr<bool>("CmdLock", false) ||
              security_level > 0) {
            command.Execute(*this, bot, std::string(sender_name), arg);
            executed = true;
          }
        }
      }
    }
  }

  if (executed && chat_broadcast) {
    std::string broadcast = sender_name.empty() ? "[Arena]" : sender_name.data();
    broadcast += " executed command: ";
    broadcast += std::string_view(msg_raw, msg_size);

    Event::Dispatch(ChatQueueEvent::Channel(1, broadcast.data()));
  }
}

void CommandSystem::LoadSecurityLevels() {
  operators_.clear();

  ConfigGroup operator_group = bot.config->GetOrCreateGroup("Operators");
  for (auto& kv : operator_group.map) {
    auto opt_level = bot.config->GetInt("Operators", kv.first.data());

    if (!opt_level) continue;

    if (std::string_view(kv.first) == std::string_view("*arena*")) {
      arena_security_level_ = *opt_level;
      Log(LogLevel::Info, "Setting arena security level to %d", *opt_level);
    } else if (std::string_view(kv.first) == std::string_view("*default*")) {
      default_security_level_ = *opt_level;
      Log(LogLevel::Info, "Setting default security level to %d", *opt_level);
    } else {
      operators_[Lowercase(kv.first)] = *opt_level;
      Log(LogLevel::Info, "Adding operator %s:%d", kv.first.data(), *opt_level);
    }
  }

  SetDefaultSecurityLevels();

  ConfigGroup command_access_group = bot.config->GetOrCreateGroup("CommandAccess");
  for (auto& kv : command_access_group.map) {
    auto opt_level = bot.config->GetInt("CommandAccess", kv.first.data());

    if (!opt_level) continue;

    Log(LogLevel::Info, "Setting command '%s' security level to %d.", kv.first.data(), *opt_level);
    SetCommandSecurityLevel(kv.first, *opt_level);
  }
}

void CommandSystem::SetDefaultSecurityLevels() {
  SetCommandSecurityLevel("behaviors", 0);
  SetCommandSecurityLevel("behavior", 1);
  SetCommandSecurityLevel("say", 10);
  SetCommandSecurityLevel("go", 5);
  SetCommandSecurityLevel("info", 0);
  SetCommandSecurityLevel("setship", 1);
  SetCommandSecurityLevel("help", 0);
  SetCommandSecurityLevel("commands", 0);
  SetCommandSecurityLevel("quit", 10);
  SetCommandSecurityLevel("reload", 10);
}

void CommandSystem::SetCommandSecurityLevel(const std::string& name, int level) {
  auto iter = commands_.find(Lowercase(name));
  if (iter != commands_.end()) {
    iter->second->SetSecurityLevel(level);
  }
}

// Very simple tokenizer. Doesn't treat quoted strings as one token.
std::vector<std::string_view> Tokenize(std::string_view message, char delim) {
  std::vector<std::string_view> tokens;

  std::size_t pos = 0;

  while ((pos = message.find(delim)) != std::string::npos) {
    // Skip over multiple delims in a row
    if (pos > 0) {
      tokens.push_back(message.substr(0, pos));
    }

    message = message.substr(pos + 1);
  }

  if (!message.empty()) {
    tokens.push_back(message);
  }

  return tokens;
}

}  // namespace zero

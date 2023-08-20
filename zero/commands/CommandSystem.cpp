#include "CommandSystem.h"

#include <zero/ZeroBot.h>
#include <zero/game/Game.h>
#include <zero/game/net/PacketDispatcher.h>

namespace zero {

constexpr int kArenaSecurityLevel = 5;

class SayCommand : public CommandExecutor {
public:
  void Execute(CommandSystem& cmd, ZeroBot& bot, const std::string& sender, const std::string& arg) override {
    const char* kFilters[] = {
      "?password", "?passwd", "?squad"
    };

    for (size_t i = 0; i < ZERO_ARRAY_SIZE(kFilters); ++i) {
      if (arg.find(kFilters[i]) != std::string::npos) {
        Player* player = bot.game->player_manager.GetPlayerByName(sender.c_str());

        if (player) {
          bot.game->chat.SendPrivateMessage("Message filtered.", player->id);
        }

        return;
      }
    }

    bot.game->chat.SendMessage(ChatType::Public, arg.c_str());
  }

  CommandAccessFlags GetAccess() { return CommandAccess_Private; }
  void SetAccess(CommandAccessFlags flags) { return; }
  std::vector<std::string> GetAliases() { return { "say" }; }
  std::string GetDescription() { return "Repeats a message publicly"; }
  int GetSecurityLevel() { return 10; }
};

class ZoneCommand : public CommandExecutor {
 public:
  void Execute(CommandSystem& cmd, ZeroBot& bot, const std::string& sender, const std::string& arg) override {
    if (sender.empty()) return;

    Player* player = bot.game->player_manager.GetPlayerByName(sender.c_str());
    if (!player) return;

    const char* zone_name = to_string(bot.server_info.zone);

    char zone_message[256];
    sprintf(zone_message, "Connected to zone '%s'.", zone_name);

    bot.game->chat.SendPrivateMessage(zone_message, player->id);
  }

  CommandAccessFlags GetAccess() { return CommandAccess_Private; }
  void SetAccess(CommandAccessFlags flags) { return; }
  std::vector<std::string> GetAliases() { return {"zone", "z"}; }
  std::string GetDescription() { return "Prints current zone"; }
  int GetSecurityLevel() { return 0; }
};

class SetShipCommand : public CommandExecutor {
 public:
  void Execute(CommandSystem& cmd, ZeroBot& bot, const std::string& sender, const std::string& arg) override {
    if (sender.empty()) return;

    Player* player = bot.game->player_manager.GetPlayerByName(sender.c_str());
    if (!player) return;

    int parsed_ship_num = 0;

    auto args = Tokenize(arg, ' ');

    if (args.size() != 1 || args[0].empty()) {
      SendUsage(bot, *player);
      return;
    }

    if (!isdigit(args[0][0])) {
      SendUsage(bot, *player);
      return;
    }

    parsed_ship_num = args[0][0] - '0';

    if (parsed_ship_num < 1 || parsed_ship_num > 9) {
      SendUsage(bot, *player);
      return;
    }

    bot.execute_ctx.blackboard.Set("request_ship", parsed_ship_num - 1);
  }

  void SendUsage(ZeroBot& bot, Player& player) {
    bot.game->chat.SendPrivateMessage("Usage: !setship [shipNum 1-9]", player.id);
  }

  CommandAccessFlags GetAccess() { return CommandAccess_Private | CommandAccess_Public; }
  void SetAccess(CommandAccessFlags flags) { return; }
  std::vector<std::string> GetAliases() { return {"setship", "ss"}; }
  std::string GetDescription() { return "Sets the ship"; }
  int GetSecurityLevel() { return 0; }
};

class HelpCommand : public CommandExecutor {
 public:
  void Execute(CommandSystem& cmd, ZeroBot& bot, const std::string& sender, const std::string& arg) override {
    if (sender.empty()) return;

    Player* player = bot.game->player_manager.GetPlayerByName(sender.c_str());
    if (!player) return;

    bot.game->chat.SendPrivateMessage("-- !commands {.c} -- see command list (pm)", player->id);
  }

  CommandAccessFlags GetAccess() { return CommandAccess_Private; }
  void SetAccess(CommandAccessFlags flags) { return; }
  std::vector<std::string> GetAliases() { return {"help", "h"}; }
  std::string GetDescription() { return "Helps"; }
  int GetSecurityLevel() { return 0; }
};

class CommandsCommand : public CommandExecutor {
 public:
  void Execute(CommandSystem& cmd, ZeroBot& bot, const std::string& sender, const std::string& arg) override {
    if (sender.empty()) return;
    Player* player = bot.game->player_manager.GetPlayerByName(sender.c_str());
    if (!player) return;

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

      bot.game->chat.SendPrivateMessage(output.c_str(), player->id);
    }
  }

  CommandAccessFlags GetAccess() { return CommandAccess_Private; }
  void SetAccess(CommandAccessFlags flags) { return; }
  std::vector<std::string> GetAliases() { return {"commands", "c"}; }
  std::string GetDescription() { return "Shows available commands"; }
  int GetSecurityLevel() { return 0; }
};

std::string Lowercase(std::string_view str) {
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

const std::unordered_map<std::string, int> kOperators = {
    {"tm_master", 10}, {"baked cake", 10}, {"x-demo", 10}, {"lyra.", 5}, {"profile", 5}, {"monkey", 10},
    {"neostar", 5},    {"geekgrrl", 5},    {"sed", 5},     {"sk", 5},    {"b.o.x.", 10}};

static void RawOnChatPacket(void* user, u8* pkt, size_t size) {
  CommandSystem* cmd = (CommandSystem*)user;

  cmd->OnChatPacket(pkt, size);
}

CommandSystem::CommandSystem(ZeroBot& bot, PacketDispatcher& dispatcher) : bot(bot) {
  dispatcher.Register(ProtocolS2C::Chat, RawOnChatPacket, this);

  RegisterCommand(std::make_shared<HelpCommand>());
  RegisterCommand(std::make_shared<CommandsCommand>());
  RegisterCommand(std::make_shared<SetShipCommand>());
  RegisterCommand(std::make_shared<ZoneCommand>());
  RegisterCommand(std::make_shared<SayCommand>());
}

int CommandSystem::GetSecurityLevel(const std::string& player) {
  auto iter = kOperators.find(player);

  if (iter != kOperators.end()) {
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

  if (msg.empty()) return;
  if (msg[0] != '!' && msg[0] != '.') return;

  msg = msg.substr(1);

  Player* player = bot.game->player_manager.GetPlayerById(sender_id);

  if (!player) return;

  // Ignore self
  if (player->id == bot.game->player_manager.player_id) return;

  std::vector<std::string_view> tokens = Tokenize(msg, ';');

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
        int security_level = 0;

        if (type == ChatType::Arena) {
          security_level = kArenaSecurityLevel;
        } else {
          auto op_iter = kOperators.find(Lowercase(player->name));

          if (op_iter != kOperators.end()) {
            security_level = op_iter->second;
          }
        }

        if (security_level >= command.GetSecurityLevel()) {
          auto& bb = bot.execute_ctx.blackboard;

          // If the command is lockable, bot is locked, and requester isn't an operator then ignore it.
          if (!(command.GetFlags() & CommandFlag_Lockable) || !bb.ValueOr<bool>("CmdLock", false) ||
              security_level > 0) {
            command.Execute(*this, bot, std::string(player->name), arg);
          }
        }
      }
    }
  }
}

const Operators& CommandSystem::GetOperators() const {
  return kOperators;
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
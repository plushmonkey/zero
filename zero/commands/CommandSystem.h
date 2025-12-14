#pragma once

#include <zero/Types.h>

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace zero {

struct ZeroBot;
struct PacketDispatcher;

// Match bits with the internal chat type numbers to simplify access check
enum {
  CommandAccess_Arena = (1 << 0),
  CommandAccess_PublicMacro = (1 << 1),
  CommandAccess_Public = (1 << 2),
  CommandAccess_Team = (1 << 3),
  CommandAccess_OtherTeam = (1 << 4),
  CommandAccess_Private = (1 << 5),
  CommandAccess_RedWarning = (1 << 6),
  CommandAccess_RemotePrivate = (1 << 7),
  CommandAccess_RedError = (1 << 8),
  CommandAccess_Chat = (1 << 9),
  CommandAccess_Fuchsia = (1 << 10),

  // The common communication channels that we should respond from.
  CommandAccess_Standard = (CommandAccess_Public | CommandAccess_PublicMacro | CommandAccess_Team |
                            CommandAccess_OtherTeam | CommandAccess_Private | CommandAccess_RemotePrivate),

  CommandAccess_All = (CommandAccess_PublicMacro | CommandAccess_Public | CommandAccess_Team | CommandAccess_OtherTeam |
                       CommandAccess_Private | CommandAccess_RemotePrivate | CommandAccess_Chat),
};

typedef u32 CommandAccessFlags;

enum {
  CommandFlag_Lockable = (1 << 0),
};
typedef u32 CommandFlags;

class CommandSystem;

class CommandExecutor {
 public:
  virtual void Execute(CommandSystem& cmd, ZeroBot& bot, const std::string& sender, const std::string& arg) = 0;
  virtual CommandAccessFlags GetAccess() = 0;
  virtual CommandFlags GetFlags() { return CommandFlag_Lockable; }
  // All of the aliases provided here should be lowercase.
  virtual std::vector<std::string> GetAliases() = 0;
  virtual std::string GetDescription() = 0;

  virtual int GetSecurityLevel() const { return security_level; }
  virtual void SetSecurityLevel(int level) { security_level = level; }

 private:
  int security_level = 0;
};

using Operators = std::unordered_map<std::string, int>;
using Commands = std::unordered_map<std::string, std::shared_ptr<CommandExecutor>>;

class CommandSystem {
 public:
  CommandSystem(ZeroBot& bot, PacketDispatcher& dispatcher);

  void Reset();

  void OnChatPacket(const u8* pkt, size_t size);

  void RegisterCommand(std::shared_ptr<CommandExecutor> executor) {
    for (std::string trigger : executor->GetAliases()) {
      commands_[trigger] = executor;
    }
  }

  void UnregisterCommand(const std::string& command_name) {
    auto iter = commands_.find(command_name);
    if (iter == commands_.end()) return;

    std::vector<std::string> aliases = iter->second->GetAliases();
    for (auto& alias : aliases) {
      commands_.erase(alias);
    }
  }

  void SetChatBroadcast(bool enabled) { chat_broadcast = enabled; }

  int GetSecurityLevel(const std::string& player);

  Commands& GetCommands() { return commands_; }
  const Operators& GetOperators() const { return operators_; }

  void SetCommandSecurityLevel(const std::string& name, int level);

  void LoadSecurityLevels();
  void SetDefaultSecurityLevels();

 private:
  Commands commands_;
  Operators operators_;
  ZeroBot& bot;

  int default_security_level_ = 0;
  int arena_security_level_ = 5;

  bool chat_broadcast = false;

  std::vector<std::shared_ptr<CommandExecutor>> default_commands_;
};

std::vector<std::string_view> Tokenize(std::string_view message, char delim);

}  // namespace zero

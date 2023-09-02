#pragma once

#include <zero/Types.h>

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

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
  virtual void SetAccess(CommandAccessFlags flags) = 0;
  virtual CommandFlags GetFlags() { return 0; }
  virtual std::vector<std::string> GetAliases() = 0;
  virtual std::string GetDescription() = 0;
  virtual int GetSecurityLevel() = 0;
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

  int GetSecurityLevel(const std::string& player);

  Commands& GetCommands() { return commands_; }
  const Operators& GetOperators() const;

 private:
  Commands commands_;
  ZeroBot& bot;

  std::vector<std::shared_ptr<CommandExecutor>> default_commands_;
};

std::vector<std::string_view> Tokenize(std::string_view message, char delim);

}  // namespace zero

#pragma once

#include <zero/Args.h>
#include <zero/Config.h>
#include <zero/DebugRenderer.h>
#include <zero/Event.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/commands/CommandSystem.h>
#include <zero/game/Game.h>
#include <zero/game/InputState.h>
#include <zero/game/Memory.h>

#include <memory>
#include <string>

namespace zero {

struct BotController;
struct Worker;
struct WorkQueue;

enum class Zone {
  Local,
  Subgame,
  Hyperspace,
  Devastation,
  MetalGear,
  ExtremeGames,
  TrenchWars,
  Nexus,

  Unknown,
  Count
};

inline const char* to_string(Zone zone) {
  const char* kZoneNames[] = {"Local",        "Subgame",    "Hyperspace", "Devastation", "MetalGear",
                              "ExtremeGames", "TrenchWars", "Nexus",      "Unknown"};

  static_assert(ZERO_ARRAY_SIZE(kZoneNames) == (size_t)Zone::Count);

  if ((size_t)zone >= ZERO_ARRAY_SIZE(kZoneNames)) {
    return kZoneNames[(size_t)Zone::Unknown];
  }

  return kZoneNames[(size_t)zone];
}

struct ServerInfo {
  std::string name;
  std::string ipaddr;
  u16 port;

  Zone zone = Zone::Unknown;
};

struct ZeroBot {
  MemoryArena perm_arena;
  MemoryArena trans_arena;
  MemoryArena work_arena;
  WorkQueue* work_queue;
  Worker* worker;
  Game* game = nullptr;
  DebugRenderer debug_renderer;

  std::unique_ptr<Config> config;
  std::unique_ptr<ArgParser> args;

  ServerInfo server_info = {};

  InputState input;
  BotController* bot_controller = nullptr;

  behavior::ExecuteContext execute_ctx;
  CommandSystem* commands = nullptr;

  char name[32] = {0};
  char password[256] = {0};
  std::string owner;

  ZeroBot();

  bool Initialize(std::unique_ptr<ArgParser> args, const char* name, const char* password);
  bool JoinZone(ServerInfo& server);

  void Run();

  struct JoinRequestEvent : public Event {
    ZeroBot& bot;
    ServerInfo& server;

    JoinRequestEvent(ZeroBot& bot, ServerInfo& server) : bot(bot), server(server) {}
  };
};

}  // namespace zero

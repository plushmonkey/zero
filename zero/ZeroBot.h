#pragma once

#include <zero/Event.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/commands/CommandSystem.h>
#include <zero/game/Game.h>
#include <zero/game/InputState.h>
#include <zero/game/Memory.h>

#include <memory>

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

  Unknown,
  Count
};

inline const char* to_string(Zone zone) {
  const char* kZoneNames[] = {"Local", "Subgame", "Hyperspace", "Devastation", "MetalGear", "ExtremeGames", "Unknown"};

  static_assert(ZERO_ARRAY_SIZE(kZoneNames) == (size_t)Zone::Count);

  if ((size_t)zone >= ZERO_ARRAY_SIZE(kZoneNames)) {
    return kZoneNames[(size_t)Zone::Unknown];
  }

  return kZoneNames[(size_t)zone];
}

struct ServerInfo {
  const char* name;
  const char* ipaddr;
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

  ServerInfo server_info = {};

  InputState input;
  BotController* bot_controller = nullptr;

  behavior::ExecuteContext execute_ctx;
  CommandSystem* commands = nullptr;

  char name[20] = {0};
  char password[256] = {0};

  ZeroBot();

  bool Initialize(const char* name, const char* password);
  bool JoinZone(ServerInfo& server);

  void Run();

  struct JoinRequestEvent : public Event {
    ZeroBot& bot;
    ServerInfo& server;

    JoinRequestEvent(ZeroBot& bot, ServerInfo& server) : bot(bot), server(server) {}
  };
};

}  // namespace zero

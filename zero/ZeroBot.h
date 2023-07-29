#pragma once

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

struct ServerInfo {
  const char* name;
  const char* ipaddr;
  u16 port;
};

struct ZeroBot {
  MemoryArena perm_arena;
  MemoryArena trans_arena;
  MemoryArena work_arena;
  WorkQueue* work_queue;
  Worker* worker;
  Game* game = nullptr;

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
};

}  // namespace zero

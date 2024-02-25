#include "ZeroBot.h"

#include <stdio.h>
#include <zero/BotController.h>
#include <zero/game/Game.h>
#include <zero/game/Logger.h>
#include <zero/game/Settings.h>

#include <chrono>

#define CREATE_RENDER_WINDOW 0
#define SURFACE_WIDTH 1360
#define SURFACE_HEIGHT 768

//

#ifdef _WIN32
#ifdef APIENTRY
#undef APIENTRY
#endif
#include <Windows.h>
#else
#include <memory.h>
#endif

namespace zero {

GameSettings g_Settings;

const char* kPlayerName = "ZeroBot";
const char* kPlayerPassword = "none";

MemoryArena* perm_global = nullptr;

ZeroBot::ZeroBot() : perm_arena(nullptr, 0), trans_arena(nullptr, 0) {}

bool ZeroBot::Initialize(const char* name, const char* password) {
  constexpr size_t kPermanentSize = Megabytes(64);
  constexpr size_t kTransientSize = Megabytes(32);
  constexpr size_t kWorkSize = Megabytes(4);

#ifdef _WIN32
  u8* perm_memory = (u8*)VirtualAlloc(NULL, kPermanentSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  u8* trans_memory = (u8*)VirtualAlloc(NULL, kTransientSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  u8* work_memory = (u8*)VirtualAlloc(NULL, kWorkSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
  u8* perm_memory = (u8*)malloc(kPermanentSize);
  u8* trans_memory = (u8*)malloc(kTransientSize);
  u8* work_memory = (u8*)malloc(kWorkSize);
#endif

  if (!perm_memory || !trans_memory || !work_memory) {
    Log(LogLevel::Error, "Failed to allocate memory.");
    return false;
  }

  perm_arena = MemoryArena(perm_memory, kPermanentSize);
  trans_arena = MemoryArena(trans_memory, kTransientSize);
  work_arena = MemoryArena(work_memory, kWorkSize);

  work_queue = new WorkQueue(work_arena);
  worker = new Worker(*work_queue);
  worker->Launch();

  perm_global = &perm_arena;

  strcpy(this->name, name);
  strcpy(this->password, password);

  g_LogPrintLevel = LogLevel::Info;

  return true;
}

bool ZeroBot::JoinZone(ServerInfo& server) {
  perm_arena.Reset();

  kPlayerName = name;
  kPlayerPassword = password;

  game = memory_arena_construct_type(&perm_arena, Game, perm_arena, trans_arena, *work_queue, SURFACE_WIDTH,
                                     SURFACE_HEIGHT);
  bot_controller = memory_arena_construct_type(&perm_arena, BotController, *game);

  commands = memory_arena_construct_type(&perm_arena, CommandSystem, *this, this->game->dispatcher);

#if CREATE_RENDER_WINDOW
  debug_renderer.Initialize(SURFACE_WIDTH, SURFACE_HEIGHT);
  game->render_enabled = true;
#endif

  GameInitializeResult init_result = game->Initialize(input);
  if (init_result == GameInitializeResult::Failure) {
    Log(LogLevel::Error, "Failed to create game");
    return false;
  }

#if CREATE_RENDER_WINDOW
  if (init_result != GameInitializeResult::Full) {
    debug_renderer.Close();
    game->render_enabled = false;
  }
#endif

  ConnectResult result = game->connection.Connect(server.ipaddr, server.port);

  if (result != ConnectResult::Success) {
    Log(LogLevel::Error, "Failed to connect. Error: %d", (int)result);
    return false;
  }

  game->connection.SendEncryptionRequest(g_Settings.encrypt_method);

  this->server_info = server;

  Event::Dispatch(JoinRequestEvent(*this, server));

  return true;
}

void ZeroBot::Run() {
  constexpr float kMaxDelta = 1.0f / 20.0f;

  // TODO: better timer
  using ms_float = std::chrono::duration<float, std::milli>;
  float frame_time = 0.0f;

  while (true) {
    auto start = std::chrono::high_resolution_clock::now();

    float dt = frame_time / 1000.0f;

    // Cap dt so window movement doesn't cause large updates
    if (dt > kMaxDelta) {
      dt = kMaxDelta;
    }

    Connection::TickResult tick_result = game->connection.Tick();
    if (tick_result != Connection::TickResult::Success) {
      break;
    }

    input.Clear();

    if (game->render_enabled && !debug_renderer.Begin()) {
      game->Cleanup();
      break;
    }

    if (bot_controller && game->connection.login_state == Connection::LoginState::Complete) {
      execute_ctx.bot = this;
      execute_ctx.dt = dt;

      RenderContext rc(&game->camera, &game->ui_camera, &game->sprite_renderer);

      bot_controller->Update(rc, dt, input, execute_ctx);
    }

    if (!game->Update(input, dt)) {
      game->Cleanup();
      break;
    }

    game->Render(dt);

    if (game->render_enabled) {
      debug_renderer.Present();
    }

#if CREATE_RENDER_WINDOW == 0
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif

    auto end = std::chrono::high_resolution_clock::now();
    frame_time = std::chrono::duration_cast<ms_float>(end - start).count();

    trans_arena.Reset();
  }

  if (game && game->connection.connected) {
    game->connection.SendDisconnect();
    Log(LogLevel::Info, "Disconnected from server.");
  }
}

}  // namespace zero

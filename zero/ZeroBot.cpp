#include "ZeroBot.h"

#include <stdio.h>
#include <zero/BotController.h>
#include <zero/game/Game.h>
#include <zero/game/Logger.h>
#include <zero/game/Settings.h>
#include <zero/game/WorkQueue.h>

#include <chrono>

#if 1
#define SURFACE_WIDTH 1152
#define SURFACE_HEIGHT 648
#else
#define SURFACE_WIDTH 1600
#define SURFACE_HEIGHT 900
#endif

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

bool ZeroBot::Initialize(std::unique_ptr<ArgParser> args, const char* name, const char* password) {
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
  this->args = std::move(args);

  g_Settings.vsync = true;

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
  commands->LoadSecurityLevels();

  if (g_Settings.debug_window) {
    if (debug_renderer.Initialize(SURFACE_WIDTH, SURFACE_HEIGHT)) {
      game->render_enabled = true;
      debug_renderer.SetWindowUserPointer(this);
    } else {
      g_Settings.debug_window = false;
    }
  }

  GameInitializeResult init_result = game->Initialize(input);
  if (init_result == GameInitializeResult::Failure) {
    Log(LogLevel::Error, "Failed to create game");
    return false;
  }

  if (g_Settings.debug_window) {
    if (init_result != GameInitializeResult::Full) {
      debug_renderer.Close();
      game->render_enabled = false;
    }
  }

  const char* group_lookups[] = {to_string(server.zone), "General"};
  auto default_arena = config->GetString(group_lookups, ZERO_ARRAY_SIZE(group_lookups), "Arena");

  std::string_view arena_override = args->GetValue({"arena", "a"});
  if (!arena_override.empty()) {
    default_arena = std::make_optional(arena_override.data());
  }

  if (default_arena) {
    bot_controller->default_arena = std::string(*default_arena);
  }

  ConnectResult result = game->connection.Connect(server.ipaddr.data(), server.port);

  if (result != ConnectResult::Success) {
    Log(LogLevel::Error, "Failed to connect. Error: %d", (int)result);
    return false;
  }

  game->connection.SendEncryptionRequest(g_Settings.encrypt_method);

  this->server_info = server;

  Event::Dispatch(JoinRequestEvent(*this, server));

  return true;
}

constexpr float kTickTime = 1.0f / 100.0f;

void ZeroBot::UpdateRelaxed(size_t update_count) {
  if (bot_controller && bot_controller->actuator.enabled) {
    input.Clear();
  } else {
    input.ClearWeapons();
  }

  if (bot_controller && game->connection.login_state == Connection::LoginState::Complete) {
    execute_ctx.bot = this;
    execute_ctx.dt = kTickTime * (float)update_count;

    RenderContext rc(&game->camera, &game->ui_camera, &game->sprite_renderer);

    bot_controller->Update(rc, input, execute_ctx);
  }

  for (size_t i = 0; i < update_count; ++i) {
    if (!game->Update(input, kTickTime)) {
      game->Cleanup();
      break;
    }

    if (i == update_count - 1) {
      game->Render(kTickTime);
    } else {
      game->animation.Update(kTickTime);
      game->sprite_renderer.push_buffer.Reset();
      game->sprite_renderer.texture_push_buffer.Reset();
    }

    game->player_manager.kdtree = nullptr;
    trans_arena.Reset();
  }
}

void ZeroBot::Update(size_t update_count) {
  for (size_t i = 0; i < update_count; ++i) {
    if (bot_controller && bot_controller->actuator.enabled) {
      input.Clear();
    } else {
      input.ClearWeapons();
    }

    if (bot_controller && game->connection.login_state == Connection::LoginState::Complete) {
      execute_ctx.bot = this;
      execute_ctx.dt = kTickTime;

      RenderContext rc(&game->camera, &game->ui_camera, &game->sprite_renderer);

      bot_controller->Update(rc, input, execute_ctx);
    }

    if (!game->Update(input, kTickTime)) {
      game->Cleanup();
      break;
    }

    if (i == update_count - 1) {
      game->Render(kTickTime);
    } else {
      game->animation.Update(kTickTime);
      game->sprite_renderer.push_buffer.Reset();
      game->sprite_renderer.texture_push_buffer.Reset();
    }

    game->player_manager.kdtree = nullptr;
    trans_arena.Reset();
  }
}

void ZeroBot::Run() {
  // TODO: better timer
  using ms_float = std::chrono::duration<float, std::milli>;
  float frame_time = 0.0f;

  execute_ctx.bot = this;

  int sleep_ms = 1;

  auto opt_sleep_ms = this->config->GetInt("General", "SleepMs");
  if (opt_sleep_ms) sleep_ms = *opt_sleep_ms;

  float dt_accumulator = 0.0f;

  bool relaxed_controller_tick = false;

  auto opt_relaxed_controller_tick = this->config->GetInt("General", "RelaxedControllerTick");
  if (opt_relaxed_controller_tick) relaxed_controller_tick = *opt_relaxed_controller_tick;

  while (true) {
    auto start = std::chrono::high_resolution_clock::now();

    dt_accumulator += frame_time / 1000.0f;

    Connection::TickResult tick_result = game->connection.Tick();
    if (tick_result != Connection::TickResult::Success) {
      break;
    }

    if (game->render_enabled && !debug_renderer.Begin()) {
      game->Cleanup();
      break;
    }

    size_t update_count = (size_t)(dt_accumulator / kTickTime);

    if (update_count > 0) {
      dt_accumulator -= (float)update_count * kTickTime;

      if (dt_accumulator < 0.0f) {
        dt_accumulator = 0.0f;
      }

      if (relaxed_controller_tick) {
        this->UpdateRelaxed(update_count);
      } else {
        this->Update(update_count);
      }
    }

    if (game->render_enabled) {
      debug_renderer.Present();
    }

    if (!g_Settings.debug_window && sleep_ms > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }

    auto end = std::chrono::high_resolution_clock::now();
    frame_time = std::chrono::duration_cast<ms_float>(end - start).count();
  }

  if (game && game->connection.connected) {
    game->connection.SendDisconnect();
    Log(LogLevel::Info, "Disconnected from server.");
  }
}

}  // namespace zero

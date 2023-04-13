#define NOMINMAX

#include <stdio.h>
#include <zero/Buffer.h>
#include <zero/Clock.h>
#include <zero/Memory.h>
#include <zero/Settings.h>
#include <zero/WorkQueue.h>

#include <algorithm>

#ifdef _WIN32
#include <Windows.h>
#else
#include <memory.h>
#endif

#include <zero/Game.h>

#include <chrono>

namespace zero {

GameSettings g_Settings;

void InitializeSettings() {
  g_Settings.encrypt_method = EncryptMethod::Continuum;
}

const char* kPlayerName = "null space";
const char* kPlayerPassword = "none";

struct ServerInfo {
  const char* name;
  const char* ipaddr;
  u16 port;
};

ServerInfo kServers[] = {
    {"local", "192.168.0.169", 5000},
    {"subgame", "192.168.0.169", 5002},
    {"SSCE Hyperspace", "162.248.95.143", 5005},
    {"SSCJ Devastation", "69.164.220.203", 7022},
    {"SSCJ MetalGear CTF", "69.164.220.203", 14000},
    {"SSCU Extreme Games", "208.118.63.35", 7900},
};

constexpr size_t kServerIndex = 0;

static_assert(kServerIndex < ZERO_ARRAY_SIZE(kServers), "Bad server index");

const char* kServerName = kServers[kServerIndex].name;
const char* kServerIp = kServers[kServerIndex].ipaddr;
u16 kServerPort = kServers[kServerIndex].port;

MemoryArena* perm_global = nullptr;

struct ZeroBot {
  MemoryArena perm_arena;
  MemoryArena trans_arena;
  MemoryArena work_arena;
  WorkQueue* work_queue;
  Worker* worker;
  Game* game = nullptr;

  InputState input;

  char name[20] = {0};
  char password[256] = {0};

  size_t selected_zone_index = 0;

  ZeroBot() : perm_arena(nullptr, 0), trans_arena(nullptr, 0) {}

  bool Initialize() {
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
      fprintf(stderr, "Failed to allocate memory.\n");
      return false;
    }

    perm_arena = MemoryArena(perm_memory, kPermanentSize);
    trans_arena = MemoryArena(trans_memory, kTransientSize);
    work_arena = MemoryArena(work_memory, kWorkSize);

    work_queue = new WorkQueue(work_arena);
    worker = new Worker(*work_queue);
    worker->Launch();

    perm_global = &perm_arena;

    strcpy(name, kPlayerName);
    strcpy(password, kPlayerPassword);

    return true;
  }

  bool JoinZone(ServerInfo& server) {
    kServerName = server.name;
    kServerIp = server.ipaddr;
    kServerPort = server.port;

    perm_arena.Reset();

    kPlayerName = name;
    kPlayerPassword = password;

    game = memory_arena_construct_type(&perm_arena, Game, perm_arena, trans_arena, *work_queue, 1920, 1080);

    if (!game->Initialize(input)) {
      fprintf(stderr, "Failed to create game\n");
      return false;
    }

    ConnectResult result = game->connection.Connect(kServerIp, kServerPort);

    if (result != ConnectResult::Success) {
      fprintf(stderr, "Failed to connect. Error: %d\n", (int)result);
      return false;
    }

    game->connection.SendEncryptionRequest(g_Settings.encrypt_method);
    return true;
  }

  void Run() {
    constexpr float kMaxDelta = 1.0f / 20.0f;

    // TODO: better timer
    using ms_float = std::chrono::duration<float, std::milli>;
    float frame_time = 0.0f;

    JoinZone(kServers[0]);

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

      if (!game->Update(input, dt)) {
        game->Cleanup();
        break;
      }

      auto end = std::chrono::high_resolution_clock::now();
      frame_time = std::chrono::duration_cast<ms_float>(end - start).count();

      std::this_thread::sleep_for(std::chrono::milliseconds(1));

      trans_arena.Reset();
    }

    if (game && game->connection.connected) {
      game->connection.SendDisconnect();
      printf("Disconnected from server.\n");
    }
  }
};

}  // namespace zero

int main(void) {
  zero::ZeroBot bot;

  zero::InitializeSettings();

  if (!bot.Initialize()) {
    return 1;
  }

  bot.Run();

  return 0;
}

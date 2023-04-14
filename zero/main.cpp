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

const char* kPlayerName = "ZeroBot";
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

  struct ShipEnforcer {
    s32 last_request_tick;
    u8 requested_ship;

    ShipEnforcer() {
      last_request_tick = GetCurrentTick();
      requested_ship = 0;
    }

    void Update(Game& game) {
      constexpr s32 kRequestInterval = 300;

      Player* self = game.player_manager.GetSelf();

      if (!self) return;
      if (self->ship == requested_ship) return;

      s32 current_tick = GetCurrentTick();

      if (TICK_DIFF(current_tick, last_request_tick) >= kRequestInterval) {
        printf("Sending ship request\n");
        game.connection.SendShipRequest(requested_ship);
        last_request_tick = current_tick;
      }
    }
  };

  struct ShipController {
    void Update(Game& game, InputState& input) {
      Player* self = game.player_manager.GetSelf();

      if (!self || self->ship == 8) return;
      if (self->explode_anim_t > 0.0f && self->explode_anim_t < 0.8f) {
        printf("Exploding.\n");
        return;
      }
      float enter_delay = (game.connection.settings.EnterDelay / 100.0f) + 0.8f;
      if (self->enter_delay > 0.0f && self->enter_delay < enter_delay) {
        printf("Respawning.\n");
        return;
      }

      Player* follow_target = game.player_manager.GetPlayerByName("monkey");
      if (!follow_target || follow_target->ship == 8) return;

      if (follow_target->enter_delay > 0.0f && follow_target->enter_delay < enter_delay) {
        printf("Target is dead.\n");
        return;
      }

      Vector2f force = follow_target->position - self->position;

      Vector2f to_target = Normalize(follow_target->position - self->position);
      float weapon_speed = game.connection.settings.ShipSettings[self->ship].BulletSpeed / 16.0f / 10.0f;
      Vector2f shot_velocity = self->velocity + (self->GetHeading() * weapon_speed);
      Vector2f shot_direction = Normalize(shot_velocity);

      // Optimize so the shot direction goes toward the enemy
      Vector2f heading = shot_direction;
      Vector2f steering_direction = heading;

      bool has_force = force.LengthSq() > 0.0f;
      if (has_force) {
        steering_direction = Normalize(force);
      }

      Vector2f rotate_target = steering_direction;
      Vector2f perp = Perpendicular(heading);
      bool behind = force.Dot(heading) < 0;
      bool leftside = steering_direction.Dot(perp) < 0;

      if (steering_direction.Dot(rotate_target) < 0.75) {
        float rotation = 0.1f;
        int sign = leftside ? 1 : -1;
        if (behind) sign *= -1;

        steering_direction = Rotate(rotate_target, rotation * sign);

        leftside = steering_direction.Dot(perp) < 0;
      }

      bool clockwise = !leftside;

      if (has_force) {
        if (behind) {
          input.SetAction(InputAction::Backward, true);
        } else {
          input.SetAction(InputAction::Forward, true);
        }
      }

      if (heading.Dot(steering_direction) < 0.996f) {
        input.SetAction(InputAction::Right, clockwise);
        input.SetAction(InputAction::Left, !clockwise);
      }

      if (to_target.Dot(heading) < 0.0f) return;

      float nearby_radius = game.connection.settings.ShipSettings[follow_target->ship].GetRadius() * 1.25f;
      Vector2f nearest_point = GetClosestLinePoint(self->position, self->position + shot_direction * 100.0f, follow_target->position);

      bool in_safe = game.connection.map.GetTileId(self->position) == kTileSafeId;
      
      if (!in_safe && nearest_point.DistanceSq(follow_target->position) < nearby_radius * nearby_radius) {
        input.SetAction(InputAction::Bullet, true);
      }
    }
  };

  ShipEnforcer ship_enforcer;
  ShipController ship_controller;

  void Update() {
    input.Clear();

    if (game->connection.login_state != Connection::LoginState::Complete) return;

    ship_enforcer.Update(*game);
    ship_controller.Update(*game, input);
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

      Update();

      if (!game->Update(input, dt)) {
        game->Cleanup();
        break;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(1));

      auto end = std::chrono::high_resolution_clock::now();
      frame_time = std::chrono::duration_cast<ms_float>(end - start).count();

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

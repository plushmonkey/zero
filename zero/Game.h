#ifndef ZERO_GAME_H_
#define ZERO_GAME_H_

#include <zero/BrickManager.h>
#include <zero/Camera.h>
#include <zero/ChatController.h>
#include <zero/InputState.h>
#include <zero/Memory.h>
#include <zero/PlayerManager.h>
#include <zero/Radar.h>
#include <zero/Settings.h>
#include <zero/ShipController.h>
#include <zero/Soccer.h>
#include <zero/WeaponManager.h>
#include <zero/net/Connection.h>
#include <zero/net/PacketDispatcher.h>

namespace zero {

enum {
  GameFlag_Dropped = (1 << 0),
  GameFlag_Turf = (1 << 1),
};

struct GameFlag {
  u16 id = 0xFFFF;
  u16 owner = 0xFFFF;
  u32 hidden_end_tick = 0;
  Vector2f position;

  u32 flags = 0;
  u32 last_pickup_request_tick = 0;
};
constexpr u32 kFlagPickupDelay = 20;

// This is the actual max green count in Continuum
constexpr size_t kMaxGreenCount = 256;
struct PrizeGreen {
  Vector2f position;
  u32 end_tick;
  s32 prize_id;
};

struct Game {
  MemoryArena& perm_arena;
  MemoryArena& temp_arena;
  WorkQueue& work_queue;
  PacketDispatcher dispatcher;
  Connection connection;
  PlayerManager player_manager;
  WeaponManager weapon_manager;
  BrickManager brick_manager;
  Camera camera;
  ChatController chat;
  Soccer soccer;
  ShipController ship_controller;
  Radar radar;
  float fps;
  int mapzoom = 0;
  float jitter_time = 0.0f;
  u32 last_tick = 0;

  size_t flag_count = 0;
  GameFlag flags[256];

  // Current max green count is min((PrizeFactor * player_count) / 1000, 256)
  size_t green_count = 0;
  PrizeGreen greens[kMaxGreenCount];
  u32 green_ticks = 0;
  u32 last_green_tick = 0;
  u32 last_green_collision_tick = 0;

  Game(MemoryArena& perm_arena, MemoryArena& temp_arena, WorkQueue& work_queue, int width, int height);

  bool Initialize(InputState& input);
  void Cleanup();

  bool Update(const InputState& input, float dt);
  void UpdateGreens(float dt);
  void SpawnDeathGreen(const Vector2f& position, Prize prize);

  void Render(float dt);

  void RenderGame(float dt);
  void RenderJoin(float dt);

  void RenderMenu();
  bool HandleMenuKey(int codepoint, int mods);

  void RecreateRadar();

  void OnFlagClaim(u8* pkt, size_t size);
  void OnFlagPosition(u8* pkt, size_t size);
  void OnTurfFlagUpdate(u8* pkt, size_t size);
  void OnPlayerId(u8* pkt, size_t size);
};

}  // namespace zero

#endif

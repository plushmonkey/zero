#ifndef ZERO_GAME_H_
#define ZERO_GAME_H_

#include <zero/game/BrickManager.h>
#include <zero/game/Camera.h>
#include <zero/game/ChatController.h>
#include <zero/game/InputState.h>
#include <zero/game/Memory.h>
#include <zero/game/PlayerManager.h>
#include <zero/game/Radar.h>
#include <zero/game/Settings.h>
#include <zero/game/ShipController.h>
#include <zero/game/Soccer.h>
#include <zero/game/WeaponManager.h>
#include <zero/game/net/Connection.h>
#include <zero/game/net/PacketDispatcher.h>
#include <zero/game/render/AnimatedTileRenderer.h>
#include <zero/game/render/LineRenderer.h>
#include <zero/game/render/SpriteRenderer.h>
#include <zero/game/render/TileRenderer.h>

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

enum class GameInitializeResult {
  // The game failed to start completely and can't even be simulated.
  Failure,
  // The game is loaded enough to do simulation but not render.
  Simulation,
  // The game can do full simulation and rendering.
  Full
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

  bool render_enabled = false;
  Camera ui_camera;
  AnimationSystem animation;
  TileRenderer tile_renderer;
  AnimatedTileRenderer animated_tile_renderer;
  SpriteRenderer sprite_renderer;
  LineRenderer line_renderer;

  char arena_name[17] = {};

  size_t flag_count = 0;
  GameFlag flags[256];

  // Current max green count is min((PrizeFactor * player_count) / 1000, 256)
  size_t green_count = 0;
  PrizeGreen greens[kMaxGreenCount];
  u32 green_ticks = 0;
  u32 last_green_tick = 0;
  u32 last_green_collision_tick = 0;

  Game(MemoryArena& perm_arena, MemoryArena& temp_arena, WorkQueue& work_queue, int width, int height);

  GameInitializeResult Initialize(InputState& input);
  void Cleanup();

  bool Update(const InputState& input, float dt);
  void UpdateGreens(float dt);
  void SpawnDeathGreen(const Vector2f& position, Prize prize);

  void Render(float dt);

  void RenderGame(float dt);
  void RenderJoin(float dt);

  void RenderMenu();

  void RecreateRadar();

  void OnFlagClaim(u8* pkt, size_t size);
  void OnFlagPosition(u8* pkt, size_t size);
  void OnFlagVictory(u8* pkt, size_t size);
  void OnFlagDrop(u8* pkt, size_t size);
  void OnTurfFlagUpdate(u8* pkt, size_t size);
  void OnPlayerId(u8* pkt, size_t size);

  Map& GetMap() { return connection.map; }
  const Map& GetMap() const { return connection.map; }
};

}  // namespace zero

#endif

#ifndef ZERO_PLAYER_MANAGER_H_
#define ZERO_PLAYER_MANAGER_H_

#include <zero/Types.h>
#include <zero/game/Player.h>
#include <zero/game/net/Connection.h>
#include <zero/game/render/Animation.h>
#include <zero/game/render/Graphics.h>

namespace zero {

struct Camera;
struct ChatController;
struct InputState;
struct PacketDispatcher;
struct Radar;
struct ShipController;
struct Soccer;
struct SpriteRenderer;
struct WeaponManager;

enum class AttachRequestResponse {
  Success,
  DetatchFromParent,
  DetatchChildren,

  NoDestination,
  CarryingBall,
  NotEnoughEnergy,
  BountyTooLow,
  Self,
  Frequency,
  Spectator,
  TargetShipNotAttachable,
  TooManyTurrets,
  Antiwarped,

  UnrecoverableError
};

struct PlayerManager {
  MemoryArena& perm_arena;
  Connection& connection;
  WeaponManager* weapon_manager = nullptr;
  ShipController* ship_controller = nullptr;
  ChatController* chat_controller = nullptr;
  Soccer* soccer = nullptr;
  Radar* radar = nullptr;

  u16 player_id = 0;
  // This is in server time
  s32 last_position_tick = 0;
  bool received_initial_list = false;

  AttachInfo* attach_free = nullptr;

  Animation explode_animation;
  Animation warp_animation;
  Animation bombflash_animation;

  size_t player_count = 0;
  Player players[1024];

  // Indirection table to look up player by id quickly
  u16 player_lookup[65536];

  PlayerManager(MemoryArena& perm_arena, Connection& connection, PacketDispatcher& dispatcher);

  inline void Initialize(WeaponManager* weapon_manager, ShipController* ship_controller,
                         ChatController* chat_controller, Radar* radar, Soccer* soccer) {
    this->weapon_manager = weapon_manager;
    this->ship_controller = ship_controller;
    this->chat_controller = chat_controller;
    this->radar = radar;
    this->soccer = soccer;

    warp_animation.sprite = &Graphics::anim_ship_warp;
    explode_animation.sprite = &Graphics::anim_ship_explode;
    bombflash_animation.sprite = &Graphics::anim_bombflash;
  }

  void Update(float dt);
  void Render(Camera& camera, SpriteRenderer& renderer);

  void RenderPlayerName(Camera& camera, SpriteRenderer& renderer, Player& self, Player& player,
    const Vector2f& position, bool is_decoy);

  void Spawn(bool reset = true);

  Player* GetSelf();
  Player* GetPlayerById(u16 id, size_t* index = nullptr);
  Player* GetPlayerByName(const char* name);

  inline u16 GetPlayerIndex(u16 id) { return player_lookup[id]; }

  void SendPositionPacket();
  void SimulatePlayer(Player& player, float dt, bool extrapolating);
  bool SimulateAxis(Player& player, float dt, int axis, bool extrapolating);

  void OnPlayerIdChange(u8* pkt, size_t size);
  void OnPlayerEnter(u8* pkt, size_t size);
  void OnPlayerLeave(u8* pkt, size_t size);
  void OnPlayerDeath(u8* pkt, size_t size);
  void OnPlayerFreqAndShipChange(u8* pkt, size_t size);
  void OnPlayerFrequencyChange(u8* pkt, size_t size);
  void OnLargePositionPacket(u8* pkt, size_t size);
  void OnBatchedLargePositionPacket(u8* pkt, size_t size);
  void OnSmallPositionPacket(u8* pkt, size_t size);
  void OnBatchedSmallPositionPacket(u8* pkt, size_t size);
  void OnFlagDrop(u8* pkt, size_t size);
  void OnCreateTurretLink(u8* pkt, size_t size);
  void OnDestroyTurretLink(u8* pkt, size_t size);

  void OnPositionPacket(Player& player, const Vector2f& position, const Vector2f& velocity, s32 sim_ticks);

  AttachRequestResponse AttachSelf(Player* destination);
  void AttachPlayer(Player& requester, Player& destination);
  void DetachPlayer(Player& player);
  void DetachAllChildren(Player& player);
  size_t GetTurretCount(Player& player);

  bool IsAntiwarped(Player& self, bool notify);

  inline bool IsSynchronized(Player& player) {
    u16 tick = (GetCurrentTick() + connection.time_diff) & 0x7FFF;
    return player.id == player_id || SMALL_TICK_DIFF(tick, player.timestamp) < kPlayerTimeout;
  }
};

}  // namespace zero

#endif

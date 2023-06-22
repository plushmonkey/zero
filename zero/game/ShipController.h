#ifndef ZERO_SHIPCONTROLLER_H_
#define ZERO_SHIPCONTROLLER_H_

#include <zero/Types.h>
#include <zero/game/Player.h>

namespace zero {

struct InputState;
struct PacketDispatcher;
struct Player;
struct PlayerManager;
struct Weapon;
struct WeaponManager;

enum class Prize {
  None,
  Recharge,
  Energy,
  Rotation,
  Stealth,
  Cloak,
  XRadar,
  Warp,
  Guns,
  Bombs,
  BouncingBullets,
  Thruster,
  TopSpeed,
  FullCharge,
  EngineShutdown,
  Multifire,
  Proximity,
  Super,
  Shields,
  Shrapnel,
  Antiwarp,
  Repel,
  Burst,
  Decoy,
  Thor,
  Multiprize,
  Brick,
  Rocket,
  Portal,

  Count
};

enum {
  ShipCapability_Stealth = (1 << 0),
  ShipCapability_Cloak = (1 << 1),
  ShipCapability_XRadar = (1 << 2),
  ShipCapability_Antiwarp = (1 << 3),
  ShipCapability_Multifire = (1 << 4),
  ShipCapability_Proximity = (1 << 5),
  ShipCapability_BouncingBullets = (1 << 6),
};
using ShipCapabilityFlags = int;

struct Ship {
  u32 energy;
  u32 recharge;
  u32 rotation;
  u32 guns;
  u32 bombs;
  u32 thrust;
  u32 speed;
  u32 shrapnel;

  u32 repels;
  u32 bursts;
  u32 decoys;
  u32 thors;
  u32 bricks;
  u32 rockets;
  u32 portals;

  u32 next_bullet_tick = 0;
  u32 next_bomb_tick = 0;
  u32 next_repel_tick = 0;

  u32 rocket_end_tick;
  u32 shutdown_end_tick;
  u32 fake_antiwarp_end_tick;

  float emped_time;
  float super_time;
  float shield_time;

  float portal_time;
  Vector2f portal_location;

  bool multifire;

  ShipCapabilityFlags capability;
};

typedef void (*OnBombDamage)(void* user);

struct BombExplosionReport {
  OnBombDamage on_damage = nullptr;
  void* user = nullptr;
};

struct ShipController {
  PlayerManager& player_manager;
  WeaponManager& weapon_manager;
  Ship ship;

  BombExplosionReport explosion_report;

  // Only activate warps/portals on key press
  bool portal_input_cleared = true;
  bool warp_input_cleared = true;

  ShipController(PlayerManager& player_manager, WeaponManager& weapon_manager, PacketDispatcher& dispatcher);

  void Update(const InputState& input, float dt);

  void FireWeapons(Player& self, const InputState& input, float dt, bool afterburners);
  void AddBombDelay(u32 tick_amount);
  void AddBulletDelay(u32 tick_amount);

  void HandleStatusEnergy(Player& self, u32 status, u32 cost, float dt);

  size_t GetGunIconIndex();
  size_t GetBombIconIndex();

  void OnCollectedPrize(u8* pkt, size_t size);

  void ResetShip();
  void UpdateSettings();

  void ApplyPrize(Player* self, s32 prize_id, bool notify, bool damage = false);
  s32 GeneratePrize(bool negative_allowed);

  void OnWeaponHit(Weapon& weapon);
};

}  // namespace zero

#endif

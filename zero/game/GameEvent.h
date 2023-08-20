#pragma once

#include <zero/Event.h>
#include <zero/game/Game.h>

namespace zero {

struct C2SPacketEvent : public Event {
  u8* data;
  size_t size;

  C2SPacketEvent(u8* data, size_t size) : data(data), size(size) {}
};

struct S2CPacketEvent : public Event {
  u8* data;
  size_t size;

  S2CPacketEvent(u8* data, size_t size) : data(data), size(size) {}
};

struct DisconnectEvent : public Event {};
struct JoinGameEvent : public Event {};

struct ArenaNameEvent : public Event {
  const char* name;

  ArenaNameEvent(const char* name) : name(name) {}
};

struct ArenaSettingsEvent : public Event {
  ArenaSettings& settings;

  ArenaSettingsEvent(ArenaSettings& settings) : settings(settings) {}
};

struct MapLoadEvent : public Event {
  Map& map;
  MapLoadEvent(Map& map) : map(map) {}
};

struct ChatEvent : public Event {
  ChatType type;
  char* sender;
  char* message;

  ChatEvent(ChatType type, char* sender, char* message) : type(type), sender(sender), message(message) {}
};

struct BrickTileEvent : public Event {
  Brick& brick;

  BrickTileEvent(Brick& brick) : brick(brick) {}
};

struct BrickTileClearEvent : public Event {
  Brick& brick;

  BrickTileClearEvent(Brick& brick) : brick(brick) {}
};

struct PlayerEnterEvent : public Event {
  Player& player;

  PlayerEnterEvent(Player& player) : player(player) {}
};

struct PlayerLeaveEvent : public Event {
  Player& player;

  PlayerLeaveEvent(Player& player) : player(player) {}
};

struct PlayerDeathEvent : public Event {
  Player& player;
  Player& killer;
  u16 bounty;
  u16 flags;

  PlayerDeathEvent(Player& player, Player& killer, u16 bounty, u16 flags)
      : player(player), killer(killer), bounty(bounty), flags(flags) {}
};

struct SpawnEvent : public Event {
  Player& self;

  SpawnEvent(Player& self) : self(self) {}
};

struct PlayerFreqChangeEvent : public Event {
  Player& player;
  u16 old_frequency;
  u16 new_frequency;

  PlayerFreqChangeEvent(Player& player, u16 old_freq, u16 new_freq)
      : player(player), old_frequency(old_freq), new_frequency(new_freq) {}
};

struct PlayerFreqAndShipChangeEvent : public Event {
  Player& player;
  u16 old_frequency;
  u16 new_frequency;
  u8 old_ship;
  u8 new_ship;

  PlayerFreqAndShipChangeEvent(Player& player, u16 old_freq, u16 new_freq, u8 old_ship, u8 new_ship)
      : player(player), old_frequency(old_freq), new_frequency(new_freq), old_ship(old_ship), new_ship(new_ship) {}
};

struct PlayerAttachEvent : public Event {
  Player& attacher;
  Player& destination;

  PlayerAttachEvent(Player& attacher, Player& destination) : attacher(attacher), destination(destination) {}
};

struct PlayerDetachEvent : public Event {
  Player& player;
  Player& parent;

  PlayerDetachEvent(Player& player, Player& parent) : player(player), parent(parent) {}
};

// Dispatched when emp goes away.
struct EmpLossEvent : public Event {};

// Dispatched when ship goes from below full energy to full energy.
struct FullEnergyEvent : public Event {};

struct SafeEnterEvent : public Event {
  Vector2f position;

  SafeEnterEvent(Vector2f position) : position(position) {}
};

struct SafeLeaveEvent : public Event {
  Vector2f position;

  SafeLeaveEvent(Vector2f position) : position(position) {}
};

struct PortalDropEvent : public Event {
  Vector2f position;
  float duration;

  PortalDropEvent(Vector2f position, float duration) : position(position), duration(duration) {}
};

struct PortalWarpEvent : public Event {
  Vector2f from;
  Vector2f to;

  PortalWarpEvent(Vector2f from, Vector2f to) : from(from), to(to) {}
};

struct SpawnWarpEvent : public Event {
  Vector2f from;
  Vector2f to;

  SpawnWarpEvent(Vector2f from, Vector2f to) : from(from), to(to) {}
};

struct PrizeEvent : public Event {
  Prize prize;
  bool negative;

  PrizeEvent(Prize prize, bool negative) : prize(prize), negative(negative) {}
};

struct ShipResetEvent : public Event {};

struct WeaponDamageEvent : public Event {
  Weapon& weapon;
  Player& shooter;
  int damage;
  bool death;

  WeaponDamageEvent(Weapon& weapon, Player& shooter, int damage, bool death)
      : weapon(weapon), shooter(shooter), damage(damage), death(death) {}
};

struct BallTimeoutEvent : public Event {
  Powerball& ball;
  Vector2f position;
  Vector2f velocity;

  BallTimeoutEvent(Powerball& ball, Vector2f position, Vector2f velocity)
      : ball(ball), position(position), velocity(velocity) {}
};

struct BallRequestPickupEvent : public Event {
  Powerball& ball;

  BallRequestPickupEvent(Powerball& ball) : ball(ball) {}
};

struct BallFireEvent : public Event {
  Powerball& ball;

  // Player can be null if this was an old fire event.
  Player* player;
  Vector2f position;
  Vector2f velocity;

  BallFireEvent(Powerball& ball, Player* player, Vector2f position, Vector2f velocity)
      : ball(ball), player(player), position(position), velocity(velocity) {}
};

struct BallPickupEvent : public Event {
  Powerball& ball;
  Player& carrier;

  BallPickupEvent(Powerball& ball, Player& carrier) : ball(ball), carrier(carrier) {}
};

struct BallGoalEvent : public Event {
  Powerball& ball;

  BallGoalEvent(Powerball& ball) : ball(ball) {}
};

struct WeaponSpawnEvent : public Event {
  Weapon& weapon;
  Player& player;

  WeaponSpawnEvent(Weapon& weapon, Player& player) : weapon(weapon), player(player) {}
};

struct WeaponDestroyEvent : public Event {
  Weapon& weapon;

  WeaponDestroyEvent(Weapon& weapon) : weapon(weapon) {}
};

struct ShipToggleEvent : public Event {
  ShipCapabilityFlags capability;
  bool enabled;

  ShipToggleEvent(ShipCapabilityFlags capability, bool enabled) : capability(capability), enabled(enabled) {}
};

struct FlagPickupRequestEvent : public Event {
  GameFlag& flag;

  FlagPickupRequestEvent(GameFlag& flag) : flag(flag) {}
};

struct FlagTimeoutEvent : public Event {};

struct FlagPickupEvent : public Event {
  GameFlag& flag;
  Player& player;

  FlagPickupEvent(GameFlag& flag, Player& player) : flag(flag), player(player) {}
};

struct FlagTurfClaimEvent : public Event {
  GameFlag& flag;
  Player& player;

  FlagTurfClaimEvent(GameFlag& flag, Player& player) : flag(flag), player(player) {}
};

struct FlagTurfUpdateEvent : public Event {
  GameFlag& flag;

  FlagTurfUpdateEvent(GameFlag& flag) : flag(flag) {}
};

struct FlagSpawnEvent : public Event {
  GameFlag& flag;

  FlagSpawnEvent(GameFlag& flag) : flag(flag) {}
};

struct GreenPickupEvent : public Event {
  PrizeGreen& green;

  GreenPickupEvent(PrizeGreen& green) : green(green) {}
};

}  // namespace zero

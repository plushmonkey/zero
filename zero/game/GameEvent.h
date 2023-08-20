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

// TODO: ShipController, Soccer, WeaponManager, and Game

}  // namespace zero

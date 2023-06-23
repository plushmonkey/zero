#include "WeaponManager.h"

#include <assert.h>
#include <math.h>
#include <zero/game/Buffer.h>
#include <zero/game/Camera.h>
#include <zero/game/Clock.h>
#include <zero/game/PlayerManager.h>
#include <zero/game/Radar.h>
#include <zero/game/ShipController.h>
#include <zero/game/net/Connection.h>
#include <zero/game/net/PacketDispatcher.h>

#include <chrono>

// TODO: Spatial partition acceleration structures

namespace zero {

static void OnLargePositionPkt(void* user, u8* pkt, size_t size) {
  WeaponManager* manager = (WeaponManager*)user;

  manager->OnWeaponPacket(pkt, size);
}

WeaponManager::WeaponManager(MemoryArena& temp_arena, Connection& connection, PlayerManager& player_manager,
                             PacketDispatcher& dispatcher)
    : temp_arena(temp_arena), connection(connection), player_manager(player_manager) {
  dispatcher.Register(ProtocolS2C::LargePosition, OnLargePositionPkt, this);
}

void WeaponManager::Update(float dt) {
  u32 tick = GetCurrentTick();

  link_removal_count = 0;

  for (size_t i = 0; i < weapon_count; ++i) {
    Weapon* weapon = weapons + i;

    Player* player = player_manager.GetPlayerById(weapon->player_id);

    if (player && connection.map.GetTileId(player->position) == kTileSafeId) {
      weapons[i--] = weapons[--weapon_count];
      continue;
    }

    s32 tick_count = TICK_DIFF(tick, weapon->last_tick);

    for (s32 j = 0; j < tick_count; ++j) {
      WeaponSimulateResult result = WeaponSimulateResult::Continue;

      result = Simulate(*weapon);

      if (result != WeaponSimulateResult::Continue && weapon->link_id != kInvalidLink) {
        AddLinkRemoval(weapon->link_id, result);
      }

      if (result == WeaponSimulateResult::PlayerExplosion || result == WeaponSimulateResult::WallExplosion) {
        CreateExplosion(*weapon);
        weapons[i--] = weapons[--weapon_count];
        break;
      } else if (result == WeaponSimulateResult::TimedOut) {
        weapons[i--] = weapons[--weapon_count];
        break;
      }
    }
  }

  if (link_removal_count > 0) {
    for (size_t i = 0; i < weapon_count; ++i) {
      Weapon* weapon = weapons + i;

      if (weapon->link_id != kInvalidLink) {
        bool removed = false;

        for (size_t j = 0; j < link_removal_count; ++j) {
          WeaponLinkRemoval* removal = link_removals + j;

          if (removal->link_id == weapon->link_id) {
            if (removal->result == WeaponSimulateResult::PlayerExplosion) {
              CreateExplosion(*weapon);
              removed = true;
            }
            break;
          }
        }

        if (removed) {
          assert(weapon_count > 0);
          weapons[i--] = weapons[--weapon_count];
        }
      }
    }
  }
}

bool WeaponManager::SimulateWormholeGravity(Weapon& weapon) {
  AnimatedTileSet& wormholes = connection.map.GetAnimatedTileSet(AnimatedTile::Wormhole);

  Player* player = player_manager.GetPlayerById(weapon.player_id);
  bool affected = false;

  if (player) {
    s16 gravity = connection.settings.ShipSettings[player->ship].Gravity;

    for (size_t i = 0; i < wormholes.count; ++i) {
      u16 p_x = (u16)weapon.position.x * 16;
      u16 p_y = (u16)weapon.position.y * 16;
      u16 wh_x = wormholes.tiles[i].x * 16;
      u16 wh_y = wormholes.tiles[i].y * 16;

      s16 dx = (p_x - wh_x);
      s16 dy = (p_y - wh_y);

      int dist_sq = (dx * dx) + (dy * dy) + 1;

      if (dist_sq < abs(gravity) * 1000) {
        int gravity_thrust = (gravity * 1000) / dist_sq;

        Vector2f position((float)wormholes.tiles[i].x, (float)wormholes.tiles[i].y);
        Vector2f direction = Normalize(position - weapon.position);

        float per_second = (gravity_thrust * 10.0f / 16.0f);

        weapon.velocity += direction * (per_second / 100.0f);
        affected = true;
      }
    }
  }

  return affected;
}

WeaponSimulateResult WeaponManager::Simulate(Weapon& weapon) {
  WeaponType type = weapon.data.type;

  if (weapon.last_tick++ >= weapon.end_tick) return WeaponSimulateResult::TimedOut;

  if (type == WeaponType::Repel) {
    return SimulateRepel(weapon);
  }

  bool gravity_effect = false;

  if (connection.settings.GravityBombs && (type == WeaponType::Bomb || type == WeaponType::ProximityBomb)) {
    gravity_effect = SimulateWormholeGravity(weapon);
  }

  Vector2f previous_position = weapon.position;

  WeaponSimulateResult position_result = SimulatePosition(weapon);

  if (gravity_effect) {
    weapon.last_event_position = weapon.position;
    weapon.last_event_time = GetMicrosecondTick();
  }

  if (position_result != WeaponSimulateResult::Continue) {
    return position_result;
  }

  if (type == WeaponType::Decoy) return WeaponSimulateResult::Continue;

  bool is_bomb = weapon.data.type == WeaponType::Bomb || weapon.data.type == WeaponType::ProximityBomb ||
                 weapon.data.type == WeaponType::Thor;

  bool is_prox = weapon.data.type == WeaponType::ProximityBomb || weapon.data.type == WeaponType::Thor;

  if (is_prox && weapon.prox_hit_player_id != 0xFFFF) {
    Player* hit_player = player_manager.GetPlayerById(weapon.prox_hit_player_id);

    if (!hit_player) {
      return WeaponSimulateResult::PlayerExplosion;
    }

    float dx = abs(weapon.position.x - hit_player->position.x);
    float dy = abs(weapon.position.y - hit_player->position.y);

    float highest = dx > dy ? dx : dy;

    if (highest > weapon.prox_highest_offset || GetCurrentTick() >= weapon.sensor_end_tick) {
      if (ship_controller) {
        ship_controller->OnWeaponHit(weapon);
      }

      weapon.position = previous_position;

      return WeaponSimulateResult::PlayerExplosion;
    } else {
      weapon.prox_highest_offset = highest;
    }

    return WeaponSimulateResult::Continue;
  }

  if (type == WeaponType::Burst && !(weapon.flags & WEAPON_FLAG_BURST_ACTIVE)) {
    return WeaponSimulateResult::Continue;
  }

  WeaponSimulateResult result = WeaponSimulateResult::Continue;

  for (size_t i = 0; i < player_manager.player_count; ++i) {
    Player* player = player_manager.players + i;

    if (player->ship == 8) continue;
    if (player->frequency == weapon.frequency) continue;
    if (player->enter_delay > 0) continue;
    if (!player_manager.IsSynchronized(*player)) continue;

    float radius = connection.settings.ShipSettings[player->ship].GetRadius();
    Vector2f player_r(radius, radius);
    Vector2f& pos = player->position;

    float weapon_radius = 18.0f;

    if (is_prox) {
      float prox = (float)(connection.settings.ProximityDistance + weapon.data.level);

      if (weapon.data.type == WeaponType::Thor) {
        prox += 3;
      }

      weapon_radius = prox * 18.0f;
    }

    weapon_radius = (weapon_radius - 14.0f) / 16.0f;

    Vector2f min_w(weapon.position.x - weapon_radius, weapon.position.y - weapon_radius);
    Vector2f max_w(weapon.position.x + weapon_radius, weapon.position.y + weapon_radius);

    if (BoxBoxOverlap(pos - player_r, pos + player_r, min_w, max_w)) {
      bool hit = true;

      if (is_prox) {
        weapon.prox_hit_player_id = player->id;
        weapon.sensor_end_tick = GetCurrentTick() + connection.settings.BombExplodeDelay;

        float dx = abs(weapon.position.x - player->position.x);
        float dy = abs(weapon.position.y - player->position.y);

        if (dx > dy) {
          weapon.prox_highest_offset = dx;
        } else {
          weapon.prox_highest_offset = dy;
        }

        weapon_radius = 4.0f / 16.0f;

        min_w = Vector2f(weapon.position.x - weapon_radius, weapon.position.y - weapon_radius);
        max_w = Vector2f(weapon.position.x + weapon_radius, weapon.position.y + weapon_radius);

        // Fully trigger the bomb if it hits the player's normal radius check
        hit = BoxBoxOverlap(pos - player_r, pos + player_r, min_w, max_w);

        if (!hit) {
          continue;
        }
      }

      if (hit && (is_bomb || player->id == player_manager.player_id) && !HasLinkRemoved(weapon.link_id)) {
        if (ship_controller) {
          ship_controller->OnWeaponHit(weapon);
        }
      }

      // Move the position back so shrap spawns correctly
      if (type == WeaponType::Bomb || type == WeaponType::ProximityBomb) {
        weapon.position = previous_position;
      }

      result = WeaponSimulateResult::PlayerExplosion;
    }
  }

  return result;
}

WeaponSimulateResult WeaponManager::SimulateRepel(Weapon& weapon) {
  float effect_radius = connection.settings.RepelDistance / 16.0f;
  float speed = connection.settings.RepelSpeed / 16.0f / 10.0f;

  Vector2f rect_min = weapon.position - Vector2f(effect_radius, effect_radius);
  Vector2f rect_max = weapon.position + Vector2f(effect_radius, effect_radius);

  for (size_t i = 0; i < weapon_count; ++i) {
    Weapon& other = weapons[i];

    if (other.frequency == weapon.frequency) continue;
    if (other.data.type == WeaponType::Repel) continue;

    if (PointInsideBox(rect_min, rect_max, other.position)) {
      Vector2f direction = Normalize(other.position - weapon.position);

      other.velocity = direction * speed;
      other.last_event_time = GetMicrosecondTick();
      other.last_event_position = other.position;

      WeaponType type = other.data.type;

      if (other.data.alternate && (type == WeaponType::Bomb || type == WeaponType::ProximityBomb)) {
        other.data.alternate = 0;
      }

      // Set end tick after turning mines into bombs so the new end tick is bomb time
      other.end_tick = GetCurrentTick() + GetWeaponTotalAliveTime(other.data.type, other.data.alternate);
    }
  }

  for (size_t i = 0; i < player_manager.player_count; ++i) {
    Player& player = player_manager.players[i];

    if (player.frequency == weapon.frequency) continue;
    if (player.ship >= 8) continue;
    if (player.enter_delay > 0.0f) continue;
    if (!player_manager.IsSynchronized(player)) continue;

    if (PointInsideBox(rect_min, rect_max, player.position)) {
      if (connection.map.GetTileId(player.position) != kTileSafeId) {
        player.last_repel_timestamp = GetCurrentTick();

        if (player.id == player_manager.player_id) {
          Vector2f direction = Normalize(player.position - weapon.position);
          player.velocity = direction * speed;
        }
      }
    }
  }

  return WeaponSimulateResult::Continue;
}

bool WeaponManager::SimulateAxis(Weapon& weapon, float dt, int axis) {
  float previous = weapon.position[axis];
  Map& map = connection.map;

  weapon.position[axis] += weapon.velocity[axis] * dt;

  if (weapon.data.type == WeaponType::Thor) return false;

  // TODO: Handle other special tiles here
  if (map.IsSolid((u16)floorf(weapon.position.x), (u16)floorf(weapon.position.y), weapon.frequency)) {
    weapon.position[axis] = previous;
    weapon.velocity[axis] = -weapon.velocity[axis];

    return true;
  }

  return false;
}

WeaponSimulateResult WeaponManager::SimulatePosition(Weapon& weapon) {
  WeaponType type = weapon.data.type;

  // This collision method deviates from Continuum when using variable update rate, so it updates by one tick at a time
  bool x_collide = SimulateAxis(weapon, 1.0f / 100.0f, 0);
  bool y_collide = SimulateAxis(weapon, 1.0f / 100.0f, 1);

  if (x_collide || y_collide) {
    weapon.last_event_time = GetMicrosecondTick();
    weapon.last_event_position = weapon.position;

    if ((type == WeaponType::Bullet || type == WeaponType::BouncingBullet) && weapon.data.shrap > 0) {
      s32 remaining = weapon.end_tick - GetCurrentTick();
      s32 duration = connection.settings.BulletAliveTime - remaining;

      if (remaining < 0 || duration <= 25) {
        return WeaponSimulateResult::TimedOut;
      }
    }

    if (type == WeaponType::Bullet || type == WeaponType::Bomb || type == WeaponType::ProximityBomb) {
      if (weapon.bounces_remaining == 0) {
        if ((type == WeaponType::Bomb || type == WeaponType::ProximityBomb) && ship_controller) {
          ship_controller->OnWeaponHit(weapon);
        }

        return WeaponSimulateResult::WallExplosion;
      }

      if (--weapon.bounces_remaining == 0 && !(weapon.flags & WEAPON_FLAG_EMP)) {
        // Do nothing. It would normally change sprite.
      }
    } else if (type == WeaponType::Burst) {
      weapon.flags |= WEAPON_FLAG_BURST_ACTIVE;
    }
  }

  return WeaponSimulateResult::Continue;
}

void WeaponManager::ClearWeapons(Player& player) {
  for (size_t i = 0; i < weapon_count; ++i) {
    Weapon* weapon = weapons + i;

    if (weapon->player_id == player.id) {
      weapons[i--] = weapons[--weapon_count];
    }
  }
}

bool WeaponManager::HasLinkRemoved(u32 link_id) {
  for (size_t i = 0; i < link_removal_count; ++i) {
    if (link_removals[i].link_id == link_id) {
      return true;
    }
  }

  return false;
}

void WeaponManager::AddLinkRemoval(u32 link_id, WeaponSimulateResult result) {
  assert(link_removal_count < ZERO_ARRAY_SIZE(link_removals));

  WeaponLinkRemoval* removal = link_removals + link_removal_count++;
  removal->link_id = link_id;
  removal->result = result;
}

void WeaponManager::CreateExplosion(Weapon& weapon) {
  WeaponType type = weapon.data.type;

  switch (type) {
    case WeaponType::Bomb:
    case WeaponType::ProximityBomb:
    case WeaponType::Thor: {
      s32 count = weapon.data.shrap;

      VieRNG rng = {(s32)weapon.rng_seed};

      for (s32 i = 0; i < count; ++i) {
        s32 orientation = 0;

        if (!connection.settings.ShrapnelRandom) {
          orientation = (i * 40000) / count * 9;
        } else {
          orientation = (rng.GetNext() % 40000) * 9;
        }

        Vector2f direction(sinf(Radians(orientation / 1000.0f)), -cosf(Radians(orientation / 1000.0f)));

        float speed = connection.settings.ShrapnelSpeed / 10.0f / 16.0f;

        Weapon* shrap = weapons + weapon_count++;

        shrap->bounces_remaining = 0;
        shrap->data = weapon.data;
        shrap->data.level = weapon.data.shraplevel;
        if (weapon.data.shrapbouncing) {
          shrap->data.type = WeaponType::BouncingBullet;
        } else {
          shrap->data.type = WeaponType::Bullet;
        }
        shrap->flags = 0;
        shrap->frequency = weapon.frequency;
        shrap->link_id = 0xFFFFFFFF;
        shrap->player_id = weapon.player_id;
        shrap->velocity = Normalize(direction) * speed;
        shrap->position = weapon.position;
        shrap->last_tick = GetCurrentTick();
        shrap->end_tick = shrap->last_tick + connection.settings.BulletAliveTime;
        shrap->last_event_position = shrap->position;
        shrap->last_event_time = GetMicrosecondTick();

        if (connection.map.IsSolid((u16)shrap->position.x, (u16)shrap->position.y, shrap->frequency)) {
          --weapon_count;
        }
      }

      weapon.rng_seed = (u32)rng.seed;
    } break;
    case WeaponType::BouncingBullet:
    case WeaponType::Bullet: {
    } break;
    default: {
    } break;
  }
}

Vector2f WeaponManager::GetExtrapolatedPos(Weapon& weapon) {
  u64 microtick = GetMicrosecondTick();
  float elapsed_seconds = (microtick - weapon.last_event_time) / 1000000.0f;
  Vector2f travel_ray = elapsed_seconds * weapon.velocity;
  Vector2f extrapolated_pos;

  if (weapon.data.type != WeaponType::Thor) {
    CastResult cast =
        connection.map.Cast(weapon.last_event_position, Normalize(travel_ray), travel_ray.Length(), weapon.frequency);
    extrapolated_pos = cast.position;

    float desync_threshold = weapon.velocity.Length() * (4.0f / 100.0f);

    // Resync render position when deviating too much from projected path
    if (extrapolated_pos.DistanceSq(weapon.position) > desync_threshold * desync_threshold) {
      extrapolated_pos = weapon.position;
      weapon.last_event_position = weapon.position;
      weapon.last_event_time = microtick;
    }
  } else {
    extrapolated_pos = weapon.last_event_position + travel_ray;
  }

  return extrapolated_pos;
}

u32 WeaponManager::CalculateRngSeed(u32 x, u32 y, u32 vel_x, u32 vel_y, u16 shrap_count, u16 weapon_level,
                                    u32 frequency) {
  u32 x1000 = (u32)(x * 1000);
  u32 y1000 = (u32)(y * 1000);
  u32 x_vel = (u32)(vel_x);
  u32 y_vel = (u32)(vel_y);

  return shrap_count + weapon_level + x1000 + y1000 + x_vel + y_vel + frequency;
}

void WeaponManager::OnWeaponPacket(u8* pkt, size_t size) {
  NetworkBuffer buffer(pkt, size, size);

  buffer.ReadU8();

  u8 direction = buffer.ReadU8();
  u16 timestamp = buffer.ReadU16();
  u16 x = buffer.ReadU16();
  s16 vel_y = (s16)buffer.ReadU16();
  u16 pid = buffer.ReadU16();
  s16 vel_x = (s16)buffer.ReadU16();
  u8 checksum = buffer.ReadU8();
  buffer.ReadU8();  // Togglables
  u8 ping = buffer.ReadU8();
  u16 y = buffer.ReadU16();
  buffer.ReadU16();  // Bounty
  u16 weapon_data = buffer.ReadU16();

  if (weapon_data == 0) return;

  // Player sends out position packet with their timestamp, it takes ping ticks to reach server, server re-timestamps it
  // and sends it to us.
  u32 server_timestamp = ((connection.GetServerTick() & 0x7FFF0000) | timestamp);
  u32 local_timestamp = server_timestamp - connection.time_diff - ping;

  Player* player = player_manager.GetPlayerById(pid);
  if (!player) return;

  Vector2f position(x / 16.0f, y / 16.0f);
  Vector2f velocity(vel_x / 16.0f / 10.0f, vel_y / 16.0f / 10.0f);
  WeaponData data = *(WeaponData*)&weapon_data;

  FireWeapons(*player, data, x, y, vel_x, vel_y, local_timestamp);
}

bool WeaponManager::FireWeapons(Player& player, WeaponData weapon, u32 pos_x, u32 pos_y, s32 vel_x, s32 vel_y,
                                u32 timestamp) {
  ShipSettings& ship_settings = connection.settings.ShipSettings[player.ship];
  WeaponType type = weapon.type;

  u8 direction = (u8)(player.orientation * 40.0f);
  u16 pid = player.id;

  if ((type == WeaponType::Bomb || type == WeaponType::ProximityBomb) && weapon.alternate) {
    Player* self = player_manager.GetSelf();

    if (self && self->id == player.id) {
      size_t self_count = 0;
      size_t team_count = 0;
      bool has_check_mine = false;

      GetMineCounts(player, player.position, &self_count, &team_count, &has_check_mine);

      if (has_check_mine) {
        return false;
      }

      if (self_count >= ship_settings.MaxMines) {
        return false;
      }

      if (team_count >= (size_t)connection.settings.TeamMaxMines) {
        return false;
      }
    }
  }

  if (type == WeaponType::Bullet || type == WeaponType::BouncingBullet) {
    bool dbarrel = ship_settings.DoubleBarrel;

    Vector2f heading = OrientationToHeading(direction);

    u32 link_id = next_link_id++;

    WeaponSimulateResult result;
    bool destroy_link = false;

    if (dbarrel) {
      Vector2f perp = Perpendicular(heading);
      Vector2f offset = perp * (ship_settings.GetRadius() * 0.75f);

      result = GenerateWeapon(pid, weapon, timestamp, pos_x - (s32)(offset.x * 16.0f), pos_y - (s32)(offset.y * 16.0f),
                              vel_x, vel_y, heading, link_id);
      if (result == WeaponSimulateResult::PlayerExplosion) {
        AddLinkRemoval(link_id, result);
        destroy_link = true;
      }

      result = GenerateWeapon(pid, weapon, timestamp, pos_x + (s32)(offset.x * 16.0f), pos_y + (s32)(offset.y * 16.0f),
                              vel_x, vel_y, heading, link_id);
      if (result == WeaponSimulateResult::PlayerExplosion) {
        AddLinkRemoval(link_id, result);
        destroy_link = true;
      }
    } else {
      result = GenerateWeapon(pid, weapon, timestamp, pos_x, pos_y, vel_x, vel_y, heading, link_id);
      if (result == WeaponSimulateResult::PlayerExplosion) {
        AddLinkRemoval(link_id, result);
        destroy_link = true;
      }
    }

    if (weapon.alternate) {
      float rads = Radians(ship_settings.MultiFireAngle / 111.0f);
      Vector2f first_heading = Rotate(heading, rads);
      Vector2f second_heading = Rotate(heading, -rads);

      result = GenerateWeapon(pid, weapon, timestamp, pos_x, pos_y, vel_x, vel_y, first_heading, link_id);
      if (result == WeaponSimulateResult::PlayerExplosion) {
        AddLinkRemoval(link_id, result);
        destroy_link = true;
      }
      result = GenerateWeapon(pid, weapon, timestamp, pos_x, pos_y, vel_x, vel_y, second_heading, link_id);
      if (result == WeaponSimulateResult::PlayerExplosion) {
        AddLinkRemoval(link_id, result);
        destroy_link = true;
      }
    }

    if (destroy_link) {
      for (size_t i = 0; i < weapon_count; ++i) {
        Weapon* weapon = weapons + i;
        if (weapon->link_id == link_id) {
          CreateExplosion(*weapon);
          weapons[i--] = weapons[--weapon_count];
        }
      }
    }
  } else if (type == WeaponType::Burst) {
    u8 count = connection.settings.ShipSettings[player.ship].BurstShrapnel;

    for (s32 i = 0; i < count; ++i) {
      s32 orientation = (i * 40000) / count * 9;
      Vector2f direction(sinf(Radians(orientation / 1000.0f)), -cosf(Radians(orientation / 1000.0f)));

      GenerateWeapon(pid, weapon, timestamp, pos_x, pos_y, 0, 0, direction, kInvalidLink);
    }
  } else {
    GenerateWeapon(pid, weapon, timestamp, pos_x, pos_y, vel_x, vel_y, OrientationToHeading(direction), kInvalidLink);
  }

  return true;
}

WeaponSimulateResult WeaponManager::GenerateWeapon(u16 player_id, WeaponData weapon_data, u32 local_timestamp,
                                                   u32 pos_x, u32 pos_y, s32 vel_x, s32 vel_y, const Vector2f& heading,
                                                   u32 link_id) {
  Weapon* weapon = weapons + weapon_count++;

  weapon->data = weapon_data;
  weapon->player_id = player_id;
  weapon->position = Vector2f(pos_x / 16.0f, pos_y / 16.0f);
  weapon->bounces_remaining = 0;
  weapon->flags = 0;
  weapon->link_id = link_id;
  weapon->prox_hit_player_id = 0xFFFF;
  weapon->last_tick = local_timestamp;

  WeaponType type = weapon->data.type;

  Player* player = player_manager.GetPlayerById(player_id);
  assert(player);

  weapon->frequency = player->frequency;

  s16 speed = 0;
  switch (type) {
    case WeaponType::Bullet:
    case WeaponType::BouncingBullet: {
      speed = (s16)connection.settings.ShipSettings[player->ship].BulletSpeed;
    } break;
    case WeaponType::Thor:
    case WeaponType::Bomb:
    case WeaponType::ProximityBomb: {
      if (!weapon->data.alternate) {
        speed = (s16)connection.settings.ShipSettings[player->ship].BombSpeed;
        weapon->bounces_remaining = connection.settings.ShipSettings[player->ship].BombBounceCount;
      }
    } break;
    case WeaponType::Decoy: {
      weapon->initial_orientation = player->orientation;
    } break;
    case WeaponType::Burst: {
      speed = (s16)connection.settings.ShipSettings[player->ship].BurstSpeed;
    } break;
    default: {
    } break;
  }

  weapon->end_tick = local_timestamp + GetWeaponTotalAliveTime(weapon->data.type, weapon->data.alternate);

  bool is_mine = (type == WeaponType::Bomb || type == WeaponType::ProximityBomb) && weapon->data.alternate;

  if (type != WeaponType::Repel && !is_mine) {
    weapon->velocity = Vector2f(vel_x / 16.0f / 10.0f, vel_y / 16.0f / 10.0f) + heading * (speed / 16.0f / 10.0f);
  } else {
    weapon->velocity = Vector2f(0, 0);
  }

  s32 tick_diff = TICK_DIFF(GetCurrentTick(), local_timestamp);

  WeaponSimulateResult result = WeaponSimulateResult::Continue;

  for (s32 i = 0; i < tick_diff; ++i) {
    result = Simulate(*weapon);

    if (result != WeaponSimulateResult::Continue) {
      CreateExplosion(*weapon);
      --weapon_count;
      return result;
    }
  }

  weapon->last_event_position = weapon->position;
  weapon->last_event_time = GetMicrosecondTick();

  vel_x = (s32)(weapon->velocity.x * 16.0f * 10.0f);
  vel_y = (s32)(weapon->velocity.y * 16.0f * 10.0f);

  weapon->rng_seed =
      CalculateRngSeed(pos_x, pos_y, vel_x, vel_y, weapon_data.shrap, weapon_data.level, player->frequency);

  weapon->last_trail_tick = weapon->last_tick;

  if (player->id == player_manager.player_id &&
      (type == WeaponType::Bomb || type == WeaponType::ProximityBomb || type == WeaponType::Thor) &&
      !weapon->data.alternate) {
    player->bombflash_anim_t = 0.0f;
  }

  return result;
}

int WeaponManager::GetWeaponTotalAliveTime(WeaponType type, bool alternate) {
  int result = 0;

  switch (type) {
    case WeaponType::Bullet:
    case WeaponType::BouncingBullet: {
      result = connection.settings.BulletAliveTime;
    } break;
    case WeaponType::Thor:
    case WeaponType::Bomb:
    case WeaponType::ProximityBomb: {
      if (alternate) {
        result = connection.settings.MineAliveTime;
      } else {
        result = connection.settings.BombAliveTime;
      }
    } break;
    case WeaponType::Repel: {
      result = connection.settings.RepelTime;
    } break;
    case WeaponType::Decoy: {
      result = connection.settings.DecoyAliveTime;
    } break;
    case WeaponType::Burst: {
      result = connection.settings.BulletAliveTime;
    } break;
    default: {
    } break;
  }

  return result;
}

void WeaponManager::GetMineCounts(Player& player, const Vector2f& check, size_t* player_count, size_t* team_count,
                                  bool* has_check_mine) {
  *player_count = 0;
  *team_count = 0;
  *has_check_mine = false;

  for (size_t i = 0; i < weapon_count; ++i) {
    Weapon* weapon = weapons + i;
    WeaponType type = weapon->data.type;

    if (weapon->data.alternate && (type == WeaponType::Bomb || type == WeaponType::ProximityBomb)) {
      if (weapon->player_id == player.id) {
        (*player_count)++;
      }

      if (weapon->frequency == player.frequency) {
        (*team_count)++;
      }

      if (weapon->position == check) {
        *has_check_mine = true;
      }
    }
  }
}

}  // namespace zero

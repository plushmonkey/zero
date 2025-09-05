#include "WeaponManager.h"

#include <assert.h>
#include <math.h>
#include <string.h>
#include <zero/game/Buffer.h>
#include <zero/game/Camera.h>
#include <zero/game/Clock.h>
#include <zero/game/GameEvent.h>
#include <zero/game/KDTree.h>
#include <zero/game/Logger.h>
#include <zero/game/Memory.h>
#include <zero/game/PlayerManager.h>
#include <zero/game/Radar.h>
#include <zero/game/ShipController.h>
#include <zero/game/net/Connection.h>
#include <zero/game/net/PacketDispatcher.h>
#include <zero/game/render/Graphics.h>

// TODO: Spatial partition acceleration structures

namespace zero {

static void OnLargePositionPkt(void* user, u8* pkt, size_t size) {
  WeaponManager* manager = (WeaponManager*)user;

  manager->OnWeaponPacket(pkt, size);
}

WeaponManager::WeaponManager(MemoryArena& temp_arena, Connection& connection, PlayerManager& player_manager,
                             PacketDispatcher& dispatcher, AnimationSystem& animation)
    : temp_arena(temp_arena), connection(connection), player_manager(player_manager), animation(animation) {
  dispatcher.Register(ProtocolS2C::LargePosition, OnLargePositionPkt, this);
}

void WeaponManager::Update(float dt) {
  u32 tick = GetCurrentTick();

  link_removal_count = 0;

  for (size_t i = 0; i < weapon_count; ++i) {
    Weapon* weapon = weapons + i;

    Player* player = player_manager.GetPlayerById(weapon->player_id);

    if (player && connection.map.GetTileId(player->position) == kTileIdSafe) {
      Event::Dispatch(WeaponDestroyEvent(*weapon));
      weapons[i--] = weapons[--weapon_count];
      continue;
    }

    s32 tick_count = TICK_DIFF(tick, weapon->last_tick);

    for (s32 j = 0; j < tick_count; ++j) {
      WeaponSimulateResult result = WeaponSimulateResult::Continue;

      result = Simulate(*weapon, tick);

      if (result != WeaponSimulateResult::Continue && weapon->link_id != kInvalidLink) {
        AddLinkRemoval(weapon->link_id, result);
      }

      if (result == WeaponSimulateResult::PlayerExplosion || result == WeaponSimulateResult::WallExplosion) {
        CreateExplosion(*weapon);
        Event::Dispatch(WeaponDestroyEvent(*weapon));
        weapons[i--] = weapons[--weapon_count];
        break;
      } else if (result == WeaponSimulateResult::TimedOut) {
        Event::Dispatch(WeaponDestroyEvent(*weapon));
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
          Event::Dispatch(WeaponDestroyEvent(*weapon));
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

        float per_second = (gravity_thrust * 1.0f);

        weapon.velocity_x += (s32)(direction.x * per_second);
        weapon.velocity_y += (s32)(direction.y * per_second);
        weapon.UpdatePosition();
        affected = true;
      }
    }
  }

  return affected;
}

WeaponSimulateResult WeaponManager::Simulate(Weapon& weapon, u32 current_tick) {
  WeaponType type = weapon.data.type;

  if (weapon.last_tick++ >= weapon.end_tick) return WeaponSimulateResult::TimedOut;

  bool initial_sim = weapon.flags & WEAPON_FLAG_INITIAL_SIM;
  weapon.flags &= ~WEAPON_FLAG_INITIAL_SIM;

  if (type == WeaponType::Repel) {
    return SimulateRepel(weapon);
  }

  bool gravity_effect = false;

  if (connection.settings.GravityBombs && (type == WeaponType::Bomb || type == WeaponType::ProximityBomb)) {
    gravity_effect = SimulateWormholeGravity(weapon);
  }

  u16 prev_x = weapon.x;
  u16 prev_y = weapon.y;

  WeaponSimulateResult position_result = SimulatePosition(weapon);

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

    float dx = abs(weapon.x / 16000.0f - hit_player->position.x);
    float dy = abs(weapon.y / 16000.0f - hit_player->position.y);

    float highest = dx > dy ? dx : dy;

    if (highest > weapon.prox_highest_offset || TICK_GTE(weapon.last_tick, weapon.sensor_end_tick)) {
      if (ship_controller) {
        ship_controller->OnWeaponHit(weapon);
      }

      Event::Dispatch(WeaponHitEvent(weapon, hit_player));

      weapon.x = prev_x;
      weapon.y = prev_y;
      weapon.UpdatePosition();

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

  float max_distance = 0.0f;
  // Find ship max radius
  for (size_t i = 0; i < 8; ++i) {
    float radius = connection.settings.ShipSettings[i].GetRadius();
    if (radius > max_distance) {
      max_distance = radius;
    }
  }

  float weapon_radius = 18.0f;

  if (is_prox) {
    float prox = (float)(connection.settings.ProximityDistance + weapon.data.level);

    if (weapon.data.type == WeaponType::Thor) {
      prox += 3;
    }

    weapon_radius = prox * 18.0f;
  }

  weapon_radius = (weapon_radius - 14.0f) / 16.0f;

  // Combine ship radius with weapon radius to find max collision lookup distance.
  max_distance += weapon_radius;

  if (player_manager.kdtree == nullptr) {
    player_manager.kdtree = BuildPartition(temp_arena, player_manager);
  }

  KDCollection players = {};
  KDNode* node = nullptr;

  Vector2f weapon_position = weapon.GetPosition();

  if (player_manager.kdtree) {
    // Range search is L2 distance, so max distance should be multiplied by sqrt(2) to handle box corner cases.
    constexpr float sqrt2 = 1.5f;
    // Add some buffer room for rounding errors
    max_distance += 1.0f;

    node = player_manager.kdtree->RangeSearch(weapon_position, max_distance * sqrt2);
  }

  if (node) {
    players = node->Collect(temp_arena);
  }

  for (size_t i = 0; i < players.count; ++i) {
    Player* player = players.players[i];

    if (player->ship == 8) continue;
    if (player->frequency == weapon.frequency) continue;
    if (player->enter_delay > 0) continue;
    if (!player_manager.IsSynchronized(*player, current_tick)) continue;

    float radius = connection.settings.ShipSettings[player->ship].GetRadius();
    Vector2f player_r(radius, radius);
    Vector2f pos = player->position.PixelRounded();

    Vector2f min_w = weapon_position.PixelRounded() - Vector2f(weapon_radius, weapon_radius);
    Vector2f max_w = weapon_position.PixelRounded() + Vector2f(weapon_radius, weapon_radius);

    if (BoxBoxOverlap(pos - player_r, pos + player_r, min_w, max_w)) {
      bool hit = true;

      if (is_prox) {
        if (weapon.prox_hit_player_id == kInvalidPlayerId) {
          weapon.prox_hit_player_id = player->id;
          weapon.sensor_end_tick = MAKE_TICK(weapon.last_tick + connection.settings.BombExplodeDelay);

          if (initial_sim) {
            // Activate the proximity explode delay immediately because the bomb was spawned on top of an enemy.
            weapon.sensor_end_tick = weapon.last_tick;
          }

          float dx = abs(weapon_position.x - player->position.x);
          float dy = abs(weapon_position.y - player->position.y);

          if (dx > dy) {
            weapon.prox_highest_offset = dx;
          } else {
            weapon.prox_highest_offset = dy;
          }
        }

        weapon_radius = 4.0f / 16.0f;

        min_w = weapon_position.PixelRounded() - Vector2f(weapon_radius, weapon_radius);
        max_w = weapon_position.PixelRounded() + Vector2f(weapon_radius, weapon_radius);

        // Fully trigger the bomb if it hits the player's normal radius check
        hit = BoxBoxOverlap(pos - player_r, pos + player_r, min_w, max_w);

        if (!hit) {
          continue;
        }
      }

      if (hit && !HasLinkRemoved(weapon.link_id)) {
        if (ship_controller && (is_bomb || player->id == player_manager.player_id)) {
          ship_controller->OnWeaponHit(weapon);
        }

        Event::Dispatch(WeaponHitEvent(weapon, player));
      }

      // Move the position back so shrap spawns correctly
      if (type == WeaponType::Bomb || type == WeaponType::ProximityBomb) {
        weapon.x = prev_x;
        weapon.y = prev_y;
        weapon.UpdatePosition();
      }

      result = WeaponSimulateResult::PlayerExplosion;
    }
  }

  return result;
}

WeaponSimulateResult WeaponManager::SimulateRepel(Weapon& weapon) {
  float effect_radius = connection.settings.RepelDistance / 16.0f;
  float speed = connection.settings.RepelSpeed / 16.0f / 10.0f;

  Vector2f weapon_position = weapon.GetPosition();

  Vector2f rect_min = weapon_position - Vector2f(effect_radius, effect_radius);
  Vector2f rect_max = weapon_position + Vector2f(effect_radius, effect_radius);

  for (size_t i = 0; i < weapon_count; ++i) {
    Weapon& other = weapons[i];

    if (other.frequency == weapon.frequency) continue;
    if (other.data.type == WeaponType::Repel) continue;

    if (PointInsideBox(rect_min, rect_max, other.GetPosition())) {
      Vector2f direction = Normalize(other.GetPosition() - weapon.GetPosition());

      other.velocity_x = (s32)(direction.x * speed * 160.0f);
      other.velocity_y = (s32)(direction.y * speed * 160.0f);

      other.UpdatePosition();

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
      if (connection.map.GetTileId(player.position) != kTileIdSafe) {
        player.last_repel_timestamp = GetCurrentTick();

        if (player.id == player_manager.player_id) {
          Vector2f direction = Normalize(player.position - weapon.GetPosition());
          player.velocity = direction * speed;
          player.repel_time = connection.settings.RepelTime / 100.0f;
        }
      }
    }
  }

  return WeaponSimulateResult::Continue;
}

bool WeaponManager::SimulateAxis(Weapon& weapon, int axis) {
  u32* pos = axis == 0 ? &weapon.x : &weapon.y;
  s32* vel = axis == 0 ? &weapon.velocity_x : &weapon.velocity_y;

  u32 previous = *pos;
  Map& map = connection.map;

  *pos += *vel;

  if (weapon.data.type == WeaponType::Thor) return false;

  // TODO: Handle other special tiles here
  if (map.IsSolid(weapon.x / 16000, weapon.y / 16000, weapon.frequency)) {
    *pos = previous;
    *vel = -*vel;

    return true;
  }

  return false;
}

WeaponSimulateResult WeaponManager::SimulatePosition(Weapon& weapon) {
  WeaponType type = weapon.data.type;

  // This collision method deviates from Continuum when using variable update rate, so it updates by one tick at a time
  bool x_collide = SimulateAxis(weapon, 0);
  bool y_collide = SimulateAxis(weapon, 1);

  weapon.UpdatePosition();

  if (x_collide || y_collide) {
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
          Event::Dispatch(WeaponHitEvent(weapon, nullptr));
        }

        return WeaponSimulateResult::WallExplosion;
      }

      if (--weapon.bounces_remaining == 0 && !(weapon.flags & WEAPON_FLAG_EMP)) {
        weapon.animation.sprite = Graphics::anim_bombs + weapon.data.level;
      }
    } else if (type == WeaponType::Burst) {
      weapon.flags |= WEAPON_FLAG_BURST_ACTIVE;
      weapon.animation.sprite = &Graphics::anim_burst_active;
    }
  }

  TileId tile_id = this->connection.map.GetTileId(weapon.GetPosition());
  if (tile_id == kTileIdWormhole) {
    return WeaponSimulateResult::TimedOut;
  }

  return WeaponSimulateResult::Continue;
}

void WeaponManager::ClearWeapons(Player& player) {
  for (size_t i = 0; i < weapon_count; ++i) {
    Weapon* weapon = weapons + i;

    if (weapon->player_id == player.id) {
      Event::Dispatch(WeaponDestroyEvent(*weapon));
      weapons[i--] = weapons[--weapon_count];
    }
  }
}

bool WeaponManager::HasLinkRemoved(u32 link_id) {
  // This should never happen, but check just to make sure.
  if (link_id == kInvalidLink) return false;

  for (size_t i = 0; i < link_removal_count; ++i) {
    if (link_removals[i].link_id == link_id) {
      return true;
    }
  }

  return false;
}

void WeaponManager::AddLinkRemoval(u32 link_id, WeaponSimulateResult result) {
  // This should never happen, but check just to make sure.
  if (link_id == kInvalidLink) return;

  assert(link_removal_count < ZERO_ARRAY_SIZE(link_removals));

  WeaponLinkRemoval* removal = link_removals + link_removal_count++;
  removal->link_id = link_id;
  removal->result = result;
}

void WeaponManager::CreateExplosion(Weapon& weapon) {
  WeaponType type = weapon.data.type;
  Vector2f position(weapon.x / 16000.0f, weapon.y / 16000.0f);

  switch (type) {
    case WeaponType::Bomb:
    case WeaponType::ProximityBomb:
    case WeaponType::Thor: {
      if (weapon.flags & WEAPON_FLAG_EMP) {
        Vector2f offset = Graphics::anim_emp_explode.frames[0].dimensions * (0.5f / 16.0f);

        animation.AddAnimation(Graphics::anim_emp_explode, position - offset)->layer = Layer::Explosions;
      } else {
        Vector2f offset = Graphics::anim_bomb_explode.frames[0].dimensions * (0.5f / 16.0f);

        animation.AddAnimation(Graphics::anim_bomb_explode, position - offset)->layer = Layer::Explosions;
      }

      constexpr u32 kExplosionIndicatorTicks = 125;
      radar->AddTemporaryIndicator(position, GetCurrentTick() + kExplosionIndicatorTicks, Vector2f(2, 2),
                                   ColorType::RadarExplosion);

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

        shrap->animation.t = 0.0f;
        shrap->animation.repeat = true;
        shrap->bounces_remaining = 0;
        shrap->data = weapon.data;
        shrap->data.level = weapon.data.shraplevel;
        if (weapon.data.shrapbouncing) {
          shrap->data.type = WeaponType::BouncingBullet;
          shrap->animation.sprite = Graphics::anim_bounce_shrapnel + weapon.data.shraplevel;
        } else {
          shrap->data.type = WeaponType::Bullet;
          shrap->animation.sprite = Graphics::anim_shrapnel + weapon.data.shraplevel;
        }
        shrap->flags = 0;
        shrap->frequency = weapon.frequency;
        shrap->link_id = 0xFFFFFFFF;
        shrap->player_id = weapon.player_id;
        shrap->velocity_x = (s32)(direction.x * speed * 160.0f);
        shrap->velocity_y = (s32)(direction.y * speed * 160.0f);
        shrap->x = weapon.x;
        shrap->y = weapon.y;
        shrap->last_tick = GetCurrentTick();
        shrap->end_tick = shrap->last_tick + connection.settings.BulletAliveTime;
        shrap->UpdatePosition();

        Player* player = player_manager.GetPlayerById(weapon.player_id);
        if (player) {
          Event::Dispatch(WeaponSpawnEvent(*shrap, *player));
        }

        if (connection.map.IsSolid((u16)(shrap->x / 16000.0f), (u16)(shrap->y / 16000.0f), shrap->frequency)) {
          Event::Dispatch(WeaponDestroyEvent(*shrap));
          --weapon_count;
        }
      }

      weapon.rng_seed = (u32)rng.seed;
    } break;
    case WeaponType::BouncingBullet:
    case WeaponType::Bullet: {
      Vector2f offset = Graphics::anim_bullet_explode.frames[0].dimensions * (0.5f / 16.0f);
      // Render the tiny explosions below the bomb explosions so they don't look weird
      animation.AddAnimation(Graphics::anim_bullet_explode, position - offset)->layer = Layer::AfterShips;
    } break;
    default: {
    } break;
  }
}

void WeaponManager::Render(Camera& camera, Camera& ui_camera, SpriteRenderer& renderer, float dt,
                           const RadarVisibility& radar_visibility) {
#pragma pack(push, 1)
  struct DecoyRenderRequest {
    Player* player;
    Vector2f position;
  };
#pragma pack(pop)

  ArenaSnapshot snapshot = temp_arena.GetSnapshot();

  // WARNING: temp_arena must not be used for anything else here or in any calls made here.
  // All of the decoys are allocated contiguously so the base decoys array can access all of them
  DecoyRenderRequest* decoys = (DecoyRenderRequest*)temp_arena.Allocate(0);
  size_t decoy_count = 0;

  Player* self = player_manager.GetSelf();

  for (size_t i = 0; i < weapon_count; ++i) {
    Weapon* weapon = weapons + i;
    Vector2f weapon_position(weapon->x / 16000.0f, weapon->y / 16000.0f);

    if (weapon->animation.sprite) {
      weapon->animation.t += dt;

      if (!weapon->animation.IsAnimating() && weapon->animation.repeat) {
        weapon->animation.t -= weapon->animation.sprite->duration;
      }
    }

    u16 see_bomb_level = radar_visibility.see_bomb_level;

    if (weapon->data.type == WeaponType::Bomb || weapon->data.type == WeaponType::ProximityBomb) {
      bool add_indicator = false;

      if (!weapon->data.alternate && see_bomb_level > 0 && (weapon->data.level + 1) >= see_bomb_level) {
        add_indicator = true;
      } else if (weapon->data.alternate && radar_visibility.see_mines) {
        add_indicator = true;
      }

      if (add_indicator) {
        radar->AddTemporaryIndicator(weapon_position, 0, Vector2f(2, 2), ColorType::RadarBomb);
      }
    }

    if (weapon->animation.IsAnimating()) {
      SpriteRenderable& frame = weapon->animation.GetFrame();
      Vector2f position = weapon_position - frame.dimensions * (0.5f / 16.0f);

      renderer.Draw(camera, frame, position.PixelRounded(), Layer::Weapons);
    } else if (weapon->data.type == WeaponType::Decoy) {
      Player* player = player_manager.GetPlayerById(weapon->player_id);

      if (player) {
        float orientation = weapon->initial_orientation - (player->orientation - weapon->initial_orientation);

        if (orientation < 0.0f) {
          orientation += 1.0f;
        } else if (orientation >= 1.0f) {
          orientation -= 1.0f;
        }

        u8 direction = (u8)(orientation * 40);
        assert(direction < 40);

        size_t index = player->ship * 40 + direction;
        SpriteRenderable& frame = Graphics::ship_sprites[index];
        Vector2f position = weapon_position - frame.dimensions * (0.5f / 16.0f);

        renderer.Draw(camera, frame, position.PixelRounded(), Layer::Ships);

        if (self && player->id != self->id) {
          player_manager.RenderPlayerName(camera, renderer, *self, *player, weapon_position, true);
        }

        // Push them on here to minimize draw calls
        DecoyRenderRequest* request = memory_arena_push_type(&temp_arena, DecoyRenderRequest);
        request->player = player;
        request->position = weapon_position;

        ++decoy_count;
      }
    }
  }

  renderer.Render(camera);

  if (self && radar) {
    for (size_t i = 0; i < decoy_count; ++i) {
      DecoyRenderRequest* request = decoys + i;

      radar->RenderDecoy(ui_camera, renderer, *self, *request->player, request->position);
    }

    renderer.Render(ui_camera);
  }

  temp_arena.Revert(snapshot);
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
  s32 my_ping = (connection.ping + 9) / 10;
  u32 local_timestamp = MAKE_TICK(server_timestamp - connection.time_diff - ping - my_ping);

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
          Event::Dispatch(WeaponDestroyEvent(*weapon));
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

  Event::Dispatch(WeaponFireEvent(weapon, player));

  return true;
}

WeaponSimulateResult WeaponManager::GenerateWeapon(u16 player_id, WeaponData weapon_data, u32 local_timestamp,
                                                   u32 pos_x, u32 pos_y, s32 vel_x, s32 vel_y, const Vector2f& heading,
                                                   u32 link_id) {
  Weapon* weapon = weapons + weapon_count++;

  // Shouldn't be necessary, but do it anyway in case something wasn't initialized.
  memset((void*)weapon, 0, sizeof(Weapon));

  weapon->data = weapon_data;
  weapon->player_id = player_id;
  weapon->x = pos_x * 1000;
  weapon->y = pos_y * 1000;
  weapon->bounces_remaining = 0;
  weapon->flags = WEAPON_FLAG_INITIAL_SIM;
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
      if (connection.settings.ShipSettings[player->ship].EmpBomb) {
        weapon->flags |= WEAPON_FLAG_EMP;
      }

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

  weapon->end_tick = MAKE_TICK(local_timestamp + GetWeaponTotalAliveTime(weapon->data.type, weapon->data.alternate));

  bool is_mine = (type == WeaponType::Bomb || type == WeaponType::ProximityBomb) && weapon->data.alternate;

  if (type != WeaponType::Repel && !is_mine) {
    weapon->velocity_x = vel_x + (s16)(heading.x * speed);
    weapon->velocity_y = vel_y + (s16)(heading.y * speed);
  } else {
    weapon->velocity_x = 0;
    weapon->velocity_y = 0;
  }

  weapon->UpdatePosition();

  weapon->animation.t = 0.0f;
  weapon->animation.sprite = nullptr;
  weapon->animation.repeat = true;

  SetWeaponSprite(*player, *weapon);

  Event::Dispatch(WeaponSpawnEvent(*weapon, *player));

  s32 tick_diff = TICK_DIFF(GetCurrentTick(), local_timestamp);

  WeaponSimulateResult result = WeaponSimulateResult::Continue;

  for (s32 i = 0; i < tick_diff; ++i) {
    result = Simulate(*weapon, GetCurrentTick());

    if (result != WeaponSimulateResult::Continue) {
      if (type == WeaponType::Repel) {
        // Create an animation even if the repel was instant.
        Vector2f offset = Graphics::anim_repel.frames[0].dimensions * (0.5f / 16.0f);

        Vector2f weapon_position(weapon->x / 16000.0f, weapon->y / 16000.0f);
        Animation* anim =
            animation.AddAnimation(Graphics::anim_repel, weapon_position.PixelRounded() - offset.PixelRounded());
        anim->layer = Layer::AfterShips;
        anim->repeat = false;
      }

      CreateExplosion(*weapon);
      Event::Dispatch(WeaponDestroyEvent(*weapon));
      --weapon_count;
      return result;
    }
  }

  weapon->rng_seed = CalculateRngSeed(pos_x, pos_y, weapon->velocity_x, weapon->velocity_y, weapon_data.shrap,
                                      weapon_data.level, player->frequency);

  weapon->last_trail_tick = weapon->last_tick;

  if (player->id == player_manager.player_id &&
      (type == WeaponType::Bomb || type == WeaponType::ProximityBomb || type == WeaponType::Thor) &&
      !weapon->data.alternate) {
    player->bombflash_anim_t = 0.0f;
  }

  return result;
}

void WeaponManager::SetWeaponSprite(Player& player, Weapon& weapon) {
  WeaponType type = weapon.data.type;

  Vector2f weapon_position(weapon.x / 16000.0f, weapon.y / 16000.0f);

  switch (type) {
    case WeaponType::Bullet: {
      weapon.animation.sprite = Graphics::anim_bullets + weapon.data.level;
    } break;
    case WeaponType::BouncingBullet: {
      weapon.animation.sprite = Graphics::anim_bullets_bounce + weapon.data.level;
    } break;
    case WeaponType::ProximityBomb:
    case WeaponType::Bomb: {
      bool emp = connection.settings.ShipSettings[player.ship].EmpBomb;

      if (weapon.data.alternate) {
        if (emp) {
          weapon.animation.sprite = Graphics::anim_emp_mines + weapon.data.level;
          weapon.flags |= WEAPON_FLAG_EMP;
        } else {
          weapon.animation.sprite = Graphics::anim_mines + weapon.data.level;
        }
      } else {
        if (emp) {
          weapon.animation.sprite = Graphics::anim_emp_bombs + weapon.data.level;
          weapon.flags |= WEAPON_FLAG_EMP;
        } else {
          if (weapon.bounces_remaining > 0) {
            weapon.animation.sprite = Graphics::anim_bombs_bounceable + weapon.data.level;
          } else {
            weapon.animation.sprite = Graphics::anim_bombs + weapon.data.level;
          }
        }
      }
    } break;
    case WeaponType::Thor: {
      weapon.animation.sprite = &Graphics::anim_thor;
    } break;
    case WeaponType::Repel: {
      Vector2f offset = Graphics::anim_repel.frames[0].dimensions * (0.5f / 16.0f);

      Animation* anim =
          animation.AddAnimation(Graphics::anim_repel, weapon_position.PixelRounded() - offset.PixelRounded());
      anim->layer = Layer::AfterShips;
      anim->repeat = false;

      weapon.animation.sprite = nullptr;
      weapon.animation.repeat = false;
    } break;
    case WeaponType::Burst: {
      weapon.animation.sprite = &Graphics::anim_burst_inactive;
    } break;
    default: {
    } break;
  }

  weapon.animation.t = 0.0f;
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
      result = 100;
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

  u32 check_x = (u32)check.x * 16;
  u32 check_y = (u32)check.y * 16;

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

      if (check_x == weapon->x / 1000 && check_y == weapon->y / 1000) {
        *has_check_mine = true;
      }
    }
  }
}

int GetEstimatedWeaponDamage(Weapon& weapon, Connection& connection) {
  // This might be a dangerous weapon.
  // Estimate damage from this weapon.
  int damage = 0;

  switch (weapon.data.type) {
    case WeaponType::Bullet:
    case WeaponType::BouncingBullet: {
      if (weapon.data.shrap > 0) {
        s32 remaining = weapon.end_tick - GetCurrentTick();
        s32 duration = connection.settings.BulletAliveTime - remaining;

        if (duration <= 25) {
          damage = (connection.settings.InactiveShrapDamage / 1000) * weapon.data.level;
        } else {
          float multiplier = connection.settings.ShrapnelDamagePercent / 1000.0f;

          damage = (connection.settings.BulletDamageLevel / 1000) +
                   (connection.settings.BulletDamageUpgrade / 1000) * weapon.data.level;

          damage = (int)(damage * multiplier);
        }
      } else {
        damage = (connection.settings.BulletDamageLevel / 1000) +
                 (connection.settings.BulletDamageUpgrade / 1000) * weapon.data.level;
      }
    } break;
    case WeaponType::Thor:
    case WeaponType::Bomb:
    case WeaponType::ProximityBomb: {
      int bomb_dmg = connection.settings.BombDamageLevel;
      int level = weapon.data.level;

      if (weapon.data.type == WeaponType::Thor) {
        // Weapon level should always be 0 for thor in normal gameplay, I believe, but this is how it's done
        bomb_dmg = bomb_dmg + bomb_dmg * weapon.data.level * weapon.data.level;
        level = 3 + weapon.data.level;
      }

      bomb_dmg = bomb_dmg / 1000;

      if (weapon.flags & WEAPON_FLAG_EMP) {
        bomb_dmg = (int)(bomb_dmg * (connection.settings.EBombDamagePercent / 1000.0f));
      }

      damage = bomb_dmg;
    } break;
    case WeaponType::Burst: {
      damage = connection.settings.BurstDamageLevel;
    } break;
    default: {
    } break;
  }

  return damage;
}

}  // namespace zero

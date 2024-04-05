#include "HeuristicEnergyTracker.h"

#include <zero/game/PlayerManager.h>

#include <string>

namespace zero {

static inline float GetEstimatedEnergy(EnergyHeuristicType type, PlayerManager& player_manager, Player& player) {
  if (player.ship >= 8) return 0.0f;

  auto& settings = player_manager.connection.settings.ShipSettings[player.ship];

  if (type == EnergyHeuristicType::Initial) {
    return (float)settings.InitialEnergy;
  } else if (type == EnergyHeuristicType::Maximum) {
    return (float)settings.MaximumEnergy;
  }

  return (settings.InitialEnergy + settings.MaximumEnergy) / 2.0f;
}

static inline float GetEstimatedRecharge(EnergyHeuristicType type, PlayerManager& player_manager, Player& player) {
  if (player.ship >= 8) return 0.0f;

  auto& settings = player_manager.connection.settings.ShipSettings[player.ship];

  if (type == EnergyHeuristicType::Initial) {
    return (float)settings.InitialRecharge;
  } else if (type == EnergyHeuristicType::Maximum) {
    return (float)settings.MaximumRecharge;
  }

  return (settings.InitialRecharge + settings.MaximumRecharge) / 2.0f;
}

HeuristicEnergyTracker::HeuristicEnergyTracker(PlayerManager& player_manager) : player_manager(player_manager) {
  memset(player_energy, 0, sizeof(player_energy));
}

void HeuristicEnergyTracker::Update() {
  Tick current_tick = GetCurrentTick();

  // Skip doing work if we aren't going to estimate.
  if (estimate_type == EnergyHeuristicType::None) {
    last_tick_time = current_tick;
    return;
  }

  s32 ticks = TICK_DIFF(current_tick, last_tick_time);

  if (ticks > 1000) ticks = 1000;

  while (ticks > 0) {
    for (size_t i = 0; i < player_manager.player_count; ++i) {
      Player* player = player_manager.players + i;

      if (player->ship >= 8) continue;

      if (player_energy[player->id].emp_ticks > 0) {
        if (--player_energy[player->id].emp_ticks > 0) {
          continue;
        }
      }

      if (player_energy[player->id].energy < 0.0f) {
        player_energy[player->id].energy = 0.0f;
      }

      player_energy[player->id].energy += GetEstimatedRecharge(estimate_type, player_manager, *player) / 1000.0f;

      float estimated_max_energy = GetEstimatedEnergy(estimate_type, player_manager, *player);

      if (player_energy[player->id].energy > estimated_max_energy) {
        player_energy[player->id].energy = estimated_max_energy;
      }
    }

    --ticks;
    last_tick_time = current_tick;
  }
}

float HeuristicEnergyTracker::GetEnergy(Player& player) const {
  // We might have exact data, so use that.
  if (player.energy > 0) {
    return player.energy;
  }

  if (estimate_type == EnergyHeuristicType::None) {
    return 0.0f;
  }

  return player_energy[player.id].energy;
}

float HeuristicEnergyTracker::GetEnergyPercent(Player& player) const {
  float current = GetEnergy(player);
  float max_energy = GetEstimatedEnergy(estimate_type, player_manager, player);

  if (max_energy <= 0.0f) return 1.0f;

  return current / max_energy;
}

void HeuristicEnergyTracker::HandleEvent(const WeaponFireEvent& event) {
  if (event.player.ship >= 8) return;
  if (estimate_type == EnergyHeuristicType::None) return;

  u32 cost = 0;

  auto& settings = player_manager.connection.settings.ShipSettings[event.player.ship];

  switch (event.data.type) {
    case WeaponType::Bullet:
    case WeaponType::BouncingBullet: {
      if (event.data.alternate) {
        cost = settings.MultiFireEnergy * (event.data.level + 1);
      } else {
        cost = settings.BulletFireEnergy * (event.data.level + 1);
      }
    } break;
    case WeaponType::Bomb:
    case WeaponType::ProximityBomb: {
      if (event.data.alternate) {
        cost = settings.LandmineFireEnergy + settings.LandmineFireEnergyUpgrade * (event.data.level + 1);
      } else {
        cost = settings.BombFireEnergy + settings.BombFireEnergyUpgrade * (event.data.level + 1);
      }
    } break;
    default: {
    } break;
  }

  player_energy[event.player.id].energy -= (float)cost;
}

void HeuristicEnergyTracker::HandleEvent(const PlayerFreqAndShipChangeEvent& event) {
  if (event.new_ship >= 8) return;
  if (estimate_type == EnergyHeuristicType::None) return;

  player_energy[event.player.id].energy = GetEstimatedEnergy(estimate_type, player_manager, event.player);
  player_energy[event.player.id].emp_ticks = 0;
}

void HeuristicEnergyTracker::HandleEvent(const PlayerEnterEvent& event) {
  if (event.player.ship >= 8) return;
  if (estimate_type == EnergyHeuristicType::None) return;

  player_energy[event.player.id].energy = GetEstimatedEnergy(estimate_type, player_manager, event.player);
  player_energy[event.player.id].emp_ticks = 0;
}

void HeuristicEnergyTracker::HandleEvent(const PlayerDeathEvent& event) {
  if (estimate_type == EnergyHeuristicType::None) return;

  player_energy[event.player.id].energy = GetEstimatedEnergy(estimate_type, player_manager, event.player);
  player_energy[event.player.id].emp_ticks = 0;
}

struct BombReport {
  int damage;
  u32 emp_ticks;
};

static BombReport GetBombDamage(PlayerManager& player_manager, Weapon& weapon, Player& player) {
  Connection& connection = player_manager.connection;
  WeaponType type = weapon.data.type;
  Player* shooter = player_manager.GetPlayerById(weapon.player_id);

  BombReport report = {};

  if (player.ship >= 8 || !shooter || shooter->ship >= 8) return report;

  int bomb_dmg = connection.settings.BombDamageLevel;
  int level = weapon.data.level;

  if (type == WeaponType::Thor) {
    bomb_dmg = bomb_dmg + bomb_dmg * weapon.data.level * weapon.data.level;
    level = 3 + weapon.data.level;
  }

  bomb_dmg = bomb_dmg / 1000;

  if (weapon.flags & WEAPON_FLAG_EMP) {
    bomb_dmg = (int)(bomb_dmg * (connection.settings.EBombDamagePercent / 1000.0f));
  }

  if (connection.settings.ShipSettings[shooter->ship].BombBounceCount > 0) {
    bomb_dmg = (int)(bomb_dmg * (connection.settings.BBombDamagePercent / 1000.0f));
  }

  Vector2f delta = Absolute(weapon.position - player.position) * 16.0f;

  float explode_pixels = (float)(connection.settings.BombExplodePixels + connection.settings.BombExplodePixels * level);

  if (delta.LengthSq() < explode_pixels * explode_pixels) {
    float constexpr kBombSize = 2.0f;
    float distance = delta.Length() - kBombSize;
    if (distance < 0.0f) distance = 0.0f;

    report.damage = (int)((explode_pixels - distance) * (bomb_dmg / explode_pixels));

    if (player.id != shooter->id) {
      Vector2f shooter_delta = Absolute(weapon.position - shooter->position) * 16;
      float shooter_distance = shooter_delta.Length();

      if (shooter_distance < explode_pixels) {
        report.damage -= (int)(((bomb_dmg / explode_pixels) * (explode_pixels - shooter_distance)) / 2.0f);
        if (report.damage < 0) {
          report.damage = 0;
        }
      }
    }

    if ((weapon.flags & WEAPON_FLAG_EMP) && report.damage > 0 && player.id != shooter->id) {
      TileId tile_id = connection.map.GetTileId((u16)player.position.x, (u16)player.position.y);

      if (tile_id != kTileSafeId) {
        u32 emp_time = (u32)((connection.settings.EBombShutdownTime * report.damage) / bomb_dmg);

        report.emp_ticks = emp_time;
      }
    }
  }

  return report;
}

void HeuristicEnergyTracker::HandleEvent(const WeaponHitEvent& event) {
  if (estimate_type == EnergyHeuristicType::None) return;

  Weapon& weapon = event.weapon;
  WeaponType type = weapon.data.type;
  auto& connection = player_manager.connection;
  Player* shooter = player_manager.GetPlayerById(weapon.player_id);
  Player* shot_player = event.target;

  int damage = 0;

  switch (type) {
    case WeaponType::Bomb:
    case WeaponType::ProximityBomb:
    case WeaponType::Thor: {
      for (size_t i = 0; i < player_manager.player_count; ++i) {
        Player* player = player_manager.players + i;
        BombReport report = GetBombDamage(player_manager, weapon, *player);

        if (report.damage > 0) {
          player_energy[player->id].energy -= report.damage;

          if (report.emp_ticks > player_energy[player->id].emp_ticks) {
            player_energy[player->id].emp_ticks = report.emp_ticks;
          }
        }
      }
    } break;
    case WeaponType::Bullet:
    case WeaponType::BouncingBullet: {
      if (weapon.data.shrap > 0) {
        s32 remaining = weapon.end_tick - GetCurrentTick();
        s32 duration = connection.settings.BulletAliveTime - remaining;

        if (duration <= 25) {
          damage = connection.settings.InactiveShrapDamage / 1000;
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
    case WeaponType::Burst: {
      damage = connection.settings.BurstDamageLevel;
    } break;
    default: {
    } break;
  }

  if (!connection.settings.ExactDamage &&
      (type == WeaponType::Bullet || type == WeaponType::BouncingBullet || type == WeaponType::Burst)) {
    u32 r = (rand() * 1000) % (damage * damage + 1);
    damage = (s32)sqrt(r);
  }

  if (shot_player && damage > 0) {
    player_energy[shot_player->id].energy -= damage;
  }
}

}  // namespace zero

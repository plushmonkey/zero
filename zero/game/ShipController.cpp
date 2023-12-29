#include "ShipController.h"

#include <assert.h>
#include <string.h>
#include <zero/game/ArenaSettings.h>
#include <zero/game/Camera.h>
#include <zero/game/Clock.h>
#include <zero/game/GameEvent.h>
#include <zero/game/InputState.h>
#include <zero/game/PlayerManager.h>
#include <zero/game/Radar.h>
#include <zero/game/Random.h>
#include <zero/game/Settings.h>
#include <zero/game/Soccer.h>
#include <zero/game/WeaponManager.h>
#include <zero/game/net/Connection.h>
#include <zero/game/net/PacketDispatcher.h>

namespace zero {

constexpr float kAnimDurationShipExplode = 0.8f;
constexpr u32 kRepelDelayTicks = 50;

static void OnCollectedPrizePkt(void* user, u8* pkt, size_t size) {
  ShipController* controller = (ShipController*)user;

  controller->OnCollectedPrize(pkt, size);
}

static void OnPlayerEnterPkt(void* user, u8* pkt, size_t size) {
  ShipController* controller = (ShipController*)user;

  u8 ship = *(pkt + 1);
  u16 player_id = *(u16*)(pkt + 51);

  if (ship != 8 && player_id == controller->player_manager.player_id) {
    controller->player_manager.Spawn();
  }
}

static void OnShipResetPkt(void* user, u8* pkt, size_t size) {
  ShipController* controller = (ShipController*)user;

  controller->ResetShip();
}

ShipController::ShipController(PlayerManager& player_manager, WeaponManager& weapon_manager,
                               PacketDispatcher& dispatcher)
    : player_manager(player_manager), weapon_manager(weapon_manager) {
  dispatcher.Register(ProtocolS2C::CollectedPrize, OnCollectedPrizePkt, this);
  dispatcher.Register(ProtocolS2C::PlayerEntering, OnPlayerEnterPkt, this);
  dispatcher.Register(ProtocolS2C::ShipReset, OnShipResetPkt, this);
}

void ShipController::Update(const InputState& input, float dt) {
  Player* self = player_manager.GetSelf();

  if (self == nullptr || self->ship >= 8 || self->enter_delay > 0.0f) {
    return;
  }

  Connection& connection = player_manager.connection;
  ShipSettings& ship_settings = connection.settings.ShipSettings[self->ship];

  u32 tick = GetCurrentTick();
  bool rockets_enabled = !TICK_GT(tick, ship.rocket_end_tick);
  bool afterburners = false;
  float ab_cost = (ship_settings.AfterburnerEnergy / 10.0f) * dt;

  if (input.IsDown(InputAction::Afterburner) && self->energy > ab_cost && !rockets_enabled) {
    afterburners = true;
  }

  u8 direction = (u8)(self->orientation * 40.0f);
  bool thrust_backward = false;
  bool thrust_forward = false;

  constexpr float kShutdownRotation = 40.0f / 400.0f;
  bool engine_shutdown = TICK_GT(ship.shutdown_end_tick, tick);

  u32 ship_speed = ship.speed;

  if (self->ship != 8) {
    AnimatedTileSet& wormholes = connection.map.GetAnimatedTileSet(AnimatedTile::Wormhole);

    s16 gravity = connection.settings.ShipSettings[self->ship].Gravity;

    for (size_t i = 0; i < wormholes.count; ++i) {
      u16 p_x = (u16)self->position.x * 16;
      u16 p_y = (u16)self->position.y * 16;
      u16 wh_x = wormholes.tiles[i].x * 16;
      u16 wh_y = wormholes.tiles[i].y * 16;

      s16 dx = (p_x - wh_x);
      s16 dy = (p_y - wh_y);

      int dist_sq = (dx * dx) + (dy * dy) + 1;

      if (dist_sq < abs(gravity) * 1000) {
        int gravity_thrust = (gravity * 1000) / dist_sq;

        Vector2f position((float)wormholes.tiles[i].x, (float)wormholes.tiles[i].y);
        Vector2f direction = Normalize(position - self->position);

        float per_second = (gravity_thrust * 10.0f / 16.0f);

        self->velocity += direction * (per_second * dt);

        if (abs(gravity_thrust) >= 1) {
          ship_speed = (u32)connection.settings.ShipSettings[self->ship].GravityTopSpeed;
        }
      }
    }

    if (ship.shield_time > 0.0f) {
      ship.shield_time -= dt;

      if (ship.shield_time < 0.0f) {
        ship.shield_time = 0.0f;
      }
    }
  }

  if (self->attach_parent == kInvalidPlayerId) {
    u32 thrust = afterburners ? ship_settings.MaximumThrust : ship.thrust;

    if (self->children) {
      thrust -= ship_settings.TurretThrustPenalty;

      if ((s32)thrust < 0) {
        thrust = 0;
      }
    }

    if (engine_shutdown) {
      thrust = 0;
    }

    if (rockets_enabled) {
      thrust = connection.settings.RocketThrust;
      self->velocity += OrientationToHeading(direction) * (thrust * (10.0f / 16.0f)) * dt;
    } else {
      if (input.IsDown(InputAction::Backward)) {
        self->velocity -= OrientationToHeading(direction) * (thrust * (10.0f / 16.0f)) * dt;
        thrust_backward = true;
      } else if (input.IsDown(InputAction::Forward)) {
        self->velocity += OrientationToHeading(direction) * (thrust * (10.0f / 16.0f)) * dt;
        thrust_forward = true;
      }
    }
  } else {
    Player* parent = player_manager.GetPlayerById(self->attach_parent);
    if (parent) {
      if (player_manager.IsSynchronized(*parent) && parent->position != Vector2f(0, 0)) {
        self->position = parent->position;
        self->velocity = parent->velocity;
        self->lerp_time = parent->lerp_time;
        self->lerp_velocity = parent->lerp_velocity;
      } else {
        self->velocity = Vector2f(0, 0);
        self->lerp_time = 0.0f;
      }
    }
  }

  if (engine_shutdown) {
    thrust_forward = false;
    thrust_backward = false;
  }

  if (input.IsDown(InputAction::Left)) {
    float rotation = ship.rotation / 400.0f;

    if (engine_shutdown) {
      rotation = kShutdownRotation;
    }

    self->orientation -= rotation * dt;
    if (self->orientation < 0) {
      self->orientation += 1.0f;
    }
  }

  if (input.IsDown(InputAction::Right)) {
    float rotation = ship.rotation / 400.0f;

    if (engine_shutdown) {
      rotation = kShutdownRotation;
    }

    self->orientation += rotation * dt;
    if (self->orientation >= 1.0f) {
      self->orientation -= 1.0f;
    }
  }

  // Only modify max speed and apply energy cost if thrusting with it enabled.
  afterburners = afterburners && (thrust_forward || thrust_backward);

  u32 speed = afterburners ? ship_settings.MaximumSpeed : ship.speed;

  if (rockets_enabled) {
    speed = connection.settings.RocketSpeed;
  }

  if (speed < ship_speed) {
    speed = ship_speed;
  }

  if (self->children) {
    speed -= ship_settings.TurretSpeedPenalty;
  }

  self->velocity.Truncate(abs((s32)speed / 10.0f / 16.0f));

  // Energy update order must be: afterburners, recharge, all of the status costs.
  // Continuum sits at full energy with afterburners enabled but lower cost than recharge.
  // Continuum will sit at lower than max energy with any status enabled that costs energy.

  if (afterburners) {
    self->energy -= ab_cost;
  }

  if (ship.emped_time > 0.0f) {
    ship.emped_time -= dt;
    if (ship.emped_time <= 0) {
      Event::Dispatch(EmpLossEvent());
    }
  } else {
    bool was_below_full = self->energy < (float)ship.energy;

    self->energy += (ship.recharge / 10.0f) * dt;
    if (self->energy >= ship.energy) {
      self->energy = (float)ship.energy;

      if (was_below_full) {
        Event::Dispatch(FullEnergyEvent());
      }
    }
  }

  HandleStatusEnergy(*self, Status_XRadar, ship_settings.XRadarEnergy, dt);
  HandleStatusEnergy(*self, Status_Stealth, ship_settings.StealthEnergy, dt);
  HandleStatusEnergy(*self, Status_Cloak, ship_settings.CloakEnergy, dt);
  HandleStatusEnergy(*self, Status_Antiwarp, ship_settings.AntiWarpEnergy, dt);

  if (player_manager.connection.map.GetTileId(self->position) == kTileSafeId) {
    if (!(self->togglables & Status_Safety)) {
      Event::Dispatch(SafeEnterEvent(self->position));
    }

    self->togglables |= Status_Safety;
  } else {
    if (self->togglables & Status_Safety) {
      Event::Dispatch(SafeLeaveEvent(self->position));
    }

    self->togglables &= ~Status_Safety;
  }

  FireWeapons(*self, input, dt, afterburners);
}

void ShipController::HandleStatusEnergy(Player& self, u32 status, u32 cost, float dt) {
  if (self.togglables & status) {
    float update_cost = (cost / 10.0f) * dt;

    if (self.energy > update_cost) {
      self.energy -= update_cost;
    } else {
      self.togglables &= ~status;
    }
  }
}

inline void SetNextTick(u32* target, u32 next_tick) {
  if (TICK_GT(next_tick, *target)) {
    *target = next_tick;
  }
}

void ShipController::AddBombDelay(u32 tick_amount) {
  SetNextTick(&ship.next_bomb_tick, GetCurrentTick() + tick_amount);
}

void ShipController::AddBulletDelay(u32 tick_amount) {
  SetNextTick(&ship.next_bullet_tick, GetCurrentTick() + tick_amount);
}

void ShipController::FireWeapons(Player& self, const InputState& input, float dt, bool afterburners) {
  Connection& connection = player_manager.connection;
  ShipSettings& ship_settings = connection.settings.ShipSettings[self.ship];
  u32 tick = GetCurrentTick();

  memset(&self.weapon, 0, sizeof(self.weapon));
  bool used_weapon = false;

  u16 energy_cost = 0;

  bool in_safe = connection.map.GetTileId(self.position) == kTileSafeId;

  bool can_fastshoot = !afterburners || !ship_settings.DisableFastShooting;

  if (input.IsDown(InputAction::Repel)) {
    if (TICK_GT(tick, ship.next_repel_tick)) {
      if (ship.repels > 0) {
        self.weapon.type = WeaponType::Repel;
        used_weapon = true;

        if (!in_safe) {
          --ship.repels;
        }

        SetNextTick(&ship.next_bomb_tick, tick + ship_settings.BombFireDelay);
        SetNextTick(&ship.next_bullet_tick, tick + ship_settings.BombFireDelay);
        ship.next_repel_tick = tick + kRepelDelayTicks;
      }
    }
  }

  if (input.IsDown(InputAction::Burst)) {
    if (TICK_GT(tick, ship.next_bomb_tick)) {
      if (ship.bursts > 0) {
        self.weapon.type = WeaponType::Burst;
        used_weapon = true;

        if (!in_safe) {
          --ship.bursts;
        }

        SetNextTick(&ship.next_bomb_tick, tick + ship_settings.BombFireDelay);
        SetNextTick(&ship.next_bullet_tick, tick + ship_settings.BombFireDelay);
        ship.next_repel_tick = tick + kRepelDelayTicks;
      }
    }
  }

  if (input.IsDown(InputAction::Thor)) {
    if (TICK_GT(tick, ship.next_bomb_tick)) {
      if (ship.thors > 0 && can_fastshoot) {
        self.weapon.type = WeaponType::Thor;
        used_weapon = true;

        if (!in_safe) {
          --ship.thors;
        }

        SetNextTick(&ship.next_bomb_tick, tick + ship_settings.BombFireDelay);
        SetNextTick(&ship.next_bullet_tick, tick + ship_settings.BombFireDelay);
        ship.next_repel_tick = tick + kRepelDelayTicks;
      }
    }
  }

  if (input.IsDown(InputAction::Decoy)) {
    if (TICK_GT(tick, ship.next_bomb_tick)) {
      if (ship.decoys > 0) {
        self.weapon.type = WeaponType::Decoy;
        used_weapon = true;

        if (!in_safe) {
          --ship.decoys;
        }

        SetNextTick(&ship.next_bomb_tick, tick + ship_settings.BombFireDelay);
        SetNextTick(&ship.next_bullet_tick, tick + ship_settings.BombFireDelay);
        ship.next_repel_tick = tick + kRepelDelayTicks;
      }
    }
  }

  if (input.IsDown(InputAction::Brick)) {
    if (TICK_GT(tick, ship.next_bomb_tick)) {
      if (ship.bricks > 0 && !in_safe) {
        --ship.bricks;

        connection.SendDropBrick(self.position);
        SetNextTick(&ship.next_bomb_tick, tick + ship_settings.BombFireDelay);
        SetNextTick(&ship.next_bullet_tick, tick + ship_settings.BombFireDelay);
      }
    }
  }

  if (input.IsDown(InputAction::Rocket)) {
    if (TICK_GT(tick, ship.next_bomb_tick) && TICK_GT(tick, ship.rocket_end_tick)) {
      if (ship.rockets > 0) {
        --ship.rockets;

        ship.rocket_end_tick = tick + ship_settings.RocketTime;

        SetNextTick(&ship.next_bomb_tick, tick + ship_settings.BombFireDelay);
        SetNextTick(&ship.next_bullet_tick, tick + ship_settings.BombFireDelay);
        ship.next_repel_tick = tick + kRepelDelayTicks;
      }
    }
  }

  bool portal_input = input.IsDown(InputAction::Portal);

  if (portal_input) {
    float portal_time = connection.settings.WarpPointDelay / 100.0f;

    bool was_cleared = portal_input_cleared;
    portal_input_cleared = false;

    if (was_cleared && ship.portals > 0 && !player_manager.IsAntiwarped(self, true)) {
      --ship.portals;

      ship.portal_time = portal_time;
      ship.portal_location = self.position;

      Event::Dispatch(PortalDropEvent(self.position, portal_time));
    }
  } else {
    portal_input_cleared = true;
  }

  // Prevent warping on portal placement if they have overlapping keybinds.
  bool warp_input = input.IsDown(InputAction::Warp);
  if (warp_input) {
    bool was_cleared = warp_input_cleared;
    warp_input_cleared = false;

    if (was_cleared && !portal_input) {
      bool fired_ball = player_manager.soccer->FireBall(BallFireMethod::Warp);

      if (!fired_ball && !player_manager.IsAntiwarped(self, true)) {
        bool warped = false;
        Vector2f previous_pos = self.position;

        if (ship.portal_time > 0.0f) {
          ship.portal_time = 0.0f;

          Vector2f from = self.position;

          self.togglables |= Status_Flash;
          self.warp_anim_t = 0.0f;
          self.position = ship.portal_location;

          player_manager.SendPositionPacket();

          ship.next_bomb_tick = tick + kRepelDelayTicks;
          ship.fake_antiwarp_end_tick = tick + connection.settings.AntiwarpSettleDelay;
          warped = true;

          Event::Dispatch(PortalWarpEvent(from, self.position));
        } else {
          if (TICK_GT(tick, ship.next_bomb_tick)) {
            if (self.energy < ship.energy) {
              // Not enough energy to warp
            } else {
              Vector2f from = self.position;

              self.togglables |= Status_Flash;
              self.warp_anim_t = 0.0f;
              self.energy = 1.0f;
              self.velocity = Vector2f(0, 0);

              player_manager.Spawn(false);

              ship.fake_antiwarp_end_tick = tick + connection.settings.AntiwarpSettleDelay;

              warped = true;

              Event::Dispatch(SpawnWarpEvent(from, self.position));
            }

            ship.next_bomb_tick = tick + kRepelDelayTicks;
          }
        }
      }
    }
  } else {
    warp_input_cleared = true;
  }

  if (input.IsDown(InputAction::Bullet) && TICK_GT(tick, ship.next_bullet_tick)) {
    if (ship.guns > 0 && can_fastshoot) {
      if (!player_manager.soccer->FireBall(BallFireMethod::Gun)) {
        self.weapon.level = ship.guns - 1;

        if (connection.settings.FlaggerGunUpgrade) {
          if (self.flags > 0 || (self.ball_carrier && connection.settings.UseFlagger)) {
            self.weapon.level++;
          }
        }

        if (ship.capability & ShipCapability_BouncingBullets) {
          self.weapon.type = WeaponType::BouncingBullet;
        } else {
          self.weapon.type = WeaponType::Bullet;
        }

        self.weapon.alternate = ship.multifire && (ship.capability & ShipCapability_Multifire);

        u32 delay = 0;
        if (self.weapon.alternate) {
          delay = ship_settings.MultiFireDelay;
          energy_cost = ship_settings.MultiFireEnergy * (self.weapon.level + 1);
        } else {
          delay = ship_settings.BulletFireDelay;
          energy_cost = ship_settings.BulletFireEnergy * (self.weapon.level + 1);
        }

        used_weapon = energy_cost < self.energy;

        if (used_weapon) {
          SetNextTick(&ship.next_bullet_tick, tick + delay);
          SetNextTick(&ship.next_bomb_tick, ship.next_bullet_tick);
        } else {
          self.weapon = {};
        }
      }
    }
  }

  bool mine_input = input.IsDown(InputAction::Mine);

  if (mine_input && TICK_GT(tick, ship.next_bomb_tick)) {
    if (ship.bombs > 0) {
      if (!player_manager.soccer->FireBall(BallFireMethod::Bomb)) {
        self.weapon.level = ship.bombs - 1;
        self.weapon.type = (ship.capability & ShipCapability_Proximity) ? WeaponType::ProximityBomb : WeaponType::Bomb;
        self.weapon.alternate = 1;

        if (connection.settings.FlaggerBombUpgrade) {
          if (self.flags > 0 || (self.ball_carrier && connection.settings.UseFlagger)) {
            self.weapon.level++;
          }
        }

        if (ship.guns > 0) {
          self.weapon.shrap = ship.shrapnel;
          self.weapon.shraplevel = ship.guns - 1;
          self.weapon.shrapbouncing = (ship.capability & ShipCapability_BouncingBullets) > 0;
        }

        energy_cost =
            ship_settings.LandmineFireEnergy + ship_settings.LandmineFireEnergyUpgrade * (self.weapon.level + 1);

        used_weapon = energy_cost < self.energy;

        if (used_weapon) {
          SetNextTick(&ship.next_bomb_tick, tick + ship_settings.BombFireDelay);

          if (!ship_settings.EmpBomb) {
            SetNextTick(&ship.next_bullet_tick, ship.next_bomb_tick);
            ship.next_repel_tick = tick + kRepelDelayTicks;
          }
        } else {
          self.weapon = {};
        }
      }
    }
  }

  if (!mine_input && input.IsDown(InputAction::Bomb) && TICK_GT(tick, ship.next_bomb_tick)) {
    if (ship.bombs > 0 && can_fastshoot) {
      if (!player_manager.soccer->FireBall(BallFireMethod::Bomb)) {
        self.weapon.level = ship.bombs - 1;
        self.weapon.type = (ship.capability & ShipCapability_Proximity) ? WeaponType::ProximityBomb : WeaponType::Bomb;

        if (connection.settings.FlaggerBombUpgrade) {
          if (self.flags > 0 || (self.ball_carrier && connection.settings.UseFlagger)) {
            self.weapon.level++;
          }
        }

        if (ship.guns > 0) {
          self.weapon.shrap = ship.shrapnel;
          self.weapon.shraplevel = ship.guns - 1;
          self.weapon.shrapbouncing = (ship.capability & ShipCapability_BouncingBullets) > 0;
        }

        energy_cost = ship_settings.BombFireEnergy + ship_settings.BombFireEnergyUpgrade * (self.weapon.level + 1);
        used_weapon = energy_cost < self.energy;

        // Disable prox bombs if they are fired near other players with BombSafety on
        if (used_weapon && self.weapon.type == WeaponType::ProximityBomb && connection.settings.BombSafety) {
          float prox = (float)(connection.settings.ProximityDistance + self.weapon.level);

          for (size_t i = 0; i < player_manager.player_count; ++i) {
            Player* player = player_manager.players + i;

            if (player->ship == 8) continue;
            if (player->frequency == self.frequency) continue;
            if (player->enter_delay > 0) continue;
            if (!player_manager.IsSynchronized(*player)) continue;

            if (self.position.DistanceSq(player->position) <= prox * prox) {
              used_weapon = false;
            }
          }
        }

        if (used_weapon) {
          // Apply thrust here before the firing velocity is calculated since this affects it.
          float thrust = ship_settings.BombThrust / 100.0f * 10.0f / 16.0f;
          self.velocity -= OrientationToHeading((u8)(self.orientation * 40.0f)) * thrust;

          SetNextTick(&ship.next_bomb_tick, tick + ship_settings.BombFireDelay);

          if (!ship_settings.EmpBomb) {
            SetNextTick(&ship.next_bullet_tick, ship.next_bomb_tick);
            ship.next_repel_tick = tick + kRepelDelayTicks;
          }
        } else {
          self.weapon = {};
        }
      }
    }
  }

  if (used_weapon) {
    if (!in_safe && (self.togglables & Status_Cloak)) {
      self.togglables &= ~Status_Cloak;
      self.togglables |= Status_Flash;
    }

    if (ship.super_time > 0.0f) {
      energy_cost = 0;
    }

    if (connection.map.GetTileId(self.position) == kTileSafeId) {
      self.velocity = Vector2f(0, 0);
    } else if (self.energy > energy_cost) {
      u32 x = (u32)(self.position.x * 16);
      u32 y = (u32)(self.position.y * 16);
      s32 vel_x = (s32)(self.velocity.x * 16.0f * 10.0f);
      s32 vel_y = (s32)(self.velocity.y * 16.0f * 10.0f);

      if (weapon_manager.FireWeapons(self, self.weapon, x, y, vel_x, vel_y, GetCurrentTick())) {
        self.energy -= energy_cost;
        player_manager.SendPositionPacket();
      }
    }
  }

  memset(&self.weapon, 0, sizeof(self.weapon));
}

size_t ShipController::GetGunIconIndex() {
  size_t start = 0;

  if (!ship.guns) {
    return 0xFFFFFFFF;
  }

  if (ship.capability & ShipCapability_Multifire) {
    if (ship.multifire) {
      start = 3;
    } else {
      start = 6;
    }
  }

  if (ship.capability & ShipCapability_BouncingBullets) {
    start += 9;
  }

  return start + ship.guns - 1;
}

size_t ShipController::GetBombIconIndex() {
  size_t start = 18;

  if (!ship.bombs) {
    return 0xFFFFFFFF;
  }

  if ((ship.capability & ShipCapability_Proximity) && !ship.shrapnel) {
    start += 3;
  } else if ((ship.capability & ShipCapability_Proximity) && ship.shrapnel) {
    start += 9;
  } else if (!(ship.capability & ShipCapability_Proximity) && ship.shrapnel) {
    start += 6;
  }

  return start + ship.bombs - 1;
}

void ShipController::OnCollectedPrize(u8* pkt, size_t size) {
  u16 count = *(u16*)(pkt + 1);
  s16 prize_id = *(s16*)(pkt + 3);

  Player* self = player_manager.GetSelf();

  if (!self) return;

  for (u16 i = 0; i < count; ++i) {
    ApplyPrize(self, prize_id, true);
  }
}

void ShipController::ApplyPrize(Player* self, s32 prize_id, bool notify, bool damage) {
  bool negative = (prize_id < 0);
  Prize prize = (Prize)prize_id;

  if (negative) {
    prize = (Prize)(-prize_id);
    if (self->bounty > 0) {
      --self->bounty;
    }
  } else {
    ++self->bounty;
  }

  ShipSettings& ship_settings = player_manager.connection.settings.ShipSettings[self->ship];

  const char* kPositiveNotifications[] = {
      "",
      "Charge rate increased.",
      "Maximum energy level increased.",
      "Rotation speed increased.",
      "Stealth available.",
      "Cloak available.",
      "X-Radar available.",
      "Warp!",
      "Guns upgraded.",
      "Bombs upgraded.",
      "Bouncing bullets.",
      "Thrusters upgraded.",
      "Top speed increased.",
      "Full charge.",
      "Engines shut-down.",
      "MultiFire bullets.",
      "Proximity bombs.",
      "Temporary SuperPower!",
      "Temporary Shields.",
      "Shrapnel increased.",
      "AntiWarp available.",
      "Repeller increased.",
      "Burst increased.",
      "Decoy increased.",
      "Thor's hammer increased.",
      "MultiPrize!",
      "Brick increased.",
      "Rocket Increased.",
      "Portal increased.",
  };

  const char* kNegativeNotifications[] = {
      "",
      "Charge rate decreased.",
      "Maximum energy level decreased.",
      "Rotation speed decreased.",
      "Stealth lost.",
      "Cloak lost.",
      "X-Radar lost.",
      "Warp!",
      "Guns downgraded.",
      "Bombs downgraded.",
      "Bouncing bullets lost.",
      "Thrusters downgraded.",
      "Top speed reduced.",
      "Energy depleted.",
      "Engines shut-down (severe).",
      "MultiFire lost.",
      "Proximity bombs lost.",
      "",
      "",
      "Shrapnel reduced.",
      "AntiWarp lost.",
      "Repeller lost.",
      "Burst lost.",
      "Decoy lost.",
      "Thor's hammer lost.",
      "",
      "Brick lost.",
      "Rocket lost.",
      "Portal lost.",
  };

  bool max_notification = false;
  bool display_notification = false;

  switch (prize) {
    case Prize::Recharge: {
      display_notification = true;

      if (negative) {
        ship.recharge -= ship_settings.UpgradeRecharge;

        if (ship.recharge < ship_settings.InitialRecharge) {
          ship.recharge = ship_settings.InitialRecharge;
          max_notification = true;
        }
      } else {
        ship.recharge += ship_settings.UpgradeRecharge;

        if (ship.recharge > ship_settings.MaximumRecharge) {
          ship.recharge = ship_settings.MaximumRecharge;
          max_notification = true;
        }
      }
    } break;
    case Prize::Energy: {
      display_notification = true;

      if (negative) {
        ship.energy -= ship_settings.UpgradeEnergy;

        if (ship.energy < ship_settings.InitialEnergy) {
          ship.energy = ship_settings.InitialEnergy;
          max_notification = true;
        }

      } else {
        ship.energy += ship_settings.UpgradeEnergy;

        if (ship.energy > ship_settings.MaximumEnergy) {
          ship.energy = ship_settings.MaximumEnergy;
          max_notification = true;
        }
      }
    } break;
    case Prize::Rotation: {
      display_notification = true;

      if (negative) {
        ship.rotation -= ship_settings.UpgradeRotation;

        if (ship.rotation < ship_settings.InitialRotation) {
          ship.rotation = ship_settings.InitialRotation;
          max_notification = true;
        }
      } else {
        ship.rotation += ship_settings.UpgradeRotation;

        if (ship.rotation > ship_settings.MaximumRotation) {
          ship.rotation = ship_settings.MaximumRotation;
          max_notification = true;
        }
      }
    } break;
    case Prize::Stealth: {
      if (negative) {
        if (ship.capability & ShipCapability_Stealth) {
          display_notification = true;
        }

        ship.capability &= ~ShipCapability_Stealth;
      } else {
        if (ship_settings.StealthStatus > 0) {
          if (!(ship.capability & ShipCapability_Stealth)) {
            display_notification = true;
          }

          ship.capability |= ShipCapability_Stealth;
        }
      }
    } break;
    case Prize::Cloak: {
      if (negative) {
        if (ship.capability & ShipCapability_Cloak) {
          display_notification = true;
        }

        ship.capability &= ~ShipCapability_Cloak;
      } else {
        if (ship_settings.CloakStatus > 0) {
          if (!(ship.capability & ShipCapability_Cloak)) {
            display_notification = true;
          }

          ship.capability |= ShipCapability_Cloak;
        }
      }
    } break;
    case Prize::XRadar: {
      if (negative) {
        if (ship.capability & ShipCapability_XRadar) {
          display_notification = true;
        }

        ship.capability &= ~ShipCapability_XRadar;
      } else {
        if (ship_settings.XRadarStatus > 0) {
          if (!(ship.capability & ShipCapability_XRadar)) {
            display_notification = true;
          }

          ship.capability |= ShipCapability_XRadar;
        }
      }
    } break;
    case Prize::Warp: {
      display_notification = true;
      player_manager.Spawn();
      self->velocity = Vector2f(0, 0);
    } break;
    case Prize::Guns: {
      display_notification = true;

      if (negative) {
        --ship.guns;

        if (ship.guns < ship_settings.InitialGuns) {
          ship.guns = ship_settings.InitialGuns;
          max_notification = true;
        }
      } else {
        ++ship.guns;

        if (ship.guns > ship_settings.MaxGuns) {
          ship.guns = ship_settings.MaxGuns;
          max_notification = true;
        }
      }
    } break;
    case Prize::Bombs: {
      display_notification = true;
      if (negative) {
        --ship.bombs;

        if (ship.bombs < ship_settings.InitialBombs) {
          ship.bombs = ship_settings.InitialBombs;
          max_notification = true;
        }
      } else {
        ++ship.bombs;

        if (ship.bombs > ship_settings.MaxBombs) {
          ship.bombs = ship_settings.MaxBombs;
          max_notification = true;
        }
      }
    } break;
    case Prize::BouncingBullets: {
      if (negative) {
        if (ship.capability & ShipCapability_BouncingBullets) {
          display_notification = true;
        }

        ship.capability &= ~ShipCapability_BouncingBullets;
      } else {
        if (ship.capability & ShipCapability_BouncingBullets) {
          --self->bounty;
        } else {
          display_notification = true;
        }

        ship.capability |= ShipCapability_BouncingBullets;
      }
    } break;
    case Prize::Thruster: {
      display_notification = true;
      if (negative) {
        ship.thrust -= ship_settings.UpgradeThrust;

        if (ship.thrust < ship_settings.InitialThrust) {
          ship.thrust = ship_settings.InitialThrust;
          max_notification = true;
        }
      } else {
        ship.thrust += ship_settings.UpgradeThrust;

        if (ship.thrust > ship_settings.MaximumThrust) {
          ship.thrust = ship_settings.MaximumThrust;
          max_notification = true;
        }
      }
    } break;
    case Prize::TopSpeed: {
      display_notification = true;
      if (negative) {
        ship.speed -= ship_settings.UpgradeSpeed;

        if (ship.speed < ship_settings.InitialSpeed) {
          ship.speed = ship_settings.InitialSpeed;
          max_notification = true;
        }
      } else {
        ship.speed += ship_settings.UpgradeSpeed;

        if (ship.speed > ship_settings.MaximumSpeed) {
          ship.speed = ship_settings.MaximumSpeed;
          max_notification = true;
        }
      }
    } break;
    case Prize::FullCharge: {
      display_notification = true;

      if (negative) {
        self->energy = 1;
      } else {
        self->energy = (float)ship.energy;
      }
    } break;
    case Prize::EngineShutdown: {
      u32 ticks = player_manager.connection.settings.EngineShutdownTime;

      if (negative) {
        ticks *= 3;
      }

      ship.shutdown_end_tick = GetCurrentTick() + ticks;

      display_notification = true;
    } break;
    case Prize::Multifire: {
      if (negative) {
        if (ship.capability & ShipCapability_Multifire) {
          display_notification = true;
        }

        ship.capability &= ~ShipCapability_Multifire;
      } else {
        if (!(ship.capability & ShipCapability_Multifire)) {
          display_notification = true;
        }

        ship.capability |= ShipCapability_Multifire;
      }
    } break;
    case Prize::Proximity: {
      if (negative) {
        if (ship.capability & ShipCapability_Proximity) {
          display_notification = true;
        }

        ship.capability &= ~ShipCapability_Proximity;
      } else {
        if (ship.capability & ShipCapability_Proximity) {
          --self->bounty;
        } else {
          display_notification = true;
        }

        ship.capability |= ShipCapability_Proximity;
      }
    } break;
    case Prize::Super: {
      u32 super_ticks = rand() % ship_settings.SuperTime;
      float super_time = super_ticks / 100.0f;

      if (super_time > ship.super_time) {
        ship.super_time = super_time;
      }

      display_notification = true;
    } break;
    case Prize::Shields: {
      ship.shield_time = ship_settings.ShieldsTime / 100.0f;

      display_notification = true;
    } break;
    case Prize::Shrapnel: {
      display_notification = true;

      if (negative) {
        if (ship.shrapnel >= ship_settings.ShrapnelRate) {
          ship.shrapnel -= ship_settings.ShrapnelRate;
        } else {
          max_notification = true;
        }
      } else {
        ship.shrapnel += ship_settings.ShrapnelRate;

        if (ship.shrapnel > ship_settings.ShrapnelMax) {
          ship.shrapnel = ship_settings.ShrapnelMax;
          max_notification = true;
        }
      }
    } break;
    case Prize::Antiwarp: {
      if (negative) {
        if (ship.capability & ShipCapability_Antiwarp) {
          display_notification = true;
        }

        ship.capability &= ~ShipCapability_Antiwarp;
      } else {
        if (ship_settings.AntiWarpStatus > 0) {
          if (!(ship.capability & ShipCapability_Antiwarp)) {
            display_notification = true;
          }

          ship.capability |= ShipCapability_Antiwarp;
        }
      }
    } break;
    case Prize::Repel: {
      display_notification = true;

      if (negative) {
        if (ship.repels > 0) {
          --ship.repels;
        } else {
          max_notification = true;
        }
      } else {
        ++ship.repels;
        if (ship.repels > ship_settings.RepelMax) {
          ship.repels = ship_settings.RepelMax;
          max_notification = true;
        }
      }
    } break;
    case Prize::Burst: {
      display_notification = true;

      if (negative) {
        if (ship.bursts > 0) {
          --ship.bursts;
        } else {
          max_notification = true;
        }
      } else {
        ++ship.bursts;
        if (ship.bursts > ship_settings.BurstMax) {
          ship.bursts = ship_settings.BurstMax;
          max_notification = true;
        }
      }
    } break;
    case Prize::Decoy: {
      display_notification = true;
      if (negative) {
        if (ship.decoys > 0) {
          --ship.decoys;
        } else {
          max_notification = true;
        }
      } else {
        ++ship.decoys;
        if (ship.decoys > ship_settings.DecoyMax) {
          ship.decoys = ship_settings.DecoyMax;
          max_notification = true;
        }
      }
    } break;
    case Prize::Thor: {
      display_notification = true;
      if (negative) {
        if (ship.thors > 0) {
          --ship.thors;
        } else {
          max_notification = true;
        }
      } else {
        ++ship.thors;
        if (ship.thors > ship_settings.ThorMax) {
          ship.thors = ship_settings.ThorMax;
          max_notification = true;
        }
      }
    } break;
    case Prize::Multiprize: {
      if (!negative) {
        u16 count = player_manager.connection.settings.MultiPrizeCount;

        size_t attempts = 0;
        for (u16 i = 0; i < count && attempts < 9999; ++i, ++attempts) {
          s32 random_prize = GeneratePrize(false);

          if (random_prize == 0 || random_prize == (s32)Prize::EngineShutdown || random_prize == (s32)Prize::Shields ||
              random_prize == (s32)Prize::Super || random_prize == (s32)Prize::Multiprize ||
              random_prize == (s32)Prize::Warp || random_prize == (s32)Prize::Brick) {
            --i;
            continue;
          }

          u16 bounty = self->bounty;
          ApplyPrize(self, random_prize, false, false);
          self->bounty = bounty;
        }

        display_notification = true;
      }
    } break;
    case Prize::Brick: {
      display_notification = true;
      if (negative) {
        if (ship.bricks > 0) {
          --ship.bricks;
        } else {
          max_notification = true;
        }
      } else {
        ++ship.bricks;
        if (ship.bricks > ship_settings.BrickMax) {
          ship.bricks = ship_settings.BrickMax;
          max_notification = true;
        }
      }
    } break;
    case Prize::Rocket: {
      display_notification = true;
      if (negative) {
        if (ship.rockets > 0) {
          --ship.rockets;
        } else {
          max_notification = true;
        }
      } else {
        ++ship.rockets;
        if (ship.rockets > ship_settings.RocketMax) {
          ship.rockets = ship_settings.RocketMax;
          max_notification = true;
        }
      }
    } break;
    case Prize::Portal: {
      display_notification = true;
      if (negative) {
        if (ship.portals > 0) {
          --ship.portals;
        } else {
          max_notification = true;
        }
      } else {
        ++ship.portals;
        if (ship.portals > ship_settings.PortalMax) {
          ship.portals = ship_settings.PortalMax;
          max_notification = true;
        }
      }
    } break;
    default: {
    } break;
  }

  Event::Dispatch(PrizeEvent(prize, negative));
}

s32 ShipController::GeneratePrize(bool negative_allowed) {
  u32 weight_total = player_manager.connection.prize_weight_total;

  if (weight_total <= 0) return 0;

  u8* weights = (u8*)&player_manager.connection.settings.PrizeWeights;
  VieRNG rng = {(s32)player_manager.connection.security.prize_seed};

  u32 random = rng.GetNext();
  s32 result = 0;
  u32 weight = 0;

  for (int prize_id = 0; prize_id < sizeof(player_manager.connection.settings.PrizeWeights); ++prize_id) {
    weight += weights[prize_id];

    if (random % weight_total < weight) {
      random = rng.GetNext();

      if (!negative_allowed || random % player_manager.connection.settings.PrizeNegativeFactor != 0) {
        result = prize_id + 1;
        break;
      }

      result = -(prize_id + 1);
      break;
    }
  }

  player_manager.connection.security.prize_seed = random;
  return result;
}

void ShipController::ResetShip() {
  Player* self = player_manager.GetSelf();

  if (!self) return;

  s32 last_tick = MAKE_TICK(GetCurrentTick() - 1);

  ship.shrapnel = 0;
  ship.capability = 0;
  ship.emped_time = 0.0f;
  ship.multifire = false;
  ship.rocket_end_tick = 0;
  ship.shutdown_end_tick = 0;
  ship.emped_time = 0.0f;
  ship.super_time = 0.0f;
  ship.shield_time = 0.0f;
  ship.portal_time = 0.0f;
  ship.next_bomb_tick = ship.next_bullet_tick = ship.next_repel_tick = last_tick;
  ship.rocket_end_tick = ship.shutdown_end_tick = ship.fake_antiwarp_end_tick = last_tick;

  self->flag_timer = 0;
  self->togglables = 0;
  self->bounty = 0;

  if (self->ship == 8) return;

  ShipSettings& ship_settings = player_manager.connection.settings.ShipSettings[self->ship];

  ship.energy = ship_settings.InitialEnergy;
  ship.recharge = ship_settings.InitialRecharge;
  ship.rotation = ship_settings.InitialRotation;
  ship.guns = ship_settings.InitialGuns;
  ship.bombs = ship_settings.InitialBombs;
  ship.thrust = ship_settings.InitialThrust;
  ship.speed = ship_settings.InitialSpeed;
  ship.repels = ship_settings.InitialRepel;
  ship.bursts = ship_settings.InitialBurst;
  ship.decoys = ship_settings.InitialDecoy;
  ship.thors = ship_settings.InitialThor;
  ship.bricks = ship_settings.InitialBrick;
  ship.rockets = ship_settings.InitialRocket;
  ship.portals = ship_settings.InitialPortal;

  if (ship_settings.StealthStatus == 2) {
    ship.capability |= ShipCapability_Stealth;
  }

  if (ship_settings.CloakStatus == 2) {
    ship.capability |= ShipCapability_Cloak;
  }

  if (ship_settings.XRadarStatus == 2) {
    ship.capability |= ShipCapability_XRadar;
  }

  if (ship_settings.AntiWarpStatus == 2) {
    ship.capability |= ShipCapability_Antiwarp;
  }

  u32 pristine_seed = player_manager.connection.security.prize_seed;

  // Generate random weighted prizes
  if (player_manager.connection.prize_weight_total > 0) {
    int attempts = 0;
    for (int i = 0; i < ship_settings.InitialBounty && attempts < 9999; ++i, ++attempts) {
      s32 prize_id = GeneratePrize(false);
      Prize prize = (Prize)prize_id;

      if (prize == Prize::FullCharge || prize == Prize::EngineShutdown || prize == Prize::Shields ||
          prize == Prize::Super || prize == Prize::Warp || prize == Prize::Brick) {
        --i;
        continue;
      }

      ApplyPrize(self, prize_id, false);
    }
  }

  // Restore the prize seed to maintain synchronization with other clients.
  // The GeneratePrizes called above would mutate the seed, so it should be restored.
  player_manager.connection.security.prize_seed = pristine_seed;

  self->energy = (float)ship.energy;
  self->bounty = ship_settings.InitialBounty;

  Event::Dispatch(ShipResetEvent());
}

void ShipController::UpdateSettings() {
  Player* self = player_manager.GetSelf();

  if (!self || self->ship == 8) return;

  ShipSettings& ship_settings = player_manager.connection.settings.ShipSettings[self->ship];

  u32 initial_energy = ship_settings.InitialEnergy;
  u32 initial_recharge = ship_settings.InitialRecharge;
  u32 initial_rotation = ship_settings.InitialRotation;
  u32 initial_speed = ship_settings.InitialSpeed;
  u32 initial_thrust = ship_settings.InitialThrust;

  if (ship.energy < initial_energy) {
    ship.energy = initial_energy;
  }

  if (ship.recharge < initial_recharge) {
    ship.recharge = initial_recharge;
  }

  if (ship.rotation < initial_rotation) {
    ship.rotation = initial_rotation;
  }

  if (ship.speed < initial_speed) {
    ship.speed = initial_speed;
  }

  if (ship.thrust < initial_thrust) {
    ship.thrust = initial_thrust;
  }
}

void ShipController::OnWeaponHit(Weapon& weapon) {
  WeaponType type = (WeaponType)weapon.data.type;
  Connection& connection = player_manager.connection;

  Player* self = player_manager.GetSelf();

  if (!self || self->enter_delay > 0 || self->ship == 8) return;

  Player* shooter = player_manager.GetPlayerById(weapon.player_id);

  if (!shooter) return;

  int damage = 0;

  switch (type) {
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
    case WeaponType::Thor:
    case WeaponType::Bomb:
    case WeaponType::ProximityBomb: {
      int bomb_dmg = connection.settings.BombDamageLevel;
      int level = weapon.data.level;

      if (type == WeaponType::Thor) {
        // Weapon level should always be 0 for thor in normal gameplay, I believe, but this is how it's done
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

      Vector2f delta = Absolute(weapon.position - self->position) * 16.0f;

      float explode_pixels =
          (float)(connection.settings.BombExplodePixels + connection.settings.BombExplodePixels * level);

      if (delta.LengthSq() < explode_pixels * explode_pixels) {
        float distance = delta.Length();

        damage = (int)((explode_pixels - distance) * (bomb_dmg / explode_pixels));

        if (self->id != shooter->id) {
          Vector2f shooter_delta = Absolute(weapon.position - shooter->position) * 16;
          float shooter_distance = shooter_delta.Length();

          if (shooter_distance < explode_pixels) {
            damage -= (int)(((bomb_dmg / explode_pixels) * (explode_pixels - shooter_distance)) / 2.0f);
            if (damage < 0) {
              damage = 0;
            }
          }
        }

        if ((weapon.flags & WEAPON_FLAG_EMP) && damage > 0 && self->id != shooter->id) {
          TileId tile_id = connection.map.GetTileId((u16)self->position.x, (u16)self->position.y);

          if (tile_id != kTileSafeId) {
            u32 emp_time = (u32)((connection.settings.EBombShutdownTime * damage) / bomb_dmg);

            ship.emped_time = emp_time / 100.0f;
          }
        }

        if (damage > 0 && explosion_report.on_damage) {
          explosion_report.on_damage(explosion_report.user);
        }
      }
    } break;
    case WeaponType::Burst: {
      damage = connection.settings.BurstDamageLevel;
    } break;
    default: {
    } break;
  }

  TileId tile_id = connection.map.GetTileId((u16)self->position.x, (u16)self->position.y);

  if (tile_id == kTileSafeId) {
    if (self->flags > 0) {
      connection.SendFlagDrop();
    }
    return;
  }

  if (ship.shield_time > 0.0f) {
    float max_shield_time = player_manager.connection.settings.ShipSettings[self->ship].ShieldsTime / 100.0f;
    float percent = ship.shield_time / max_shield_time;

    damage -= (int)(damage * percent);
  }

  if (!connection.settings.ExactDamage &&
      (type == WeaponType::Bullet || type == WeaponType::BouncingBullet || type == WeaponType::Burst)) {
    u32 r = (rand() * 1000) % (damage * damage + 1);
    damage = (s32)sqrt(r);
  }

  if (damage <= 0) return;

  bool died = false;

  if (self->energy < damage) {
    bool is_bomb = (type == WeaponType::Bomb || type == WeaponType::ProximityBomb || type == WeaponType::Thor);

    if (is_bomb && shooter->id == self->id) {
      self->energy = 1.0f;
    } else {
      connection.SendDeath(weapon.player_id, self->bounty);

      self->enter_delay = (connection.settings.EnterDelay / 100.0f) + kAnimDurationShipExplode;
      self->explode_anim_t = 0.0f;
      self->energy = 0;
      died = true;
    }
  } else {
    u16 factor = connection.settings.ShipSettings[self->ship].DamageFactor;
    if (self->bounty > 0 && (u32)self->energy < ship.energy && factor != 0) {
      int chance = ((factor * 200000) / (damage * 1000)) + 1;

      if ((rand() % chance) == 0) {
        s32 prize_id = 7;

        while (prize_id == 7 || prize_id == 13 || prize_id == 14 || prize_id == 17 || prize_id == 18 ||
               prize_id == 25) {
          prize_id = rand() % (u32)Prize::Count;
        }

        ApplyPrize(self, -prize_id, true, true);
      }
    }

    self->energy -= damage;
  }

  Event::Dispatch(WeaponDamageEvent(weapon, *shooter, damage, died));
}

}  // namespace zero

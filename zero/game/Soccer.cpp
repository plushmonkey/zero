#include "Soccer.h"

#include <assert.h>
#include <float.h>
#include <stdio.h>
#include <string.h>
#include <zero/game/Clock.h>
#include <zero/game/GameEvent.h>
#include <zero/game/PlayerManager.h>
#include <zero/game/Radar.h>
#include <zero/game/ShipController.h>
#include <zero/game/WeaponManager.h>
#include <zero/game/net/Connection.h>

namespace zero {

static void OnPowerballPositionPkt(void* user, u8* pkt, size_t size) {
  Soccer* soccer = (Soccer*)user;

  soccer->OnPowerballPosition(pkt, size);
}

static void OnPowerballGoalPkt(void* user, u8* pkt, size_t size) {
  if (size < 2) return;

  u8 ball_id = pkt[1];

  Soccer* soccer = (Soccer*)user;

  for (size_t i = 0; i < ZERO_ARRAY_SIZE(soccer->balls); ++i) {
    if (soccer->balls[i].id == ball_id) {
      Event::Dispatch(BallGoalEvent(soccer->balls[i]));
      return;
    }
  }
}

inline void SimulateAxis(Powerball& ball, Map& map, u32* pos, s16* vel) {
  u32 previous = *pos;

  *pos += *vel;

  float x = floorf(ball.x / 16000.0f);
  float y = floorf(ball.y / 16000.0f);

  if (map.IsSolid((u16)x, (u16)y, ball.frequency)) {
    *pos = previous;
    *vel = -*vel;
  }
}

Soccer::Soccer(PlayerManager& player_manager) : player_manager(player_manager), connection(player_manager.connection) {
  connection.dispatcher.Register(ProtocolS2C::PowerballPosition, OnPowerballPositionPkt, this);
  connection.dispatcher.Register(ProtocolS2C::SoccerGoal, OnPowerballGoalPkt, this);

  Clear();
}

void Soccer::Update(float dt) {
  u64 microtick = GetMicrosecondTick();
  u32 tick = GetCurrentTick();

  s32 pass_delay = connection.settings.PassDelay;

  for (size_t i = 0; i < ZERO_ARRAY_SIZE(balls); ++i) {
    Powerball* ball = balls + i;

    if (ball->id == kInvalidBallId) continue;

    while ((s64)(microtick - ball->last_micro_tick) >= kTickDurationMicro) {
      Simulate(*ball, true);
      ball->last_micro_tick += kTickDurationMicro;
    }

    // Update timer if the carrier is this player
    if (ball->state == BallState::Carried && ball->carrier_id == player_manager.player_id) {
      carry_id = ball->id;
      carry_timer -= dt;

      Player* self = player_manager.GetSelf();
      if (self && self->ship != 8) {
        bool has_timer = connection.settings.ShipSettings[self->ship].SoccerBallThrowTimer > 0;

        if (has_timer && carry_timer < 0) {
          float speed = connection.settings.ShipSettings[self->ship].SoccerBallSpeed / 10.0f / 16.0f;
          Vector2f position = GetBallPosition(*ball, microtick);
          Vector2f heading = OrientationToHeading((u8)(self->orientation * 40.0f));
          Vector2f velocity = self->velocity - Vector2f(heading) * speed;

          u32 timestamp = GetCurrentTick() + connection.time_diff;

          connection.SendBallFire((u8)ball->id, position, velocity, self->id, timestamp);
          carry_id = kInvalidBallId;

          Event::Dispatch(BallTimeoutEvent(*ball, position, velocity));
        }
      }
    }

    // Check for nearby player touches if the ball isn't currently phased
    if (ball->state == BallState::World && TICK_DIFF(tick, ball->last_touch_timestamp) >= pass_delay) {
      Vector2f position(ball->x / 16000.0f, ball->y / 16000.0f);

      float closest_distance = FLT_MAX;
      Player* closest_player = nullptr;

      // Loop over players to find anyone close enough to pick up the ball
      for (size_t j = 0; j < player_manager.player_count; ++j) {
        Player* player = player_manager.players + j;

        if (player->ship == 8) continue;
        if (player->enter_delay > 0.0f) continue;
        if (!player_manager.IsSynchronized(*player)) continue;
        if (player->id == ball->carrier_id && (ball->vel_x != 0 || ball->vel_y != 0)) continue;
        if (player->attach_parent != kInvalidPlayerId) continue;
        if (IsCarryingBall() && player->id == player_manager.player_id) continue;

        float pickup_radius = connection.settings.ShipSettings[player->ship].SoccerBallProximity / 16.0f;
        float dist_sq = position.DistanceSq(player->position);

        if (dist_sq <= pickup_radius * pickup_radius && dist_sq < closest_distance) {
          closest_distance = dist_sq;
          closest_player = player;
        }
      }

      if (closest_player) {
        if (closest_player->id == player_manager.player_id && TICK_DIFF(tick, last_pickup_request) >= 100) {
          // Send pickup
          connection.SendBallPickup((u8)ball->id, ball->timestamp);
          last_pickup_request = tick;

          Event::Dispatch(BallRequestPickupEvent(*ball));
        }

        ball->last_touch_timestamp = GetCurrentTick();
      }
    }
  }
}

bool Soccer::FireBall(BallFireMethod method) {
  if (!IsCarryingBall()) return false;

  assert(carry_id < ZERO_ARRAY_SIZE(balls));

  if (method == BallFireMethod::Gun && connection.settings.AllowGuns) {
    return false;
  } else if (method == BallFireMethod::Bomb && connection.settings.AllowBombs) {
    return false;
  }

  Player* self = player_manager.GetSelf();

  if (!self) return false;
  if (carry_id == kInvalidBallId) return false;

  Powerball* ball = balls + carry_id;

  float speed = connection.settings.ShipSettings[self->ship].SoccerBallSpeed / 10.0f / 16.0f;
  Vector2f position = GetBallPosition(*ball, GetMicrosecondTick());
  Vector2f heading = OrientationToHeading((u8)(self->orientation * 40.0f));
  Vector2f velocity = self->velocity + Vector2f(heading) * speed;

  u32 timestamp = MAKE_TICK(GetCurrentTick() + connection.time_diff);

  connection.SendBallFire((u8)carry_id, self->position.PixelRounded(), velocity.PixelRounded(), self->id, timestamp);
  carry_id = kInvalidBallId;

  player_manager.ship_controller->AddBombDelay(50);
  player_manager.ship_controller->AddBulletDelay(50);

  Event::Dispatch(BallFireEvent(*ball, self, position, velocity));

  return true;
}

void Soccer::Simulate(Powerball& ball, bool drop_trail) {
  if (ball.friction <= 0) return;

  SimulateAxis(ball, connection.map, &ball.x, &ball.vel_x);
  SimulateAxis(ball, connection.map, &ball.y, &ball.vel_y);

  if (ball.state != BallState::Goal && ball.carrier_id == player_manager.player_id) {
    TileId tile_id = connection.map.GetTileId(ball.x / 16000, ball.y / 16000);

    if (tile_id == kGoalTileId) {
      Vector2f position(ball.x / 16000.0f, ball.y / 16000.0f);

      if (!IsTeamGoal(position)) {
        connection.SendBallGoal((u8)ball.id, GetCurrentTick() + connection.time_diff);
        ball.state = BallState::Goal;
      }
    }
  }

  // Drop trail if the ball is moving
  if (drop_trail && (ball.vel_x != 0 || ball.vel_y != 0)) {
    if (--ball.trail_delay <= 0) {
      ball.trail_delay = 5;
    }
  }

  s32 friction = ball.friction / 1000;
  ball.vel_x = (ball.vel_x * friction) / 1000;
  ball.vel_y = (ball.vel_y * friction) / 1000;

  ball.friction -= ball.friction_delta;

  ball.next_x = ball.x;
  ball.next_y = ball.y;

  SimulateAxis(ball, connection.map, &ball.next_x, &ball.vel_x);
  SimulateAxis(ball, connection.map, &ball.next_y, &ball.vel_y);
}

void Soccer::Clear() {
  memset(balls, 0, sizeof(balls));

  for (size_t i = 0; i < ZERO_ARRAY_SIZE(balls); ++i) {
    Powerball* ball = balls + i;

    ball->id = kInvalidBallId;
    ball->carrier_id = kInvalidPlayerId;
    ball->timestamp = 0;
    ball->last_touch_timestamp = GetCurrentTick();
  }

  last_pickup_request = GetCurrentTick();
}

void Soccer::OnPowerballPosition(u8* pkt, size_t size) {
  NetworkBuffer buffer(pkt, size, size);

  buffer.ReadU8();  // Type

  u8 ball_id = buffer.ReadU8();
  u16 x = buffer.ReadU16();
  u16 y = buffer.ReadU16();
  s16 velocity_x = buffer.ReadU16();
  s16 velocity_y = buffer.ReadU16();
  u16 owner_id = buffer.ReadU16();
  u32 timestamp = buffer.ReadU32() & 0x7FFFFFFF;

  if (ball_id >= ZERO_ARRAY_SIZE(balls)) return;

  Powerball* ball = balls + ball_id;

  bool new_ball_pos_pkt = ball->id == kInvalidBallId || TICK_GT(timestamp, ball->timestamp) ||
                          ball->state == BallState::Goal || (ball->state == BallState::Carried && timestamp != 0);
  ball->id = ball_id;

  if (new_ball_pos_pkt) {
    ball->x = x * 1000;
    ball->y = y * 1000;
    ball->next_x = ball->x;
    ball->next_y = ball->y;
    ball->vel_x = velocity_x;
    ball->vel_y = velocity_y;
    ball->frequency = 0xFFFF;
    ball->state = BallState::World;

    if (ball_id == carry_id) {
      carry_id = kInvalidBallId;
      carry_timer = 0.0f;

      auto self = player_manager.GetSelf();

      if (self) {
        self->ball_carrier = false;
      }
    }

    u32 current_timestamp = MAKE_TICK(GetCurrentTick() + connection.time_diff);
    s32 sim_ticks = TICK_DIFF(current_timestamp, timestamp);

    if (sim_ticks > 6000 || sim_ticks < 0) {
      sim_ticks = 6000;
    }

    if (timestamp == 0) {
      sim_ticks = 0;
    }

    // The way this is setup seems like it could desynchronize state depending on brick setup at the time.
    // Maybe initial synchronization should ignore current bricks?
    //
    // This also seems to send owner id of 0xFFFF so how can a new client join and fully synchronize without knowing
    // the ball friction? Each ship has its own friction but the ship can't be determined based on the data sent.
    //
    // I don't think there's a way to actually solve this. Does Continuum also not synchronize correctly in certain
    // situations?

    u8 ship = 0;

    Player* carrier = nullptr;

    if (owner_id != kInvalidPlayerId) {
      carrier = player_manager.GetPlayerById(owner_id);

      if (carrier) {
        carrier->ball_carrier = false;
        ball->frequency = carrier->frequency;

        ship = carrier->ship;

        if (ship == 8) {
          ship = 0;
        }
      }

      if (owner_id == player_manager.player_id) {
        last_pickup_request = GetCurrentTick();
      }

      ball->last_touch_timestamp = GetCurrentTick();
    }

    if (ball->vel_x != 0 || ball->vel_y != 0) {
      ball->friction_delta = connection.settings.ShipSettings[ship].SoccerBallFriction;
      ball->friction = 1000000;
    } else {
      ball->friction = 0;
    }

    ball->carrier_id = owner_id;

    Event::Dispatch(BallFireEvent(*ball, carrier, Vector2f(x / 16.0f, y / 16.0f),
                                  Vector2f(velocity_x / 160.0f, velocity_y / 160.0f)));

    for (s32 i = 0; i < sim_ticks; ++i) {
      Simulate(*ball, false);
    }

    ball->last_micro_tick = GetMicrosecondTick();

    ball->timestamp = timestamp;
  } else if (timestamp == 0) {
    // Ball is carried if the timestamp is zero.
    ball->timestamp = timestamp;
    ball->carrier_id = owner_id;
    ball->vel_x = ball->vel_y = 0;
    ball->last_micro_tick = GetMicrosecondTick();

    Player* carrier = player_manager.GetPlayerById(owner_id);

    if (ball->state != BallState::Carried && carrier && carrier->ship != 8) {
      ball->state = BallState::Carried;
      carrier->ball_carrier = true;

      if (carrier->id == player_manager.player_id) {
        ShipSettings& ship_settings = connection.settings.ShipSettings[carrier->ship];

        this->carry_timer = ship_settings.SoccerBallThrowTimer / 100.0f;
        this->carry_id = ball->id;

        player_manager.ship_controller->AddBombDelay(ship_settings.BombFireDelay);
        player_manager.ship_controller->AddBulletDelay(ship_settings.BombFireDelay);
      }

      Event::Dispatch(BallPickupEvent(*ball, *carrier));
    }
  }
}

bool OnMode3(const Vector2f& position, u32 frequency) {
  u32 corner = frequency % 4;

  switch (corner) {
    case 0: {
      return position.x < 512 && position.y < 512;
    } break;
    case 1: {
      return position.x >= 512 && position.y < 512;
    } break;
    case 2: {
      return position.x < 512 && position.y >= 512;
    } break;
    case 3: {
      return position.x >= 512 && position.y >= 512;
    } break;
    default: {
      return false;
    } break;
  }

  return false;
}

bool OnMode5(const Vector2f& position, u32 frequency) {
  u32 direction = frequency % 4;

  switch (direction) {
    case 0: {
      if (position.y < 512) {
        return position.x < position.y;
      }

      return position.x + position.y < 1024;
    } break;
    case 1: {
      if (position.x < 512) {
        return position.x + position.y >= 1024;
      }

      return position.x < position.y;
    } break;
    case 2: {
      if (position.x < 512) {
        return position.x >= position.y;
      }

      return position.x + position.y < 1024;
    } break;
    case 3: {
      if (position.y <= 512) {
        return position.x + position.y >= 1024;
      }

      return position.x >= position.y;
    } break;
    default: {
      return false;
    } break;
  }

  return false;
}

bool Soccer::IsTeamGoal(const Vector2f& position) {
  u32 frequency = player_manager.GetSelf()->frequency;

  switch (connection.settings.SoccerMode) {
    case 0: {
      return false;
    } break;
    case 1: {
      if (frequency & 1) {
        return position.x >= 512;
      }

      return position.x < 512;
    } break;
    case 2: {
      if (frequency & 1) {
        return position.y >= 512;
      }

      return position.y < 512;
    } break;
    case 3: {
      return OnMode3(position, frequency);
    } break;
    case 4: {
      return !OnMode3(position, frequency);
    } break;
    case 5: {
      return OnMode5(position, frequency);
    } break;
    case 6: {
      return !OnMode5(position, frequency);
    }
    default: {
    } break;
  }

  return true;
}

Vector2f Soccer::GetBallPosition(Powerball& ball, u64 microtick) const {
  if (ball.state == BallState::Carried) {
    Player* carrier = player_manager.GetPlayerById(ball.carrier_id);

    if (carrier && carrier->ship != 8) {
      Vector2f heading = OrientationToHeading((u8)(carrier->orientation * 40.0f));
      float radius = player_manager.connection.settings.ShipSettings[carrier->ship].GetRadius();

      float extension = radius - 0.25f;

      if (extension < 0) {
        extension = 0.0f;
      }

      return carrier->position.PixelRounded() + heading * extension;
    }
  }

  Vector2f current_position(ball.x / 16000.0f, ball.y / 16000.0f);
  Vector2f next_position(ball.next_x / 16000.0f, ball.next_y / 16000.0f);
  float t = (microtick - ball.last_micro_tick) / (float)kTickDurationMicro;

  return current_position * (1 - t) + (t * next_position);
}

}  // namespace zero

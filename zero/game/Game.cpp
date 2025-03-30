#include "Game.h"

#include <assert.h>
#include <stdio.h>
#include <zero/game/Clock.h>
#include <zero/game/GameEvent.h>
#include <zero/game/Logger.h>
#include <zero/game/Memory.h>
#include <zero/game/Platform.h>
#include <zero/game/Random.h>
#include <zero/game/render/Graphics.h>

namespace zero {

inline void ToggleStatus(Game* game, Player* self, ShipCapabilityFlags capability, StatusFlag status) {
  if (game->ship_controller.ship.capability & capability) {
    self->togglables ^= status;

    Event::Dispatch(ShipToggleEvent(capability, (self->togglables & status) != 0));
  }
}

static void OnAction(void* user, InputAction action) {
  Game* game = (Game*)user;
  Player* self = game->player_manager.GetSelf();

  if (!self) return;

  bool dirty_radar = false;

  switch (action) {
    case InputAction::Multifire: {
      if (game->ship_controller.ship.capability & ShipCapability_Multifire) {
        game->ship_controller.ship.multifire = !game->ship_controller.ship.multifire;

        Event::Dispatch(ShipToggleEvent(ShipCapability_Multifire, game->ship_controller.ship.multifire));
      }

    } break;
    case InputAction::Stealth: {
      ToggleStatus(game, self, ShipCapability_Stealth, Status_Stealth);

      dirty_radar = true;
    } break;
    case InputAction::Cloak: {
      ToggleStatus(game, self, ShipCapability_Cloak, Status_Cloak);

      if ((game->ship_controller.ship.capability & ShipCapability_Cloak) && !(self->togglables & Status_Cloak)) {
        self->togglables |= Status_Flash;
      }
    } break;
    case InputAction::XRadar: {
      ToggleStatus(game, self, ShipCapability_XRadar, Status_XRadar);

      if (self->ship == 8 && !game->connection.settings.NoXRadar) {
        self->togglables ^= Status_XRadar;
      }

      dirty_radar = true;
    } break;
    case InputAction::Antiwarp: {
      ToggleStatus(game, self, ShipCapability_Antiwarp, Status_Antiwarp);
    } break;
    default: {
    } break;
  }

  if (dirty_radar) {
    game->RecreateRadar();
  }
}

static void OnFlagClaimPkt(void* user, u8* pkt, size_t size) {
  Game* game = (Game*)user;

  game->OnFlagClaim(pkt, size);
}

static void OnFlagPositionPkt(void* user, u8* pkt, size_t size) {
  Game* game = (Game*)user;

  game->OnFlagPosition(pkt, size);
}

static void OnPlayerIdPkt(void* user, u8* pkt, size_t size) {
  Game* game = (Game*)user;

  game->OnPlayerId(pkt, size);
}

static void OnFlagVictoryPkt(void* user, u8* pkt, size_t size) {
  Game* game = (Game*)user;

  game->OnFlagVictory(pkt, size);
}

static void OnFlagDropPkt(void* user, u8* pkt, size_t size) {
  Game* game = (Game*)user;

  game->OnFlagDrop(pkt, size);
}

static void OnArenaListPkt(void* user, u8* pkt, size_t size) {
  NetworkBuffer buffer(pkt, kMaxPacketSize);
  Game* game = (Game*)user;

  buffer.ReadU8();  // Skip type byte

  char* current_name = (char*)buffer.read;
  bool name_parse = true;

  while (buffer.read < buffer.data + buffer.size) {
    if (name_parse) {
      if (!*buffer.read) {
        name_parse = false;
      }

      buffer.ReadU8();
    } else {
      s16 count = buffer.ReadU16();

      // The current directory is marked with negative count
      if (count < 0) {
        strcpy(game->arena_name, current_name);
        break;
      }

      current_name = (char*)buffer.read;
      name_parse = true;
    }
  }

  Event::Dispatch(ArenaNameEvent(game->arena_name));
}

static void OnArenaSettingsPkt(void* user, u8* pkt, size_t size) {
  Game* game = (Game*)user;
  game->RecreateRadar();
  game->ship_controller.UpdateSettings();
}

static void OnTurfFlagUpdatePkt(void* user, u8* pkt, size_t size) {
  Game* game = (Game*)user;

  game->OnTurfFlagUpdate(pkt, size);
}

static void OnPlayerFreqAndShipChangePkt(void* user, u8* pkt, size_t size) {
  Game* game = (Game*)user;
  NetworkBuffer buffer(pkt, size, size);

  buffer.ReadU8();

  u8 ship = buffer.ReadU8();
  u16 pid = buffer.ReadU16();
  u16 freq = buffer.ReadU16();

  game->RecreateRadar();
}

static void OnPlayerDeathPkt(void* user, u8* pkt, size_t size) {
  NetworkBuffer buffer(pkt, size, size);

  buffer.ReadU8();

  s8 green_id = buffer.ReadU8();
  u16 killer_id = buffer.ReadU16();
  u16 killed_id = buffer.ReadU16();

  Game* game = (Game*)user;

  Player* killed = game->player_manager.GetPlayerById(killed_id);
  Player* killer = game->player_manager.GetPlayerById(killer_id);

  // Only spawn greens if they are positive and the killed player has moved.
  if (green_id > 0 && killer && killed && killed->velocity != Vector2f(0, 0)) {
    game->SpawnDeathGreen(killed->position, (Prize)green_id);
  }
}

static void OnSecurityRequestPkt(void* user, u8* pkt, size_t size) {
  Game* game = (Game*)user;

  // Reset green timer so it can synchronize with other clients
  game->green_ticks = 0;
  game->last_green_tick = GetCurrentTick();
  game->last_green_collision_tick = GetCurrentTick();
}

static void OnPlayerPrizePkt(void* user, u8* pkt, size_t size) {
  NetworkBuffer buffer(pkt, size, size);
  Game* game = (Game*)user;

  buffer.ReadU8();   // type
  buffer.ReadU32();  // timestamp

  u16 x = buffer.ReadU16();
  u16 y = buffer.ReadU16();
  s16 prize_id = (s16)buffer.ReadU16();
  u16 player_id = buffer.ReadU16();

  // Loop through greens to remove any on that tile. This exists so players outside of position broadcast range will
  // still keep prize seed in sync.
  for (size_t i = 0; i < game->green_count; ++i) {
    PrizeGreen* green = game->greens + i;

    u16 green_x = (u16)green->position.x;
    u16 green_y = (u16)green->position.y;

    if (green_x == x && green_y == y) {
      game->greens[i] = game->greens[--game->green_count];
      break;
    }
  }

  if (prize_id == (s16)Prize::Warp) return;

  // Perform prize sharing
  Player* player = game->player_manager.GetPlayerById(player_id);
  if (player && player->id != game->player_manager.player_id) {
    Player* self = game->player_manager.GetSelf();

    if (self && self->ship != 8 && self->frequency == player->frequency) {
      u16 share_limit = game->connection.settings.ShipSettings[self->ship].PrizeShareLimit;

      if (self->bounty < share_limit) {
        u32 pristine_seed = game->connection.security.prize_seed;

        game->ship_controller.ApplyPrize(self, prize_id, true);
        game->connection.security.prize_seed = pristine_seed;
      }
    }
  }
}

static void OnBombDamageTaken(void* user) {
  Game* game = (Game*)user;

  game->jitter_time = game->connection.settings.JitterTime / 100.0f;
}

Game::Game(MemoryArena& perm_arena, MemoryArena& temp_arena, WorkQueue& work_queue, int width, int height)
    : perm_arena(perm_arena),
      temp_arena(temp_arena),
      work_queue(work_queue),
      animation(),
      dispatcher(),
      connection(perm_arena, temp_arena, work_queue, dispatcher),
      player_manager(perm_arena, connection, dispatcher),
      weapon_manager(temp_arena, connection, player_manager, dispatcher, animation),
      brick_manager(perm_arena, connection, player_manager, dispatcher),
      camera(Vector2f((float)width, (float)height), Vector2f(0, 0), 1.0f / 16.0f),
      ui_camera(Vector2f((float)width, (float)height), Vector2f(0, 0), 1.0f),
      fps(60.0f),
      chat(dispatcher, connection, player_manager),
      soccer(player_manager),
      ship_controller(player_manager, weapon_manager, dispatcher),
      radar(player_manager) {
  dispatcher.Register(ProtocolS2C::FlagPosition, OnFlagPositionPkt, this);
  dispatcher.Register(ProtocolS2C::FlagClaim, OnFlagClaimPkt, this);
  dispatcher.Register(ProtocolS2C::PlayerId, OnPlayerIdPkt, this);
  dispatcher.Register(ProtocolS2C::ArenaDirectoryListing, OnArenaListPkt, this);
  dispatcher.Register(ProtocolS2C::ArenaSettings, OnArenaSettingsPkt, this);
  dispatcher.Register(ProtocolS2C::TeamAndShipChange, OnPlayerFreqAndShipChangePkt, this);
  dispatcher.Register(ProtocolS2C::TurfFlagUpdate, OnTurfFlagUpdatePkt, this);
  dispatcher.Register(ProtocolS2C::PlayerDeath, OnPlayerDeathPkt, this);
  dispatcher.Register(ProtocolS2C::Security, OnSecurityRequestPkt, this);
  dispatcher.Register(ProtocolS2C::PlayerPrize, OnPlayerPrizePkt, this);

  float zmax = (float)Layer::Count;
  ui_camera.projection = Orthographic(0, ui_camera.surface_dim.x, ui_camera.surface_dim.y, 0, -zmax, zmax);

  player_manager.Initialize(&weapon_manager, &ship_controller, &chat, &radar, &soccer);
  weapon_manager.Initialize(&ship_controller, &radar);

  connection.view_dim = camera.surface_dim;

  ship_controller.explosion_report.on_damage = OnBombDamageTaken;
  ship_controller.explosion_report.user = this;
}

GameInitializeResult Game::Initialize(InputState& input) {
  input.user = this;
  input.action_callback = OnAction;

  if (render_enabled) {
    if (sprite_renderer.Initialize(perm_arena)) {
      if (Graphics::Initialize(sprite_renderer)) {
        line_renderer.Initialize();
        return GameInitializeResult::Full;
      } else {
        Log(LogLevel::Error, "Failed to initialize graphics. Debug display disabled.");
      }
    } else {
      Log(LogLevel::Error, "Failed to initialize sprite renderer. Debug display disabled.");
    }
  }

  return GameInitializeResult::Simulation;
}

bool Game::Update(const InputState& input, float dt) {
  Player* self = player_manager.GetSelf();

  Graphics::colors.Update(dt);

  connection.map.UpdateDoors(connection.settings);

  // Update ship controller before player manager so it will send the position packet with any weapons fired before the
  // player manager sends its position packet.
  ship_controller.Update(input, dt);
  player_manager.Update(dt);
  weapon_manager.Update(dt);

  soccer.Update(dt);

  chat.Update(dt);

  connection.map.brick_manager = &brick_manager;

  if (render_enabled && tile_renderer.tilemap_texture == -1 &&
      connection.login_state == Connection::LoginState::Complete) {
    if (!tile_renderer.CreateMapBuffer(temp_arena, connection.map.filename, ui_camera.surface_dim)) {
      Log(LogLevel::Error, "Failed to create renderable map.");
    }

    RecreateRadar();

    animated_tile_renderer.InitializeDoors(tile_renderer);
  }

  // Cap player and spectator camera to playable area
  if (self) {
    brick_manager.Update(connection.map, self->frequency, dt);

    if (self->position.x < 0) self->position.x = 0;
    if (self->position.y < 0) self->position.y = 0;
    if (self->position.x >= 1024) self->position.x = 1023.9f;
    if (self->position.y >= 1024) self->position.y = 1023.9f;

    camera.position = self->position.PixelRounded();

    if (jitter_time > 0) {
      float max_jitter_time = connection.settings.JitterTime / 100.0f;
      float strength = jitter_time / max_jitter_time;
      float max_jitter_distance = max_jitter_time;

      if (max_jitter_distance > 2.0f) {
        max_jitter_distance = 2.0f;
      }

      camera.position.x += sinf(GetCurrentTick() * 0.75f) * strength * max_jitter_distance;
      camera.position.y += sinf(GetCurrentTick() * 0.63f) * strength * max_jitter_distance;

      jitter_time -= dt;
    }
  }

  if (render_enabled) {
    animated_tile_renderer.Update(dt);
  } else {
    ship_controller.exhaust_count = 0;
  }

  if (self) {
    radar.Update(ui_camera, connection.settings.MapZoomFactor, self->frequency, self->id);
  }

  UpdateGreens(dt);

  u32 tick = GetCurrentTick();

  // TODO: Spatial partition queries
  for (size_t i = 0; i < flag_count; ++i) {
    GameFlag* flag = flags + i;

    if (TICK_GT(flag->hidden_end_tick, tick)) continue;
    if (!(flag->flags & GameFlag_Dropped)) continue;

    Vector2f flag_min = flag->position;
    Vector2f flag_max = flag->position + Vector2f(1, 1);

    for (size_t j = 0; j < player_manager.player_count; ++j) {
      Player* player = player_manager.players + j;

      if (player->ship == 8) continue;
      if (player->enter_delay > 0.0f) continue;
      if (player->frequency == flag->owner) continue;
      if (!player_manager.IsSynchronized(*player)) continue;

      float radius = connection.settings.ShipSettings[player->ship].GetRadius() + (1.0f / 16.0f);
      Vector2f player_min = player->position - Vector2f(radius, radius);
      Vector2f player_max = player->position + Vector2f(radius, radius);

      if (BoxBoxIntersect(flag_min, flag_max, player_min, player_max)) {
        constexpr u32 kHideFlagDelay = 300;

        if (!(flag->flags & GameFlag_Turf)) {
          flag->hidden_end_tick = MAKE_TICK(tick + kHideFlagDelay);
        }

        u32 carry = connection.settings.CarryFlags;
        bool can_carry = carry > 0 && (carry == 1 || player->flags < carry - 1);

#if 0
        u32 view_tick = connection.login_tick + connection.settings.EnterGameFlaggingDelay;
#else
        // Continuum seems to show flags immediately.
        u32 view_tick = 0;
#endif

        if (TICK_GT(tick, view_tick) && (can_carry || (flag->flags & GameFlag_Turf))) {
          if (player->id == player_manager.player_id &&
              TICK_DIFF(tick, flag->last_pickup_request_tick) >= kFlagPickupDelay) {
            // Send flag pickup
            connection.SendFlagRequest(flag->id);
            flag->last_pickup_request_tick = tick;

            Event::Dispatch(FlagPickupRequestEvent(*flag));
          }
        }
      }
    }
  }

  if (self) {
    s32 tick_diff = TICK_DIFF(tick, last_tick);

    if (self->flag_timer > 0 && tick_diff > 0) {
      s32 new_timer = self->flag_timer - tick_diff;

      if (new_timer <= 0) {
        connection.SendFlagDrop();
        self->flag_timer = 0;
        Event::Dispatch(FlagTimeoutEvent());
      } else {
        self->flag_timer = (u16)new_timer;
      }
    }

    last_tick = GetCurrentTick();
  }

  return true;
}

void Game::Render(float dt) {
  if (dt > 0) {
    fps = fps * 0.99f + (1.0f / dt) * 0.01f;
  }

  // Always update animations even when the renderer is disabled so they get removed from the active list.
  animation.Update(dt);

  if (!render_enabled) {
    sprite_renderer.push_buffer.Reset();
    sprite_renderer.texture_push_buffer.Reset();
    return;
  }

  if (connection.login_state == Connection::LoginState::Complete) {
    RenderGame(dt);
  } else {
    RenderJoin(dt);
  }

  char fps_text[32];
  sprintf(fps_text, "FPS: %d", (int)(fps + 0.5f));
  sprite_renderer.PushText(ui_camera, fps_text, TextColor::Pink, Vector2f(ui_camera.surface_dim.x, 24), Layer::TopMost,
                           TextAlignment::Right);

  sprite_renderer.Render(ui_camera);
}

void Game::RenderGame(float dt) {
  Player* self = player_manager.GetSelf();

  tile_renderer.Render(camera);

  u32 self_freq = self->frequency;

  size_t viewable_flag_count = flag_count;
  u32 tick = GetCurrentTick();
  bool hide_spec_flags = self && self->ship == 8 && connection.settings.HideFlags;

  if (hide_spec_flags || !TICK_GT(tick, connection.login_tick + connection.settings.EnterGameFlaggingDelay)) {
    viewable_flag_count = 0;
  }

  animated_tile_renderer.Render(sprite_renderer, connection.map, camera, ui_camera.surface_dim, flags,
                                viewable_flag_count, greens, green_count, self_freq, soccer);
  brick_manager.Render(camera, sprite_renderer, ui_camera.surface_dim, self_freq);
  soccer.Render(camera, sprite_renderer);

  if (self) {
    animation.Render(camera, sprite_renderer);

    u8 visibility_ship = self->ship;

    RadarVisibility radar_visibility;
    if (visibility_ship != 8) {
      radar_visibility.see_mines = connection.settings.ShipSettings[visibility_ship].SeeMines;
      radar_visibility.see_bomb_level = connection.settings.ShipSettings[visibility_ship].SeeBombLevel;
    } else {
      radar_visibility.see_mines = false;
      radar_visibility.see_bomb_level = 0;
    }

    weapon_manager.Render(camera, ui_camera, sprite_renderer, dt, radar_visibility);
    player_manager.Render(camera, sprite_renderer);

    sprite_renderer.Render(camera);

    ship_controller.Render(ui_camera, camera, sprite_renderer);

    sprite_renderer.Render(camera);

    for (size_t i = 0; i < flag_count; ++i) {
      GameFlag* flag = flags + i;

      if (flag->owner == self->frequency) {
        radar.AddTemporaryIndicator(flag->position, 0, Vector2f(2, 2), ColorType::RadarTeamFlag);
      }
    }

    radar.Render(ui_camera, sprite_renderer, tile_renderer, connection.settings.MapZoomFactor, greens, green_count);

    chat.Render(ui_camera, sprite_renderer);
  }

  sprite_renderer.Render(ui_camera);
}

void Game::RenderJoin(float dt) {
  // TODO: Moving stars during load

  sprite_renderer.Draw(ui_camera, Graphics::ship_sprites[0],
                       ui_camera.surface_dim * 0.5f - Graphics::ship_sprites[0].dimensions * 0.5f, Layer::TopMost);

  switch (connection.login_state) {
    case Connection::LoginState::EncryptionRequested:
    case Connection::LoginState::Authentication:
    case Connection::LoginState::Registering:
    case Connection::LoginState::ArenaLogin: {
      sprite_renderer.Draw(ui_camera, Graphics::ship_sprites[0],
                           ui_camera.surface_dim * 0.5f - Graphics::ship_sprites[0].dimensions * 0.5f, Layer::TopMost);

      Vector2f position(ui_camera.surface_dim.x * 0.5f, (float)(u32)(ui_camera.surface_dim.y * 0.8f));

      if (connection.packets_received > 0) {
        sprite_renderer.PushText(ui_camera, "Entering arena", TextColor::Blue, position, Layer::TopMost,
                                 TextAlignment::Center);
      } else {
        sprite_renderer.PushText(ui_camera, "Connecting to server", TextColor::Blue, position, Layer::TopMost,
                                 TextAlignment::Center);
      }
    } break;
    case Connection::LoginState::MapDownload: {
      char downloading[64];

      if (connection.map.compressed_size > 0) {
        int percent = (int)(connection.packet_sequencer.huge_chunks.size * 100 / (float)connection.map.compressed_size);
        sprintf(downloading, "Downloading level: %d%%", percent);
      } else {
        sprintf(downloading, "Downloading level: %d bytes", (int)(connection.packet_sequencer.huge_chunks.size));
      }

      Vector2f download_pos(ui_camera.surface_dim.x * 0.5f, ui_camera.surface_dim.y * 0.8f);

      sprite_renderer.PushText(ui_camera, downloading, TextColor::Blue, download_pos, Layer::TopMost,
                               TextAlignment::Center);
      sprite_renderer.Render(ui_camera);
    } break;
    case Connection::LoginState::Quit:
    case Connection::LoginState::ConnectTimeout: {
      sprite_renderer.Draw(ui_camera, Graphics::ship_sprites[0],
                           ui_camera.surface_dim * 0.5f - Graphics::ship_sprites[0].dimensions * 0.5f, Layer::TopMost);

      Vector2f position(ui_camera.surface_dim.x * 0.5f, (float)(u32)(ui_camera.surface_dim.y * 0.8f));

      sprite_renderer.PushText(ui_camera, "Failed to connect to server", TextColor::DarkRed, position, Layer::TopMost,
                               TextAlignment::Center);
    } break;
    default: {
    } break;
  }
}

void Game::UpdateGreens(float dt) {
  u32 tick = GetCurrentTick();

  if (TICK_GT(tick, last_green_collision_tick)) {
    Player* self = player_manager.GetSelf();

    // TODO: Should probably speed this up with an acceleration structure. It's only done once a tick so it's not
    // really important to speed up.
    for (size_t i = 0; i < player_manager.player_count; ++i) {
      Player* player = player_manager.players + i;

      if (player->ship == 8) continue;
      if (player->enter_delay > 0) continue;
      if (!player_manager.IsSynchronized(*player)) continue;

      float radius = connection.settings.ShipSettings[player->ship].GetRadius();

      Vector2f pmin = player->position - Vector2f(radius, radius);
      Vector2f pmax = player->position + Vector2f(radius, radius);

      for (size_t j = 0; j < green_count; ++j) {
        PrizeGreen* green = greens + j;

        Vector2f gmin = green->position;
        Vector2f gmax = gmin + Vector2f(1, 1);

        if (green->end_tick > 0 && BoxBoxIntersect(pmin, pmax, gmin, gmax)) {
          if (player == self) {
            // Pick up green
            ship_controller.ApplyPrize(self, green->prize_id, true);
            connection.SendTakeGreen((u16)green->position.x, (u16)green->position.y, green->prize_id);

            Event::Dispatch(GreenPickupEvent(*green));
          }

          // Set the end tick to zero so it gets automatically removed next update
          green->end_tick = 0;
        }
      }
    }

    last_green_collision_tick = tick;
  }

  for (size_t i = 0; i < green_count; ++i) {
    PrizeGreen* green = greens + i;

    if (tick > green->end_tick) {
      greens[i--] = greens[--green_count];
    }
  }

  if (connection.security.prize_seed == 0) return;

  s32 tick_count = TICK_DIFF(tick, last_green_tick);
  if (tick_count <= 0) return;

  size_t max_greens = (connection.settings.PrizeFactor * player_manager.player_count) / 1000;
  if (max_greens > ZERO_ARRAY_SIZE(greens)) {
    max_greens = ZERO_ARRAY_SIZE(greens);
  }

  u16 spawn_extent =
      connection.settings.MinimumVirtual + connection.settings.UpgradeVirtual * (u16)player_manager.player_count;

  if (spawn_extent < 3) {
    spawn_extent = 3;
  } else if (spawn_extent > 1024) {
    spawn_extent = 1024;
  }

  for (s32 i = 0; i < tick_count; ++i) {
    if (++green_ticks >= (u32)connection.settings.PrizeDelay) {
      for (s32 j = 0; j < connection.settings.PrizeHideCount; ++j) {
        VieRNG rng = {(s32)connection.security.prize_seed};

        u16 x = (u16)((rng.GetNext() % (spawn_extent - 2)) + 1 + ((1024 - spawn_extent) / 2));
        u16 y = (u16)((rng.GetNext() % (spawn_extent - 2)) + 1 + ((1024 - spawn_extent) / 2));

        connection.security.prize_seed = rng.seed;

        s32 prize_id = ship_controller.GeneratePrize(true);

        rng.seed = (s32)connection.security.prize_seed;

        u32 duration_rng = rng.GetNext();

        connection.security.prize_seed = rng.seed;

        // Insert prize if it's valid and in an empty map space
        if (prize_id != 0 && green_count < max_greens && connection.map.GetTileId(x, y) == 0) {
          PrizeGreen* green = greens + green_count++;

          green->position.x = (float)x;
          green->position.y = (float)y;
          green->prize_id = prize_id;

          s16 exist_diff = (connection.settings.PrizeMaxExist - connection.settings.PrizeMinExist);

          u32 duration = (duration_rng % (exist_diff + 1)) + connection.settings.PrizeMinExist;
          green->end_tick = tick + duration;
        }
      }

      green_ticks = 0;
    }

    last_green_tick = tick;
  }
}

void Game::SpawnDeathGreen(const Vector2f& position, Prize prize) {
  if (green_count >= kMaxGreenCount) return;

  PrizeGreen* green = greens + green_count++;

  green->position = position;
  green->prize_id = (s32)prize;
  green->end_tick = GetCurrentTick() + connection.settings.DeathPrizeTime;
}

void Game::RecreateRadar() {
  if (connection.login_state != Connection::LoginState::Complete) {
    return;
  }

  mapzoom = connection.settings.MapZoomFactor;

  if (render_enabled) {
    if (!tile_renderer.CreateRadar(temp_arena, connection.map, ui_camera.surface_dim, mapzoom, soccer)) {
      Log(LogLevel::Error, "Failed to create radar.");
    }
  }
}

void Game::OnFlagClaim(u8* pkt, size_t size) {
  u16 id = *(u16*)(pkt + 1);
  u16 player_id = *(u16*)(pkt + 3);

  assert(id < ZERO_ARRAY_SIZE(flags));

  Player* player = player_manager.GetPlayerById(player_id);

  if (!player) return;

  u16 self_id = player_manager.GetSelf()->id;

  if (!(flags[id].flags & GameFlag_Turf)) {
    bool was_dropped = flags[id].flags & GameFlag_Dropped;

    flags[id].flags &= ~GameFlag_Dropped;

    player->flags++;

    if (was_dropped && player->id == self_id) {
      player->flag_timer = connection.settings.FlagDropDelay;
    }

    Event::Dispatch(FlagPickupEvent(flags[id], *player));
  } else {
    flags[id].owner = player->frequency;

    Event::Dispatch(FlagTurfClaimEvent(flags[id], *player));
  }
}

void Game::OnFlagPosition(u8* pkt, size_t size) {
  u16 id = *(u16*)(pkt + 1);
  u16 x = *(u16*)(pkt + 3);
  u16 y = *(u16*)(pkt + 5);
  u16 owner = *(u16*)(pkt + 7);

  assert(id < ZERO_ARRAY_SIZE(flags));
  if (id >= ZERO_ARRAY_SIZE(flags)) return;

  if (id + 1 > (u16)flag_count) {
    flag_count = id + 1;
  }

  flags[id].id = id;
  flags[id].owner = owner;
  flags[id].position = Vector2f((float)x, (float)y);
  flags[id].flags |= GameFlag_Dropped;
  flags[id].hidden_end_tick = 0;

  Event::Dispatch(FlagSpawnEvent(flags[id]));
}

void Game::OnFlagVictory(u8* pkt, size_t size) {
  auto self = player_manager.GetSelf();
  if (self) {
    self->flags = 0;
  }

  if (size < 3) return;

  u16 team = *(u16*)(pkt + 1);

  Event::Dispatch(FlagVictoryEvent(team));
}

void Game::OnFlagDrop(u8* pkt, size_t size) {
  if (size < 3) return;

  u16 pid = *(u16*)(pkt + 1);

  auto player = player_manager.GetPlayerById(pid);
  if (!player) return;

  player->flags = 0;
}

void Game::OnTurfFlagUpdate(u8* pkt, size_t size) {
  NetworkBuffer buffer(pkt, size, size);

  buffer.ReadU8();

  if (connection.login_state != Connection::LoginState::Complete) return;

  u16 id = 0;
  while (buffer.read < buffer.write) {
    u16 team = buffer.ReadU16();

    AnimatedTileSet& tileset = connection.map.GetAnimatedTileSet(AnimatedTile::Flag);

    assert(id < tileset.count);

    Tile* tile = &tileset.tiles[id];

    if (id + 1 > (u16)flag_count) {
      flag_count = id + 1;
    }

    flags[id].id = id;
    flags[id].owner = team;
    flags[id].position = Vector2f((float)tile->x, (float)tile->y);
    flags[id].flags |= GameFlag_Dropped | GameFlag_Turf;
    flags[id].hidden_end_tick = 0;

    Event::Dispatch(FlagTurfUpdateEvent(flags[id]));

    ++id;
  }
}

void Game::OnPlayerId(u8* pkt, size_t size) {
  Cleanup();

  flag_count = 0;
  green_count = 0;

  memset(flags, 0, sizeof(flags));

  soccer.Clear();

  if (render_enabled) {
    if (!sprite_renderer.Initialize(perm_arena)) {
      Log(LogLevel::Error, "Failed to initialize sprite renderer. Debug display disabled.");
      render_enabled = false;
      return;
    }

    if (!tile_renderer.Initialize()) {
      Log(LogLevel::Error, "Failed to initialize tile renderer. Debug display disabled.");
      render_enabled = false;
      return;
    }

    if (!Graphics::Initialize(sprite_renderer)) {
      Log(LogLevel::Error, "Failed to initialize graphics. Debug display disabled.");
      render_enabled = false;
      return;
    }

    animated_tile_renderer.Initialize();
  }
}

void Game::Cleanup() {
  connection.security_solver.ClearWork();
  brick_manager.Clear();
  work_queue.Clear();

  if (render_enabled) {
    sprite_renderer.Cleanup();
    tile_renderer.Cleanup();
  }
}

}  // namespace zero

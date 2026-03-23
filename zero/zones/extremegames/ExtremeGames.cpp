#include "ExtremeGames.h"

#include <zero/BotController.h>
#include <zero/Utility.h>
#include <zero/ZeroBot.h>
#include <zero/game/GameEvent.h>
#include <zero/game/Logger.h>
#include <zero/zones/ZoneController.h>
#include <zero/zones/extremegames/BaseBehavior.h>
#include <zero/zones/extremegames/CenterBehavior.h>

#include <string_view>

namespace zero {
namespace eg {

struct ExtremeGamesController : ZoneController {
  bool IsZone(Zone zone) override {
    bot->execute_ctx.blackboard.Erase("eg");
    eg = nullptr;
    return zone == Zone::ExtremeGames;
  }

  void CreateBehaviors(const char* arena_name) override;

  std::unique_ptr<ExtremeGames> eg;
};

static ExtremeGamesController controller;

void ExtremeGamesController::CreateBehaviors(const char* arena_name) {
  Log(LogLevel::Info, "Registering eg behaviors.");

  eg = std::make_unique<ExtremeGames>();
  eg->CreateBases(*bot);

  bot->execute_ctx.blackboard.Set("eg", eg.get());

  auto& repo = bot->bot_controller->behaviors;

  repo.Add("center", std::make_unique<CenterBehavior>());
  repo.Add("base", std::make_unique<BaseBehavior>());

  SetBehavior("center");

  bot->bot_controller->energy_tracker.estimate_type = EnergyHeuristicType::Average;
}

static MapCoord GenerateRandomSpawn(ZeroBot& bot) {
  MapCoord spawn;

  Connection& connection = bot.game->connection;
  size_t spawn_count = 0;

  for (size_t i = 0; i < ZERO_ARRAY_SIZE(connection.settings.SpawnSettings); ++i) {
    if (connection.settings.SpawnSettings[i].X != 0 || connection.settings.SpawnSettings[i].Y != 0 ||
        connection.settings.SpawnSettings[i].Radius != 0) {
      ++spawn_count;
    }
  }

  float ship_radius = connection.settings.ShipSettings[0].GetRadius();
  u32 rand_seed = rand();

  u32 player_count = (u32)bot.game->player_manager.player_count;

  if (spawn_count == 0) {
    // Default position to center of map if no location could be found.
    spawn = MapCoord(512, 512);

    for (size_t i = 0; i < 100; ++i) {
      u16 x = 0;
      u16 y = 0;

      switch (connection.settings.RadarMode) {
        case 1:
        case 3: {
          VieRNG rng = {(s32)rand_seed};
          u8 rng_x = (u8)rng.GetNext();
          u8 rng_y = (u8)rng.GetNext();

          x = rng_x;
          y = rng_y + 0x100;
        } break;
        case 2:
        case 4: {
          VieRNG rng = {(s32)rand_seed};
          u8 rng_x = (u8)rng.GetNext();
          u8 rng_y = (u8)rng.GetNext();

          x = rng_x;
          y = rng_y;
        } break;
        default: {
          u32 spawn_radius = (((u32)player_count / 8) * 0x2000 + 0x400) / 0x60 + 0x100;

          if (spawn_radius > (u32)connection.settings.WarpRadiusLimit) {
            spawn_radius = (u32)connection.settings.WarpRadiusLimit;
          }

          if (spawn_radius < 3) {
            spawn_radius = 3;
          }

          VieRNG rng = {(s32)rand_seed};
          x = rng.GetNext() % (spawn_radius - 2) - 9 + ((0x400 - spawn_radius) / 2) + (rand() % 0x14);
          y = rng.GetNext() % (spawn_radius - 2) - 9 + ((0x400 - spawn_radius) / 2) + (rand() % 0x14);
        } break;
      }

      Vector2f new_spawn((float)x, (float)y);

      if (connection.map.CanFit(new_spawn, ship_radius, 0)) {
        spawn = new_spawn;
        break;
      }
    }
  } else {
    u32 spawn_index = 0;

    float x_center = (float)connection.settings.SpawnSettings[spawn_index].X;
    float y_center = (float)connection.settings.SpawnSettings[spawn_index].Y;
    int radius = connection.settings.SpawnSettings[spawn_index].Radius;

    if (x_center == 0) {
      x_center = 512;
    } else if (x_center < 0) {
      x_center += 1024;
    }
    if (y_center == 0) {
      y_center = 512;
    } else if (y_center < 0) {
      y_center += 1024;
    }

    // Default to exact center in the case that a random position wasn't found
    spawn = Vector2f(x_center, y_center);

    if (radius > 0) {
      // Try 100 times to spawn in a random spot.
      for (int i = 0; i < 100; ++i) {
        u32 xrand = ((u32)rand());
        u32 yrand = ((u32)rand());

        float x_offset = (float)((int)(xrand % (radius * 2)) - radius);
        float y_offset = (float)((int)(yrand % (radius * 2)) - radius);

        Vector2f new_spawn(x_center + x_offset, y_center + y_offset);

        if (connection.map.CanFit(new_spawn, ship_radius, 0)) {
          spawn = new_spawn;
          break;
        }
      }
    }
  }

  return spawn;
}

void ExtremeGames::CreateBases(ZeroBot& bot) {
  MapBuildConfig cfg = {};

  MapCoord spawn = GenerateRandomSpawn(bot);
  cfg.spawn = spawn;

  std::vector<Vector2f> waypoints;

  auto opt_waypoints = bot.config->GetString("ExtremeGames", "Waypoints");
  if (opt_waypoints) {
    std::string_view waypoint_str = *opt_waypoints;
    std::vector<std::string_view> split_waypoints = SplitString(waypoint_str, ";");

    for (std::string_view str : split_waypoints) {
      // Chop off front of string to get start of waypoint integer.
      while (!str.empty()) {
        char front = str[0];
        str = str.substr(1);
        if (front == '{') {
          break;
        }
      }
      if (str.empty()) continue;

      size_t separator_pos = str.find(',');
      if (separator_pos == std::string_view::npos) continue;

      int first = strtol(str.data(), nullptr, 10);
      int second = strtol(str.data() + separator_pos + 1, nullptr, 10);

      waypoints.emplace_back((float)first, (float)second);
    }
  } else {
    waypoints.emplace_back(450.0f, 460.0f);
    waypoints.emplace_back(575.0f, 460.0f);
    waypoints.emplace_back(575.0f, 560.0f);
    waypoints.emplace_back(450.0f, 560.0f);
  }

  bot.execute_ctx.blackboard.Set("base_center_waypoints", waypoints);

  auto opt_base_count = bot.config->GetInt("ExtremeGames", "BaseCount");
  if (opt_base_count) {
    cfg.base_count = *opt_base_count;
  } else {
    cfg.base_count = 1;
  }

  cfg.populate_flood_map = true;

  auto opt_empty_exit_range = bot.config->GetInt("ExtremeGames", "EmptyExitRange");
  if (opt_empty_exit_range) {
    cfg.empty_exit_range = *opt_empty_exit_range;
  } else {
    cfg.empty_exit_range = 25;
  }

  // TODO: Add config option for specifying bases manually to reduce startup work. Or just store the results once per
  // map.

  this->bases = FindBases(*bot.bot_controller->pathfinder, cfg);
  this->base_states.resize(this->bases.size());

  for (MapBase& base : this->bases) {
    base.path = bot.bot_controller->pathfinder->FindPath(bot.game->GetMap(), base.entrance_position,
                                                         base.flagroom_position, 14.0f / 16.0f, 0xFFFF);
    if (!base.path.points.empty()) {
      // Move the entrance a bit inside so we are definitely in the base.
      size_t new_entrance_index = (size_t)(base.path.points.size() * 0.15f);
      base.entrance_position = base.path.points[new_entrance_index];
    }
  }
}

void ExtremeGames::UpdateBaseState(ZeroBot& bot) {
  for (size_t i = 0; i < base_states.size(); ++i) {
    BaseState& state = base_states[i];

    state.flag_controlling_carried_count = 0;
    state.flag_controlling_dropped_count = 0;
    state.controlling_freq = 0xFFFF;
    state.attacking_penetration_percent = 0.0f;
    state.defending_penetration_percent = 1.0f;
    state.player_data.clear();
  }

  std::vector<float> best_positions(base_states.size());

  PlayerManager& pm = bot.game->player_manager;
  Player* self = pm.GetSelf();

  if (!self) return;

  float enter_delay = (float)bot.game->connection.settings.EnterDelay;

  for (size_t i = 0; i < pm.player_count; ++i) {
    Player* player = pm.players + i;

    if (player->ship >= 8) continue;
    // Skip player if they are dead and outside of attach time.
    if (player->enter_delay > 0.0f && player->enter_delay <= enter_delay) continue;

    size_t base_index = GetBaseFromPosition(player->position);
    if (base_index >= bases.size() || base_index >= base_states.size()) continue;

    MapBase& base = bases[base_index];
    BaseState& state = base_states[base_index];

    // Store how far through the base the player is. 0.0f is at the entrance and 1.0f is farthest point into flag room.
    float traverse_percent = GetBasePenetrationPercent(base_index, player->position);

    state.player_data.push_back(PlayerBaseState{player->id, player->frequency, traverse_percent});

    if (player->id == self->id) {
      state.self_penetration_percent = traverse_percent;
    }

    if (traverse_percent > best_positions[base_index]) {
      state.controlling_freq = player->frequency;
      best_positions[base_index] = traverse_percent;
    }
  }

  for (size_t i = 0; i < base_states.size(); ++i) {
    BaseState& state = base_states[i];

    for (auto& player_state : state.player_data) {
      auto player = pm.GetPlayerById(player_state.player_id);

      if (!player) continue;

      bool control_player = player->frequency == state.controlling_freq;

      if (!control_player && player_state.position_percent > state.attacking_penetration_percent) {
        state.attacking_penetration_percent = player_state.position_percent;
      } else if (control_player && player_state.position_percent < state.defending_penetration_percent) {
        state.defending_penetration_percent = player_state.position_percent;
      }

      if (player->flags > 0) {
        if (control_player) {
          state.flag_controlling_carried_count += player->flags;
        } else {
          state.flag_attacking_carried_count += player->flags;
        }
      }
    }
  }

  for (size_t i = 0; i < bot.game->flag_count; ++i) {
    GameFlag* flag = bot.game->flags + i;

    if (flag->flags & GameFlag_Dropped) {
      size_t base_index = GetBaseFromPosition(flag->position);

      if (base_index < base_states.size()) {
        if (flag->owner == base_states[base_index].controlling_freq) {
          ++base_states[base_index].flag_controlling_dropped_count;
        } else if (flag->owner == 0xFFFF) {
          ++base_states[base_index].flag_unclaimed_dropped_count;
        } else {
          ++base_states[base_index].flag_attacking_dropped_count;
        }
      }
    }
  }
}

}  // namespace eg
}  // namespace zero

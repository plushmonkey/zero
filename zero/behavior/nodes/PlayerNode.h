#pragma once

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

namespace zero {
namespace behavior {

struct PlayerEnergyQueryNode : public BehaviorNode {
  PlayerEnergyQueryNode(const char* output_key) : output_key(output_key) {}
  PlayerEnergyQueryNode(const char* player_key, const char* output_key)
      : player_key(player_key), output_key(output_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    Player* player = ctx.bot->game->player_manager.GetSelf();

    if (player_key) {
      auto opt_player = ctx.blackboard.Value<Player*>(player_key);
      if (!opt_player) return ExecuteResult::Failure;
      player = *opt_player;
    }

    if (!player) return ExecuteResult::Failure;

    float energy = ctx.bot->bot_controller->energy_tracker.GetEnergy(*player);

    ctx.blackboard.Set(output_key, energy);

    return ExecuteResult::Success;
  }

  const char* player_key = nullptr;
  const char* output_key = nullptr;
};

struct PlayerNearPositionNode : public behavior::BehaviorNode {
  PlayerNearPositionNode(const char* player_key, const char* position_key, float near_distance)
      : player_key(player_key), position_key(position_key), near_distance_sq(near_distance * near_distance) {}
  PlayerNearPositionNode(const char* player_key, Vector2f position, float near_distance)
      : player_key(player_key), position(position), near_distance_sq(near_distance * near_distance) {}
  PlayerNearPositionNode(const char* position_key, float near_distance)
      : position_key(position_key), near_distance_sq(near_distance * near_distance) {}
  PlayerNearPositionNode(Vector2f position, float near_distance)
      : position(position), near_distance_sq(near_distance * near_distance) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto player = ctx.bot->game->player_manager.GetSelf();

    if (player_key) {
      auto opt_player = ctx.blackboard.Value<Player*>(player_key);
      if (!opt_player.has_value()) return ExecuteResult::Failure;

      player = opt_player.value();
    }

    if (!player) return ExecuteResult::Failure;

    Vector2f position = this->position;

    if (position_key) {
      auto opt_position = ctx.blackboard.Value<Vector2f>(position_key);
      if (!opt_position.has_value()) return ExecuteResult::Failure;

      position = opt_position.value();
    }

    bool near = player->position.DistanceSq(position) < near_distance_sq;

    return near ? behavior::ExecuteResult::Success : behavior::ExecuteResult::Failure;
  }

  const char* player_key = nullptr;
  const char* position_key = nullptr;

  float near_distance_sq = 0.0f;
  Vector2f position;
};

struct PlayerFrequencyQueryNode : public BehaviorNode {
  PlayerFrequencyQueryNode(const char* output_key) : output_key(output_key) {}
  PlayerFrequencyQueryNode(const char* player_key, const char* output_key)
      : player_key(player_key), output_key(output_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    auto player = ctx.bot->game->player_manager.GetSelf();

    if (player_key) {
      auto opt_player = ctx.blackboard.Value<Player*>(player_key);
      if (!opt_player) return ExecuteResult::Failure;
      player = *opt_player;
    }

    if (!player) return ExecuteResult::Failure;

    ctx.blackboard.Set(output_key, player->frequency);

    return ExecuteResult::Success;
  }

  const char* player_key = nullptr;
  const char* output_key = nullptr;
};

struct PlayerChangeFrequencyNode : public behavior::BehaviorNode {
  PlayerChangeFrequencyNode(u16 frequency) : frequency_key(nullptr), frequency(frequency) {}
  PlayerChangeFrequencyNode(const char* frequency_key) : frequency_key(frequency_key), frequency(0) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    u16 frequency = this->frequency;

    if (frequency_key) {
      auto opt_freq = ctx.blackboard.Value<u16>(frequency_key);
      if (!opt_freq.has_value()) return ExecuteResult::Failure;

      frequency = opt_freq.value();
    }

    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self) return ExecuteResult::Failure;

    if (self->frequency == frequency) return ExecuteResult::Success;

    ctx.bot->game->connection.SendFrequencyChange(frequency);

    return ExecuteResult::Success;
  }

  const char* frequency_key;
  u16 frequency;
};

struct PlayerFrequencyCountQueryNode : public behavior::BehaviorNode {
  PlayerFrequencyCountQueryNode(const char* output_key) : player_key(nullptr), output_key(output_key) {}
  PlayerFrequencyCountQueryNode(const char* player_key, const char* output_key)
      : player_key(player_key), output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    Player* player = ctx.bot->game->player_manager.GetSelf();

    if (player_key != nullptr) {
      auto player_opt = ctx.blackboard.Value<Player*>(player_key);
      if (!player_opt.has_value()) return behavior::ExecuteResult::Failure;

      player = player_opt.value();
    }

    if (!player) return behavior::ExecuteResult::Failure;

    auto& player_man = ctx.bot->game->player_manager;
    size_t count = 0;
    u16 freq = player->frequency;

    for (size_t i = 0; i < player_man.player_count; ++i) {
      if (freq == player_man.players[i].frequency) {
        ++count;
      }
    }

    ctx.blackboard.Set(output_key, count);

    return behavior::ExecuteResult::Success;
  }

  const char* player_key;
  const char* output_key;
};

struct PlayerBoundingBoxQueryNode : public behavior::BehaviorNode {
  PlayerBoundingBoxQueryNode(const char* output_key, float radius_multiplier = 1.0f)
      : player_key(nullptr), output_key(output_key), radius_multiplier(radius_multiplier) {}
  PlayerBoundingBoxQueryNode(const char* player_key, const char* output_key, float radius_multiplier = 1.0f)
      : player_key(player_key), output_key(output_key), radius_multiplier(radius_multiplier) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    Player* player = ctx.bot->game->player_manager.GetSelf();

    if (player_key != nullptr) {
      auto player_opt = ctx.blackboard.Value<Player*>(player_key);
      if (!player_opt.has_value()) return behavior::ExecuteResult::Failure;

      player = player_opt.value();
    }

    if (!player) return behavior::ExecuteResult::Failure;
    if (player->ship >= 8) return behavior::ExecuteResult::Failure;

    float radius = ctx.bot->game->connection.settings.ShipSettings[player->ship].GetRadius() * radius_multiplier;

    Rectangle bounds;

    bounds.min = player->position - Vector2f(radius, radius);
    bounds.max = player->position + Vector2f(radius, radius);

    ctx.blackboard.Set(output_key, bounds);

    return behavior::ExecuteResult::Success;
  }

  const char* player_key;
  const char* output_key;
  float radius_multiplier;
};

struct PlayerStatusQueryNode : public behavior::BehaviorNode {
  PlayerStatusQueryNode(StatusFlag status) : player_key(nullptr), status(status) {}
  PlayerStatusQueryNode(const char* player_key, StatusFlag status) : player_key(player_key), status(status) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    Player* player = ctx.bot->game->player_manager.GetSelf();

    if (player_key != nullptr) {
      auto player_opt = ctx.blackboard.Value<Player*>(player_key);
      if (!player_opt.has_value()) return behavior::ExecuteResult::Failure;

      player = player_opt.value();
    }

    if (!player) return behavior::ExecuteResult::Failure;

    if (player->togglables & status) {
      return behavior::ExecuteResult::Success;
    }

    return behavior::ExecuteResult::Failure;
  }

  const char* player_key;
  StatusFlag status;
};

struct PlayerEnergyPercentThresholdNode : public behavior::BehaviorNode {
  PlayerEnergyPercentThresholdNode(float threshold) : player_key(nullptr), threshold(threshold) {}
  PlayerEnergyPercentThresholdNode(const char* player_key, float threshold)
      : player_key(player_key), threshold(threshold) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    Player* player = ctx.bot->game->player_manager.GetSelf();

    if (player_key) {
      auto player_opt = ctx.blackboard.Value<Player*>(player_key);
      if (!player_opt.has_value()) return behavior::ExecuteResult::Failure;

      player = player_opt.value();
    }

    if (!player) return behavior::ExecuteResult::Failure;

    float percent = player->energy / (float)ctx.bot->game->ship_controller.ship.energy;

    return percent >= threshold ? behavior::ExecuteResult::Success : behavior::ExecuteResult::Failure;
  }

  const char* player_key;
  float threshold;
};

struct PlayerCurrentEnergyQueryNode : public behavior::BehaviorNode {
  PlayerCurrentEnergyQueryNode(const char* output_key) : output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    Player* player = ctx.bot->game->player_manager.GetSelf();

    if (!player) return ExecuteResult::Failure;

    ctx.blackboard.Set(output_key, player->energy);

    return ExecuteResult::Success;
  }

  const char* output_key = nullptr;
};

struct PlayerPositionQueryNode : public behavior::BehaviorNode {
  PlayerPositionQueryNode(const char* position_key) : player_key(nullptr), position_key(position_key) {}
  PlayerPositionQueryNode(const char* player_key, const char* position_key)
      : player_key(player_key), position_key(position_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    Player* player = ctx.bot->game->player_manager.GetSelf();

    if (player_key) {
      auto player_opt = ctx.blackboard.Value<Player*>(player_key);
      if (!player_opt.has_value()) return behavior::ExecuteResult::Failure;

      player = player_opt.value();
    }

    if (!player) return behavior::ExecuteResult::Failure;

    ctx.blackboard.Set(position_key, player->position);

    return behavior::ExecuteResult::Success;
  }

  const char* player_key;
  const char* position_key;
};

struct PlayerHeadingQueryNode : public behavior::BehaviorNode {
  PlayerHeadingQueryNode(const char* output_key) : player_key(nullptr), output_key(output_key) {}
  PlayerHeadingQueryNode(const char* player_key, const char* output_key)
      : player_key(player_key), output_key(output_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    Player* player = ctx.bot->game->player_manager.GetSelf();

    if (player_key) {
      auto player_opt = ctx.blackboard.Value<Player*>(player_key);
      if (!player_opt.has_value()) return behavior::ExecuteResult::Failure;

      player = player_opt.value();
    }

    if (!player) return behavior::ExecuteResult::Failure;

    ctx.blackboard.Set(output_key, player->GetHeading());

    return behavior::ExecuteResult::Success;
  }

  const char* player_key;
  const char* output_key;
};

}  // namespace behavior
}  // namespace zero

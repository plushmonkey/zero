#pragma once

#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/zones/trenchwars/TrenchWars.h>

namespace zero {
namespace tw {

#if TW_RENDER_FR

struct RenderFlagroomNode : public behavior::BehaviorNode {
  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto& world_camera = ctx.bot->game->camera;
    auto opt_tw = ctx.blackboard.Value<TrenchWars*>("tw");
    if (!opt_tw) return behavior::ExecuteResult::Failure;

    auto tw = *opt_tw;
    bool render = false;

    for (auto& pos : tw->fr_positions) {
      Vector2f start(pos.x, pos.y);
      Vector2f end(pos.x + 1.0f, pos.y + 1.0f);
      Vector3f color(0.0f, 1.0f, 0.0f);

      ctx.bot->game->line_renderer.PushRect(start, end, color);
      render = true;
    }

    if (render) {
      ctx.bot->game->line_renderer.Render(world_camera);
    }

    return behavior::ExecuteResult::Success;
  }
};

#endif

// Determines if we are the best player to be claiming a flag.
// It's not perfect since it is distance, not path distance, but it's fast.
struct BestFlagClaimerNode : public behavior::BehaviorNode {
  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto& pm = ctx.bot->game->player_manager;
    auto self = pm.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    Player* best_player = nullptr;
    float best_dist_sq = 1024.0f * 1024.0f;

    Vector2f flag_position = ctx.blackboard.ValueOr("tw_flag_position", Vector2f(512, 269));

    // Loop over players to find a teammate that can be attached to that is closer to the flag room than us.
    for (size_t i = 0; i < pm.player_count; ++i) {
      Player* player = pm.players + i;

      if (player->ship >= 8) continue;
      if (player->frequency != self->frequency) continue;

      float dist_sq = player->position.DistanceSq(flag_position);
      if (dist_sq < best_dist_sq) {
        best_dist_sq = dist_sq;
        best_player = player;
      }
    }

    if (best_player && best_player->id == self->id) {
      return behavior::ExecuteResult::Success;
    }

    return behavior::ExecuteResult::Failure;
  }
};

// This looks for a good position to sit inside the flag room and aim.
struct FlagroomAimPositionNode : public behavior::BehaviorNode {
  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    Vector2f aim_position(512, 273);

    if (ctx.bot->game->GetMap().CastTo(self->position, aim_position, self->frequency).hit) {
      return behavior::ExecuteResult::Success;
    }

    return behavior::ExecuteResult::Success;
  }
};

struct InFlagroomNode : public behavior::BehaviorNode {
  InFlagroomNode(const char* position_key) : position_key(position_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto opt_tw = ctx.blackboard.Value<TrenchWars*>("tw");
    if (!opt_tw) return behavior::ExecuteResult::Failure;

    auto opt_position = ctx.blackboard.Value<Vector2f>(position_key);
    if (!opt_position) return behavior::ExecuteResult::Failure;

    bool in_fr = (*opt_tw)->fr_bitset.Test(*opt_position);

    return in_fr ? behavior::ExecuteResult::Success : behavior::ExecuteResult::Failure;
  }

  const char* position_key = nullptr;
};

// Returns success if the target player has some number of teammates within the flag room.
struct FlagroomPresenceNode : public behavior::BehaviorNode {
  FlagroomPresenceNode(u32 count) : count_check(count) {}
  FlagroomPresenceNode(const char* count_key) : count_key(count_key) {}
  FlagroomPresenceNode(const char* count_key, const char* player_key) : count_key(count_key), player_key(player_key) {}
  FlagroomPresenceNode(u32 count, const char* player_key) : count_check(count), player_key(player_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto& pm = ctx.bot->game->player_manager;
    auto player = pm.GetSelf();

    if (player_key) {
      auto opt_player = ctx.blackboard.Value<Player*>(player_key);
      if (!opt_player) return behavior::ExecuteResult::Failure;

      player = *opt_player;
    }

    if (!player) return behavior::ExecuteResult::Failure;

    u32 count = 0;
    u32 count_threshold = count_check;

    if (count_key) {
      auto opt_count_threshold = ctx.blackboard.Value<u32>(count_key);
      if (!opt_count_threshold) return behavior::ExecuteResult::Failure;

      count_threshold = *opt_count_threshold;
    }

    auto opt_tw = ctx.blackboard.Value<TrenchWars*>("tw");
    if (!opt_tw) return behavior::ExecuteResult::Failure;

    const auto& bitset = (*opt_tw)->fr_bitset;

    for (size_t i = 0; i < pm.player_count; ++i) {
      Player* check_player = pm.players + i;

      if (check_player->ship >= 8) continue;
      if (check_player->frequency != player->frequency) continue;
      if (check_player->enter_delay > 0.0f) continue;
      if (!bitset.Test(check_player->position)) continue;

      if (++count > count_threshold) {
        return behavior::ExecuteResult::Success;
      }
    }

    return behavior::ExecuteResult::Failure;
  }

  u32 count_check = 0;
  const char* player_key = nullptr;
  const char* count_key = nullptr;
};

}  // namespace tw
}  // namespace zero

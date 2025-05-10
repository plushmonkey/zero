#pragma once

#include <zero/BotController.h>
#include <zero/RegionRegistry.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>
#include <zero/game/Logger.h>

namespace zero {
namespace nexus {

struct teamList {
  Player* player;
  float distance;

 teamList(Player* p, float& d) : player(p), distance(d) {}
  
  // Sort by distance
  bool operator < (const teamList& team) const { 
      return (distance < team.distance); 
  }
};

//Returns nearest teammate, optionally if factor is included can provide 2nd nearest temmate or 3rd, etc. as int 1 (for 1st closest), 2 for (2nd closest), etc.
  struct NearestTeammateNode : public behavior::BehaviorNode {
    NearestTeammateNode(const char* player_key) : player_key(player_key) {}
    NearestTeammateNode(const char* player_key, size_t player_factor)
        : player_key(player_key), player_factor(player_factor) {}

    behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
      Player* self = ctx.bot->game->player_manager.GetSelf();
      if (!self) return behavior::ExecuteResult::Failure;

      if (!player_factor) {
        player_factor = 0;
      } else {
        player_factor = player_factor - 1;  //since index starts at 0 if we want the 1st closest teammate we pass a 1, 2 for 2nd closest, etc.
      }

      Player* nearest = GetNearestTeammate(*ctx.bot->game, *self, *ctx.bot->bot_controller->region_registry, player_factor);

      if (!nearest) return behavior::ExecuteResult::Failure;

      ctx.blackboard.Set(player_key, nearest);

      return behavior::ExecuteResult::Success;
    }

   private:
    Player* GetNearestTeammate(Game& game, Player& self, RegionRegistry& region_registry, size_t& player_factor) {
      Player* teammate = nullptr;
      std::vector<teamList> team;

      float closest_dist_sq = std::numeric_limits<float>::max();

      for (size_t i = 0; i < game.player_manager.player_count; ++i) {
        Player* player = game.player_manager.players + i;

        if (player->name == self.name) continue;
        if (player->ship >= 8) continue;
        if (player->frequency != self.frequency) continue;
        if (player->IsRespawning()) continue;
        if (player->position == Vector2f(0, 0)) continue;
        if (!IsSynchronized(game, *player)) continue;
        if (!region_registry.IsConnected(self.position, player->position)) continue;

        bool in_safe = game.connection.map.GetTileId(player->position) == kTileIdSafe;
        if (in_safe) continue;

        float dist_sq = player->position.DistanceSq(self.position);
        
        std::vector<teamList> teammate = {
            {player, dist_sq},
        };

        //Insert the player and their distance
        team.insert(team.end(), teammate.begin(), teammate.end());
      }
      // Sort the team based on distance then we can return the desired player based on the provided factor
      std::sort(team.begin(), team.end());
     
      //Get the desired teammte by factor
     teammate = team[player_factor].player;
     
     return teammate;
  }

  inline bool IsSynchronized(Game& game, Player& player) {
    // If the player is within our view, but we haven't received any packets, then they left where we last saw them and
    // should be ignored.
    if (game.radar.InRadarView(player.position)) {
      return game.player_manager.IsSynchronized(player);
    }

    // Try to path to where we last saw the player until their old position is in view.
    return true;
  }

  size_t player_factor = 0;
  const char* player_key = nullptr;
};

}  // namespace nexus
}  // namespace zero

#pragma once

#include <zero/BotController.h>
#include <zero/RegionRegistry.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>
#include <zero/game/Logger.h>
#include <variant>

namespace zero {
namespace nexus {

    struct teamList {
        Player* player;
        float distance;
        bool operator<(const teamList& other) const {
          return distance < other.distance;  // Sort based on 'distance' in ascending order
        }
        teamList(Player* player, float distance) : player(player), distance(distance) {}
    };


//Returns nearest teammate, optionally if factor is included can provide 2nd nearest temmate or 3rd, etc. as int 1 (for 1st closest), 2 for (2nd closest), etc.
  struct NearestTeammateNode : public behavior::BehaviorNode {
    NearestTeammateNode(const char* player_key) : player_key(player_key) {}
    NearestTeammateNode(const char* player_key, size_t player_factor)
        : player_key(player_key), player_factor(player_factor) {}

    behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
      Player* self = ctx.bot->game->player_manager.GetSelf();
      if (!self) return behavior::ExecuteResult::Failure;

      Player* nearest = GetNearestTeammate(*ctx.bot->game, *self, *ctx.bot->bot_controller->region_registry, player_factor);

      if (!nearest) return behavior::ExecuteResult::Failure;

      ctx.blackboard.Set(player_key, nearest);

      return behavior::ExecuteResult::Success;
    }

   private:
    Player* GetNearestTeammate(Game& game, Player& self, RegionRegistry& region_registry, size_t& player_factor) {
      Player* best_teammate = nullptr;
      std::vector<teamList> team;
      size_t teamsize = 0;

      for (size_t i = 0; i < game.player_manager.player_count; ++i) {
        Player* player = game.player_manager.players + i;

        if (player->id == self.id) continue;
        if (player->ship >= 8) continue;
        if (player->frequency != self.frequency) continue;
        if (player->IsRespawning()) continue;
        if (player->position == Vector2f(0, 0)) continue;
        if (!IsSynchronized(game, *player)) continue;
        if (!region_registry.IsConnected(self.position, player->position)) continue;

        bool in_safe = game.connection.map.GetTileId(player->position) == kTileIdSafe;
        if (in_safe) continue;

        float dist_sq = player->position.DistanceSq(self.position);
        
        //Insert the player and their distance
        team.emplace_back(player, dist_sq);
      }
      
      teamsize = team.size();
      
      //If no teamsize will return null leading to failed execute result
      if (teamsize >= 1) {
        // Sort the team based on distance then we can return the desired player based on the provided factor
        std::sort(team.begin(), team.end());

        // Index starts from 0 not 1
        size_t index = player_factor - 1;  //Assuming this sorted correctly and they are in order by distance 

        if (index > teamsize - 1) {
          index = teamsize - 1;
        }
        //Try the specified player otherwise return the furthest player (assuming if they specified the 3rd player and they no longer exist you'd get the 2nd player)
        try {
          best_teammate = team[index].player;
        } catch (const std::out_of_range&) { }   
      } 

      return best_teammate;

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

  size_t player_factor = 1;
  const char* player_key = nullptr;
};

}  // namespace nexus
}  // namespace zero

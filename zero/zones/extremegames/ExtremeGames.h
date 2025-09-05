#pragma once

#include <zero/MapBase.h>

#include <vector>

namespace zero {

struct ZeroBot;

namespace eg {

struct PlayerBaseState {
  PlayerId player_id;
  float position_percent;
};

// Each base gets some data computed about it every update so we can query quickly in the behavior tree.
struct BaseState {
  // Each player present in the base gets data here.
  std::vector<PlayerBaseState> player_data;

  // How far through the base the farthest attacker is.
  float attacking_penetration_percent;

  // How far through the base we are. 0.0f being entrance 1.0f being flagroom.
  float self_penetration_percent;

  // How many flags are dropped for the team that controls the base.
  u32 flag_controlling_dropped_count;
  // How many flags are carried for the team that controls the base.
  u32 flag_controlling_carried_count;

  // How many flags are dropped for the team that doesn't control the base.
  u32 flag_attacking_dropped_count;
  // How many flags are carried for the team that doesn't control the base.
  u32 flag_attacking_carried_count;

  // How many flags are dropped that aren't claimed.
  u32 flag_unclaimed_dropped_count;

  // This is the frequency that has the deepest player into the base.
  u16 controlling_freq;

  inline bool IsEnemyControlled(u16 team_freq) const {
    return controlling_freq != 0xFFFF && controlling_freq != team_freq;
  }

  inline u32 GetDefendingFlagCount() const {
    return flag_controlling_dropped_count + flag_controlling_carried_count + flag_unclaimed_dropped_count;
  }
};

struct ExtremeGames {
  std::vector<MapBase> bases;
  std::vector<BaseState> base_states;

  size_t GetBaseFromPosition(Vector2f position) {
    struct Coord {
      u16 x;
      u16 y;
      Coord(u16 x, u16 y) : x(x), y(y) {}
    };
    constexpr float kRadius = 14.0f / 16.0f;

    // Check surrounding us so standing on a diagonal-tile won't cause us to think we aren't in a base.
    Coord check_coords[] = {
        Coord((u16)(position.x - kRadius), (u16)(position.y - kRadius)),
        Coord((u16)(position.x + kRadius), (u16)(position.y - kRadius)),
        Coord((u16)(position.x - kRadius), (u16)(position.y + kRadius)),
        Coord((u16)(position.x + kRadius), (u16)(position.y + kRadius)),
    };

    for (size_t i = 0; i < bases.size(); ++i) {
      auto& base = bases[i];

      for (Coord check : check_coords) {
        if (base.bitset.Test(check.x, check.y)) return i;
      }
    }

    return -1;
  }

  void UpdateBaseState(ZeroBot& bot);
};

}  // namespace eg
}  // namespace zero

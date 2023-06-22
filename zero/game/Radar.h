#ifndef ZERO_RADAR_H_
#define ZERO_RADAR_H_

#include <zero/Math.h>
#include <zero/Types.h>

namespace zero {

struct Camera;
struct Player;
struct PlayerManager;
struct PrizeGreen;

struct Radar {
  PlayerManager& player_manager;

  Radar(PlayerManager& player_manager) : player_manager(player_manager) {}

  void Update(Camera& ui_camera, short map_zoom, u16 team_freq, u16 spec_id);

  bool InRadarView(const Vector2f& position);

 private:
  struct Context {
    Vector2f radar_position;
    Vector2f radar_dim;
    Vector2f world_min;
    Vector2f world_max;
    Vector2f world_dim;

    Vector2f min_uv;
    Vector2f max_uv;

    float team_freq;
    u16 spec_id;
  };

  Context ctx;
};

}  // namespace zero

#endif

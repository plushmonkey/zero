#ifndef ZERO_SOCCER_H_
#define ZERO_SOCCER_H_

#include <zero/Math.h>
#include <zero/Types.h>
#include <zero/game/Clock.h>

namespace zero {

struct Camera;
struct Connection;
struct PlayerManager;
struct SpriteRenderer;

enum class BallState { World, Carried, Goal };

struct Powerball {
  u16 id;
  u16 carrier_id;

  u16 frequency;
  s16 friction_delta;

  u32 friction;

  u32 x;
  u32 y;

  s16 vel_x;
  s16 vel_y;

  u32 next_x;
  u32 next_y;

  u32 timestamp;
  u64 last_micro_tick;

  u32 last_touch_timestamp;
  s32 trail_delay;

  BallState state;
};

constexpr u16 kInvalidBallId = 0xFFFF;

enum class BallFireMethod { Gun, Bomb, Warp };

struct Soccer {
  PlayerManager& player_manager;
  Connection& connection;

  float anim_t = 0.0f;
  u32 last_pickup_request = 0;
  float carry_timer = 0.0f;
  u16 carry_id = kInvalidBallId;

  Powerball balls[8];

  Soccer(PlayerManager& player_manager);

  void Render(Camera& camera, SpriteRenderer& renderer);
  void RenderIndicator(Powerball& ball, const Vector2f& position);

  void Update(float dt);
  void Simulate(Powerball& ball, bool drop_trail);
  bool FireBall(BallFireMethod method);

  void Clear();

  void OnPowerballPosition(u8* pkt, size_t size);

  bool IsTeamGoal(const Vector2f& position);

  inline bool IsCarryingBall() { return carry_id != kInvalidBallId; }

  Vector2f GetBallPosition(Powerball& ball, u64 microtick) const;
};

}  // namespace zero

#endif

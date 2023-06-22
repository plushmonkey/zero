#include "Radar.h"

#include <math.h>
#include <stdio.h>
#include <time.h>
#include <zero/game/Camera.h>
#include <zero/game/Clock.h>
#include <zero/game/Game.h>
#include <zero/game/PlayerManager.h>

namespace zero {

constexpr float kRadarBorder = 6.0f;

void Radar::Update(Camera& ui_camera, short map_zoom, u16 team_freq, u16 spec_id) {
  Player* self = player_manager.GetSelf();
  if (!self) return;

  if (map_zoom < 1) map_zoom = 1;

  s16 dim = ((((u16)ui_camera.surface_dim.x / 6) / 4) * 8) / 2;
  u32 full_dim = ((u32)ui_camera.surface_dim.x * 8) / map_zoom;

  // Use the same method as Continuum to generate visible texture
  u32 ivar8 = ((u32)ui_camera.surface_dim.x / 6) + ((u32)ui_camera.surface_dim.x >> 0x1F);
  s32 ivar5 = full_dim;
  u32 ivar6 = (u32)(self->position.y * 16) * ivar5;
  u32 ivar4 = ((ivar8 >> 2) - (ivar8 >> 0x1F)) * 8 * 4;

  ivar8 = (ivar4 + (ivar4 >> 0x1F & 7U)) >> 3;
  ivar4 = (u32)(self->position.x * 16) * ivar5;

  s32 texture_min_x = ((s32)(ivar4 + (ivar4 >> 0x1F & 0x3FFFU)) >> 0xE) - ivar8 / 2;
  s32 texture_min_y = ((s32)(ivar6 + (ivar6 >> 0x1F & 0x3FFFU)) >> 0xE) - ivar8 / 2;

  ivar5 = ivar5 - ivar8;

  if (texture_min_x < 0) {
    texture_min_x = 0;
  } else if (ivar5 < texture_min_x) {
    texture_min_x = ivar5;
  }

  if (texture_min_y < 0) {
    texture_min_y = 0;
  } else if (ivar5 < texture_min_y) {
    texture_min_y = ivar5;
  }

  s32 texture_max_x = texture_min_x + ivar8;
  s32 texture_max_y = texture_min_y + ivar8;

  ctx.radar_dim = Vector2f(dim, dim);
  ctx.radar_position = ui_camera.surface_dim - Vector2f(dim, dim) - Vector2f(kRadarBorder, kRadarBorder);
  ctx.min_uv = Vector2f(texture_min_x / (float)full_dim, texture_min_y / (float)full_dim);
  ctx.max_uv = Vector2f(texture_max_x / (float)full_dim, texture_max_y / (float)full_dim);

  ctx.world_min = Vector2f(ctx.min_uv.x * 1024.0f, ctx.min_uv.y * 1024.0f);
  ctx.world_max = Vector2f(ctx.max_uv.x * 1024.0f, ctx.max_uv.y * 1024.0f);
  ctx.world_dim = ctx.world_max - ctx.world_min;
  ctx.team_freq = team_freq;
  ctx.spec_id = spec_id;
}

bool Radar::InRadarView(const Vector2f& position) {
  return BoxContainsPoint(ctx.world_min, ctx.world_max, position);
}

}  // namespace zero

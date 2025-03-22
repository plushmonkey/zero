#include "Map.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <zero/game/ArenaSettings.h>
#include <zero/game/BrickManager.h>
#include <zero/game/Clock.h>
#include <zero/game/GameEvent.h>
#include <zero/game/Logger.h>
#include <zero/game/PlayerManager.h>
#include <zero/game/net/Connection.h>

namespace zero {

inline static bool CornerPointCheck(const Map& map, int sX, int sY, int diameter, u32 frequency) {
  for (int y = 0; y < diameter; ++y) {
    for (int x = 0; x < diameter; ++x) {
      uint16_t world_x = (uint16_t)(sX + x);
      uint16_t world_y = (uint16_t)(sY + y);

      if (map.IsSolid(world_x, world_y, frequency)) {
        return false;
      }
    }
  }
  return true;
}

bool Map::CanTraverse(const Vector2f& start, const Vector2f& end, float radius, u32 frequency) const {
  if (!CanOverlapTile(start, radius, frequency)) return false;
  if (!CanOverlapTile(end, radius, frequency)) return false;

  Vector2f cross = Perpendicular(Normalize(start - end));

  bool left_solid = IsSolid(start + cross, frequency);
  bool right_solid = IsSolid(start - cross, frequency);

  if (left_solid) {
    for (float i = 0; i < radius * 2.0f; ++i) {
      if (!CanOverlapTile(start - cross * i, radius, frequency)) {
        return false;
      }

      if (!CanOverlapTile(end - cross * i, radius, frequency)) {
        return false;
      }
    }

    return true;
  }

  if (right_solid) {
    for (float i = 0; i < radius * 2.0f; ++i) {
      if (!CanOverlapTile(start + cross * i, radius, frequency)) {
        return false;
      }

      if (!CanOverlapTile(end + cross * i, radius, frequency)) {
        return false;
      }
    }

    return true;
  }

  return true;
}

bool Map::CanOverlapTile(const Vector2f& position, float radius, u32 frequency) const {
  u16 d = (u16)(radius * 2.0f);
  u16 start_x = (u16)position.x;
  u16 start_y = (u16)position.y;

  u16 far_left = start_x - d;
  u16 far_right = start_x + d;
  u16 far_top = start_y - d;
  u16 far_bottom = start_y + d;

  // Handle wrapping that can occur from using unsigned short
  if (far_left > 1023) far_left = 0;
  if (far_right > 1023) far_right = 1023;
  if (far_top > 1023) far_top = 0;
  if (far_bottom > 1023) far_bottom = 1023;

  bool solid = IsSolidEmptyDoors(start_x, start_y, frequency);
  if (d < 1 || solid) return !solid;

  // Loop over the entire check region and move in the direction of the check tile.
  // This makes sure that the check tile is always contained within the found region.
  for (u16 check_y = far_top; check_y <= far_bottom; ++check_y) {
    s16 dir_y = (start_y - check_y) > 0 ? 1 : (start_y == check_y ? 0 : -1);

    // Skip cardinal directions because the radius is >1 and must be found from a corner region.
    if (dir_y == 0) continue;

    for (u16 check_x = far_left; check_x <= far_right; ++check_x) {
      s16 dir_x = (start_x - check_x) > 0 ? 1 : (start_x == check_x ? 0 : -1);

      if (dir_x == 0) continue;

      bool can_fit = true;

      for (s16 y = check_y; std::abs(y - check_y) <= d && can_fit; y += dir_y) {
        for (s16 x = check_x; std::abs(x - check_x) <= d; x += dir_x) {
          if (IsSolidEmptyDoors(x, y, frequency)) {
            can_fit = false;
            break;
          }
        }
      }

      if (can_fit) {
        return true;
      }
    }
  }

  return false;
}

bool Map::CanOccupy(const Vector2f& position, float radius, u32 frequency) const {
  u16 position_x = (u16)position.x;
  u16 position_y = (u16)position.y;

  int radius_check = (int)(radius + 0.5f);

  for (int y = -radius_check; y <= radius_check; ++y) {
    for (int x = -radius_check; x <= radius_check; ++x) {
      uint16_t world_x = (uint16_t)(position_x + x);
      uint16_t world_y = (uint16_t)(position_y + y);

      if (IsSolid(world_x, world_y, frequency)) {
        return false;
      }
    }
  }

  return true;
#if 0
  /* Convert the ship into a tiled grid and put each tile of the ship on the test
     position.

     If the ship can get any part of itself on the tile return true.
  */
  if (IsSolid(position, frequency)) {
    return false;
  }

  // casting the result to int always rounds towards 0
  int tile_diameter = (int)((radius + 0.5f) * 2);

  if (tile_diameter == 0) {
    return true;
  }

  for (int y = -(tile_diameter - 1); y <= 0; ++y) {
    for (int x = -(tile_diameter - 1); x <= 0; ++x) {
      if (CornerPointCheck(*this, (int)position.x + x, (int)position.y + y, tile_diameter, frequency)) {
        return true;
      }
    }
  }
  return false;
#endif
}

bool Map::CanOccupyRadius(const Vector2f& position, float radius, u32 frequency) const {
  // rounds 2 tile ships to a 3 tile search

  if (IsSolid(position, frequency)) {
    return false;
  }

  radius = floorf(radius + 0.5f);

  for (float y = -radius; y <= radius; ++y) {
    for (float x = -radius; x <= radius; ++x) {
      uint16_t world_x = (uint16_t)(position.x + x);
      uint16_t world_y = (uint16_t)(position.y + y);
      if (IsSolid(world_x, world_y, frequency)) {
        return false;
      }
    }
  }
  return true;
}

OccupyRect Map::GetClosestOccupyRect(Vector2f position, float radius, Vector2f point) const {
  OccupyRect result = {};

  u16 d = (u16)(radius * 2.0f);
  u16 start_x = (u16)position.x;
  u16 start_y = (u16)position.y;

  u16 far_left = start_x - d;
  u16 far_right = start_x + d;
  u16 far_top = start_y - d;
  u16 far_bottom = start_y + d;

  result.occupy = false;

  // Handle wrapping that can occur from using unsigned short
  if (far_left > 1023) far_left = 0;
  if (far_right > 1023) far_right = 1023;
  if (far_top > 1023) far_top = 0;
  if (far_bottom > 1023) far_bottom = 1023;

  bool solid = IsSolid(start_x, start_y, 0xFFFF);
  if (d < 1 || solid) {
    result.occupy = !solid;
    result.start_x = (u16)position.x;
    result.start_y = (u16)position.y;
    result.end_x = (u16)position.x;
    result.end_y = (u16)position.y;

    return result;
  }

  float best_distance_sq = 1000.0f;

  // Loop over the entire check region and move in the direction of the check tile.
  // This makes sure that the check tile is always contained within the found region.
  for (u16 check_y = far_top; check_y <= far_bottom; ++check_y) {
    s16 dir_y = (start_y - check_y) > 0 ? 1 : (start_y == check_y ? 0 : -1);

    // Skip cardinal directions because the radius is >1 and must be found from a corner region.
    if (dir_y == 0) continue;

    for (u16 check_x = far_left; check_x <= far_right; ++check_x) {
      s16 dir_x = (start_x - check_x) > 0 ? 1 : (start_x == check_x ? 0 : -1);

      if (dir_x == 0) continue;

      bool can_fit = true;

      for (s16 y = check_y; std::abs(y - check_y) <= d && can_fit; y += dir_y) {
        for (s16 x = check_x; std::abs(x - check_x) <= d; x += dir_x) {
          if (IsSolid(x, y, 0xFFFF)) {
            can_fit = false;
            break;
          }
        }
      }

      if (can_fit) {
        u16 found_start_x = 0;
        u16 found_start_y = 0;
        u16 found_end_x = 0;
        u16 found_end_y = 0;

        if (check_x > start_x) {
          found_start_x = check_x - d;
          found_end_x = check_x;
        } else {
          found_start_x = check_x;
          found_end_x = check_x + d;
        }

        if (check_y > start_y) {
          found_start_y = check_y - d;
          found_end_y = check_y;
        } else {
          found_start_y = check_y;
          found_end_y = check_y + d;
        }

        bool use_rect = true;

        // Already have an occupied rect, see if this one is better.
        if (result.occupy) {
          Vector2f start(found_start_x, found_start_y);
          Vector2f end(found_end_x, found_end_y);

          Vector2f center = (end + start) * 0.5f;
          float dist_sq = center.DistanceSq(point);

          use_rect = dist_sq < best_distance_sq;
        }

        // Check if this occupy rect contains the search position. Use it if it does.
        bool contains_point =
            BoxContainsPoint(Vector2f(found_start_x, found_start_y), Vector2f(found_end_x, found_end_y), point);

        if (contains_point || use_rect) {
          result.start_x = found_start_x;
          result.start_y = found_start_y;
          result.end_x = found_end_x;
          result.end_y = found_end_y;

          result.occupy = true;
        }

        if (contains_point) {
          return result;
        }
      }
    }
  }

  return result;
}

OccupyRect Map::GetPossibleOccupyRect(const Vector2f& position, float radius, u32 frequency) const {
  OccupyRect result = {};

  u16 d = (u16)(radius * 2.0f);
  u16 start_x = (u16)position.x;
  u16 start_y = (u16)position.y;

  u16 far_left = start_x - d;
  u16 far_right = start_x + d;
  u16 far_top = start_y - d;
  u16 far_bottom = start_y + d;

  result.occupy = false;

  // Handle wrapping that can occur from using unsigned short
  if (far_left > 1023) far_left = 0;
  if (far_right > 1023) far_right = 1023;
  if (far_top > 1023) far_top = 0;
  if (far_bottom > 1023) far_bottom = 1023;

  bool solid = IsSolid(start_x, start_y, frequency);
  if (d < 1 || solid) {
    result.occupy = !solid;
    result.start_x = (u16)position.x;
    result.start_y = (u16)position.y;
    result.end_x = (u16)position.x;
    result.end_y = (u16)position.y;

    return result;
  }

  // Loop over the entire check region and move in the direction of the check tile.
  // This makes sure that the check tile is always contained within the found region.
  for (u16 check_y = far_top; check_y <= far_bottom; ++check_y) {
    s16 dir_y = (start_y - check_y) > 0 ? 1 : (start_y == check_y ? 0 : -1);

    // Skip cardinal directions because the radius is >1 and must be found from a corner region.
    if (dir_y == 0) continue;

    for (u16 check_x = far_left; check_x <= far_right; ++check_x) {
      s16 dir_x = (start_x - check_x) > 0 ? 1 : (start_x == check_x ? 0 : -1);

      if (dir_x == 0) continue;

      bool can_fit = true;

      for (s16 y = check_y; abs(y - check_y) <= d && can_fit; y += dir_y) {
        for (s16 x = check_x; abs(x - check_x) <= d; x += dir_x) {
          if (IsSolid(x, y, frequency)) {
            can_fit = false;
            break;
          }
        }
      }

      if (can_fit) {
        // Calculate the final region. Not necessary for simple overlap check, but might be useful
        u16 found_start_x = 0;
        u16 found_start_y = 0;
        u16 found_end_x = 0;
        u16 found_end_y = 0;

        if (check_x > start_x) {
          found_start_x = check_x - d;
          found_end_x = check_x;
        } else {
          found_start_x = check_x;
          found_end_x = check_x + d;
        }

        if (check_y > start_y) {
          found_start_y = check_y - d;
          found_end_y = check_y;
        } else {
          found_start_y = check_y;
          found_end_y = check_y + d;
        }

        result.start_x = found_start_x;
        result.start_y = found_start_y;
        result.end_x = found_end_x;
        result.end_y = found_end_y;

        result.occupy = true;
        return result;
      }
    }
  }

  return result;
}

Vector2f Map::GetOccupyCenter(const Vector2f& position, float radius, u32 frequency) const {
  u16 d = (u16)(radius * 2.0f);
  u16 start_x = (u16)position.x;
  u16 start_y = (u16)position.y;

  u16 far_left = start_x - d;
  u16 far_right = start_x + d;
  u16 far_top = start_y - d;
  u16 far_bottom = start_y + d;

  // Handle wrapping that can occur from using unsigned short
  if (far_left > 1023) far_left = 0;
  if (far_right > 1023) far_right = 1023;
  if (far_top > 1023) far_top = 0;
  if (far_bottom > 1023) far_bottom = 1023;

  bool solid = IsSolid(start_x, start_y, frequency);
  if (d < 1 || solid) {
    return position;
  }

  Vector2f accum;
  size_t count = 0;

  // Loop over the entire check region and move in the direction of the check tile.
  // This makes sure that the check tile is always contained within the found region.
  for (u16 check_y = far_top; check_y <= far_bottom; ++check_y) {
    s16 dir_y = (start_y - check_y) > 0 ? 1 : (start_y == check_y ? 0 : -1);

    // Skip cardinal directions because the radius is >1 and must be found from a corner region.
    if (dir_y == 0) continue;

    for (u16 check_x = far_left; check_x <= far_right; ++check_x) {
      s16 dir_x = (start_x - check_x) > 0 ? 1 : (start_x == check_x ? 0 : -1);

      if (dir_x == 0) continue;

      bool can_fit = true;

      for (s16 y = check_y; std::abs(y - check_y) <= d && can_fit; y += dir_y) {
        for (s16 x = check_x; std::abs(x - check_x) <= d; x += dir_x) {
          if (IsSolid(x, y, frequency)) {
            can_fit = false;
            break;
          }
        }
      }

      if (can_fit) {
        // Calculate the final region. Not necessary for simple overlap check, but might be useful
        u16 found_start_x = 0;
        u16 found_start_y = 0;
        u16 found_end_x = 0;
        u16 found_end_y = 0;

        if (check_x > start_x) {
          found_start_x = check_x - d;
          found_end_x = check_x;
        } else {
          found_start_x = check_x;
          found_end_x = check_x + d;
        }

        if (check_y > start_y) {
          found_start_y = check_y - d;
          found_end_y = check_y;
        } else {
          found_start_y = check_y;
          found_end_y = check_y + d;
        }

        Vector2f min(found_start_x, found_start_y);
        Vector2f max((float)found_end_x + 1.0f, (float)found_end_y + 1.0f);

        accum += (min + max) * 0.5f;
        ++count;
      }
    }
  }

  if (count <= 0) return position;

  return accum * (1.0f / count);
}

// Rects must be initialized memory that can contain all possible occupy rects.
size_t Map::GetAllOccupiedRects(Vector2f position, float radius, u32 frequency, OccupiedRect* rects,
                                bool dynamic_doors) const {
  size_t count = 0;

  u16 d = (u16)(radius * 2.0f);
  u16 start_x = (u16)position.x;
  u16 start_y = (u16)position.y;

  u16 far_left = start_x - d;
  u16 far_right = start_x + d;
  u16 far_top = start_y - d;
  u16 far_bottom = start_y + d;

  // Handle wrapping that can occur from using unsigned short
  if (far_left > 1023) far_left = 0;
  if (far_right > 1023) far_right = 1023;
  if (far_top > 1023) far_top = 0;
  if (far_bottom > 1023) far_bottom = 1023;

  bool solid = false;
  if (dynamic_doors) {
    solid = IsSolid(start_x, start_y, frequency);
  } else {
    solid = IsSolidEmptyDoors(start_x, start_y, frequency);
  }

  if (d < 1 || solid) {
    if (rects) {
      rects->start_x = (u16)position.x;
      rects->start_y = (u16)position.y;
      rects->end_x = (u16)position.x;
      rects->end_y = (u16)position.y;

      TileId id = GetTileId(start_x, start_y);

      rects->contains_door = id >= kTileIdFirstDoor && id <= kTileIdLastDoor;
    }

    return !solid;
  }

  // Loop over the entire check region and move in the direction of the check tile.
  // This makes sure that the check tile is always contained within the found region.
  for (u16 check_y = far_top; check_y <= far_bottom; ++check_y) {
    s16 dir_y = (start_y - check_y) > 0 ? 1 : (start_y == check_y ? 0 : -1);

    // Skip cardinal directions because the radius is >1 and must be found from a corner region.
    if (dir_y == 0) continue;

    for (u16 check_x = far_left; check_x <= far_right; ++check_x) {
      s16 dir_x = (start_x - check_x) > 0 ? 1 : (start_x == check_x ? 0 : -1);

      if (dir_x == 0) continue;

      bool can_fit = true;
      bool contains_door = false;

      for (s16 y = check_y; std::abs(y - check_y) <= d && can_fit; y += dir_y) {
        for (s16 x = check_x; std::abs(x - check_x) <= d; x += dir_x) {
          bool solid = false;

          if (dynamic_doors) {
            solid = IsSolid(x, y, frequency);
          } else {
            solid = IsSolidEmptyDoors(x, y, frequency);
          }

          if (solid) {
            can_fit = false;
            break;
          }

          TileId id = GetTileId(x, y);
          if (id >= kTileIdFirstDoor && id <= kTileIdLastDoor) {
            contains_door = true;
          }
        }
      }

      if (can_fit) {
        // Calculate the final region. Not necessary for simple overlap check, but might be useful
        u16 found_start_x = 0;
        u16 found_start_y = 0;
        u16 found_end_x = 0;
        u16 found_end_y = 0;

        if (check_x > start_x) {
          found_start_x = check_x - d;
          found_end_x = check_x;
        } else {
          found_start_x = check_x;
          found_end_x = check_x + d;
        }

        if (check_y > start_y) {
          found_start_y = check_y - d;
          found_end_y = check_y;
        } else {
          found_start_y = check_y;
          found_end_y = check_y + d;
        }

        if (rects) {
          OccupiedRect* rect = rects + count++;

          rect->start_x = found_start_x;
          rect->start_y = found_start_y;
          rect->end_x = found_end_x;
          rect->end_y = found_end_y;
          rect->contains_door = contains_door;
        } else {
          ++count;
        }
      }
    }
  }

  return count;
}

bool Map::Load(MemoryArena& arena, const char* filename) {
  assert(strlen(filename) < 1024);

  strcpy(this->filename, filename);

  FILE* file = fopen(filename, "rb");

  if (!file) {
    return false;
  }

  fseek(file, 0, SEEK_END);
  size_t size = ftell(file);
  fseek(file, 0, SEEK_SET);

  if (size <= 0) {
    fclose(file);
    return false;
  }

  // Maps are allocated in their own arena so they are freed automatically when the arena is reset
  data = (char*)arena.Allocate(size);
  tiles = arena.Allocate(1024 * 1024);

  assert(data);
  assert(tiles);

  size_t read_size = fread(data, 1, size, file);
  if (read_size != size) {
    Log(LogLevel::Warning, "Map load failed to read entire file: %s", filename);
  }
  fclose(file);

  size_t pos = 0;

  if (data[0] == 'B' && data[1] == 'M') {
    pos = *(u32*)(data + 2);
  }

  memset(tiles, 0, 1024 * 1024);

  // Expand tile data out into full grid
  size_t start = pos;

  size_t tile_count = (size - pos) / sizeof(Tile);
  Tile* tiles = (Tile*)(data + pos);

  this->door_count = GetTileCount(tiles, tile_count, kTileIdFirstDoor, kTileIdLastDoor);
  this->doors = memory_arena_push_type_count(&arena, Tile, this->door_count);

  for (size_t i = 0; i < kAnimatedTileCount; ++i) {
    animated_tiles[i].index = 0;
    animated_tiles[i].count = GetTileCount(tiles, tile_count, kAnimatedIds[i], kAnimatedIds[i]);
    animated_tiles[i].tiles = memory_arena_push_type_count(&arena, Tile, animated_tiles[i].count);
  }

  size_t door_index = 0;
  for (size_t tile_index = 0; tile_index < tile_count; ++tile_index) {
    Tile* tile = tiles + tile_index;

    this->tiles[tile->y * 1024 + tile->x] = tile->id;

    if (tile->id >= kTileIdFirstDoor && tile->id <= kTileIdLastDoor) {
      Tile* door = this->doors + door_index++;
      *door = *tile;
    }

    for (size_t i = 0; i < kAnimatedTileCount; ++i) {
      if (tile->id == kAnimatedIds[i]) {
        animated_tiles[i].tiles[animated_tiles[i].index++] = *tile;

        for (size_t j = 0; j < kAnimatedTileSizes[i]; ++j) {
          size_t y = tile->y + j;

          for (size_t k = 0; k < kAnimatedTileSizes[i]; ++k) {
            size_t x = tile->x + k;

            this->tiles[y * 1024 + x] = tile->id;
          }
        }
      }
    }
  }

  return true;
}

size_t Map::GetTileCount(Tile* tiles, size_t tile_count, TileId id_begin, TileId id_end) {
  size_t count = 0;

  for (size_t i = 0; i < tile_count; ++i) {
    Tile* tile = tiles + i;

    if (tile->id >= id_begin && tile->id <= id_end) {
      ++count;
    }
  }

  return count;
}

void Map::UpdateDoors(const ArenaSettings& settings) {
  u32 current_tick = GetCurrentTick();

  // Check if we received any settings first
  if (settings.Type == 0) return;

  s32 count = TICK_DIFF(current_tick, last_seed_tick);

  s32 delay = settings.DoorDelay;
  if (delay <= 0) delay = 1;

  count /= delay;

  if (count >= 100) {
    count = 100;
  }

  for (s32 i = 0; i < count; ++i) {
    u8 seed = door_rng.seed;

    if (settings.DoorMode == -2) {
      seed = door_rng.GetNext();
    } else if (settings.DoorMode == -1) {
      u32 table[7];

      for (size_t j = 0; j < 7; ++j) {
        table[j] = door_rng.GetNext();
      }

      table[6] &= 0x8000000F;
      if ((s32)table[6] < 0) {
        table[6] = ((table[6] - 1) | 0xFFFFFFF0) + 1;
      }
      table[6] = -(s32)(table[6] != 0) & 0x80;

      table[5] &= 0x80000007;
      if ((s32)table[5] < 0) {
        table[5] = ((table[5] - 1) | 0xFFFFFFF8) + 1;
      }
      table[5] = -(s32)(table[5] != 0) & 0x40;

      table[4] &= 0x80000003;
      if ((s32)table[4] < 0) {
        table[4] = ((table[4] - 1) | 0xFFFFFFFC) + 1;
      }
      table[4] = -(s32)(table[4] != 0) & 0x20;

      table[3] &= 0x8000000F;
      if ((s32)table[3] < 0) {
        table[3] = ((table[3] - 1) | 0xFFFFFFF0) + 1;
      }
      table[3] = -(s32)(table[3] != 0) & 0x8;

      table[2] &= 0x80000007;
      if ((s32)table[2] < 0) {
        table[2] = ((table[2] - 1) | 0xFFFFFFF8) + 1;
      }
      table[2] = -(s32)(table[2] != 0) & 0x4;

      table[1] &= 0x80000003;
      if ((s32)table[1] < 0) {
        table[1] = ((table[1] - 1) | 0xFFFFFFFC) + 1;
      }
      table[1] = -(s32)(table[1] != 0) & 0x2;

      table[0] &= 0x80000001;
      if ((s32)table[0] < 0) {
        table[0] = ((table[0] - 1) | 0xFFFFFFFE) + 1;
      }
      table[0] = -(s32)(table[0] != 0) & 0x11;

      seed = table[6] + table[5] + table[4] + table[3] + table[2] + table[1] + table[0];
    } else if (settings.DoorMode >= 0) {
      seed = (u8)settings.DoorMode;
    }

    if (settings.DoorMode < 0 && settings.DoorDelay > 0 && door_count > 0) {
      Event::Dispatch(DoorToggleEvent());
    }

    SeedDoors(seed);
    last_seed_tick += delay;
  }
}

void Map::SeedDoors(u32 seed) {
  u8 bottom = seed & 0xFF;
  u8 table[8];

  table[0] = ((~bottom & 1) << 3) | 0xA2;
  table[1] = (-((bottom & 2) != 0) & 0xF9) + 0xAA;
  table[2] = (-((bottom & 4) != 0) & 0xFA) + 0xAA;
  table[3] = (-((bottom & 8) != 0) & 0xFB) + 0xAA;
  table[4] = (-((bottom & 0x10) != 0) & 0xFC) + 0xAA;
  table[5] = (-((bottom & 0x20) != 0) & 0xFD) + 0xAA;
  table[6] = (~(bottom >> 5) & 2) | 0xA8;
  table[7] = 0xAA - ((bottom & 0x80) != 0);

  PlayerManager* player_manager = nullptr;
  Player* self = nullptr;
  Vector2f self_min;
  Vector2f self_max;

  if (brick_manager) {
    player_manager = &brick_manager->player_manager;

    if (player_manager) {
      self = player_manager->GetSelf();

      if (self) {
        float radius = player_manager->connection.settings.ShipSettings[self->ship].GetRadius();

        self_min = self->position - Vector2f(radius, radius);
        self_max = self->position + Vector2f(radius, radius);
      }
    }
  }

  for (size_t i = 0; i < door_count; ++i) {
    Tile* door = doors + i;

    u8 id = table[door->id - kTileIdFirstDoor];

    constexpr TileId kOpenDoorId = kTileIdLastDoor + 1;

    TileId previous_id = tiles[door->y * 1024 + door->x];
    tiles[door->y * 1024 + door->x] = id;

    // If the tile just changed from open to closed then check for collisions
    if (self && previous_id == kOpenDoorId && id != kOpenDoorId) {
      Vector2f door_position((float)door->x, (float)door->y);

      // Perform door warp on overlap
      if (self && BoxBoxOverlap(self_min, self_max, door_position, door_position + Vector2f(1, 1))) {
        player_manager->Spawn(false);
      }
    }
  }
}

bool Map::CanFit(const Vector2f& position, float radius, u32 frequency) const {
  for (float y_offset_check = -radius; y_offset_check < radius; ++y_offset_check) {
    for (float x_offset_check = -radius; x_offset_check < radius; ++x_offset_check) {
      if (IsSolid((u16)(position.x + x_offset_check), (u16)(position.y + y_offset_check), frequency)) {
        return false;
      }
    }
  }

  return true;
}

Vector2f Map::ResolveShipCollision(Vector2f position, float radius, u32 frequency) const {
  position = position.PixelRounded();

  Vector2f half_extents(radius, radius);
  Rectangle player_bounds(position - half_extents, position + half_extents);

  int start_y = (int)player_bounds.min.y - 1;
  int end_y = (int)player_bounds.max.y + 1;
  int start_x = (int)player_bounds.min.x - 1;
  int end_x = (int)player_bounds.max.x + 1;

  constexpr int kMaxIterations = 5;

  bool moved = true;
  for (size_t i = 0; i < kMaxIterations && moved; ++i) {
    moved = false;

    // Loop over nearby tiles to find collisions, then resolve them by msoving the position.
    for (int y = start_y; y <= end_y; ++y) {
      for (int x = start_x; x <= end_x; ++x) {
        if (IsSolid((u16)x, (u16)y, frequency)) {
          Rectangle tile_bounds(Vector2f((float)x, (float)y), Vector2f((float)x + 1.0f, (float)y + 1.0f));
          Rectangle minkowski_tile_collider = tile_bounds.Grow(half_extents);

          if (minkowski_tile_collider.ContainsExclusive(position)) {
            Vector2f center = minkowski_tile_collider.GetCenter();
            float req_dist = 0.5f + radius;
            float dx = position.x - center.x;
            float dy = position.y - center.y;

            if (fabsf(dx) > 0.5f) {
              float x_move_req = req_dist - fabsf(dx);

              if (x_move_req > 0.0f) {
                position.x += x_move_req * (signbit(dx) ? -1.0f : 1.0f);
              }
            }

            if (fabsf(dy) > 0.5f) {
              float y_move_req = req_dist - fabsf(dy);

              if (y_move_req > 0.0f) {
                position.y += y_move_req * (signbit(dy) ? -1.0f : 1.0f);
              }
            }

            moved = true;
          }
        }
      }
    }
  }

  return position.PixelRounded();
}

bool Map::IsColliding(const Vector2f& position, float radius, u32 frequency) const {
  s16 start_x = (s16)(position.x - radius - 1);
  s16 start_y = (s16)(position.y - radius - 1);

  s16 end_x = (s16)(position.x + radius + 1);
  s16 end_y = (s16)(position.y + radius + 1);

  if (start_x < 0) start_x = 0;
  if (start_y < 0) start_y = 0;

  if (end_x > 1023) end_x = 1023;
  if (end_y > 1023) end_y = 1023;

  for (s16 y = start_y; y <= end_y; ++y) {
    for (s16 x = start_x; x <= end_x; ++x) {
      if (!IsSolid(x, y, frequency)) continue;

      Rectangle tile_collider(Vector2f((float)x, (float)y), Vector2f((float)x + 1, (float)y + 1));
      Rectangle minkowski_collider = tile_collider.Grow(Vector2f(radius, radius));

      if (minkowski_collider.ContainsInclusive(position)) {
        return true;
      }
    }
  }

  return false;
}

TileId Map::GetTileId(u16 x, u16 y) const {
  if (!tiles) return 0;
  if (x >= 1024 || y >= 1024) return 20;

  return tiles[y * 1024 + x];
}

void Map::SetTileId(u16 x, u16 y, TileId id) {
  if (!tiles) return;
  if (x >= 1024 || y >= 1024) return;

  tiles[y * 1024 + x] = id;
}

TileId Map::GetTileId(const Vector2f& position) const {
  return GetTileId((u16)position.x, (u16)position.y);
}

bool Map::IsSolid(u16 x, u16 y, u32 frequency) const {
  TileId id = GetTileId(x, y);

  if (id == 250 && brick_manager) {
    Brick* brick = brick_manager->GetBrick(x, y);

    if (brick && brick->team == frequency) {
      return false;
    }
  }

  return zero::IsSolid(id);
}

bool Map::IsSolidEmptyDoors(u16 x, u16 y, u32 frequency) const {
  TileId id = GetTileId(x, y);

  if (id == 250 && brick_manager) {
    Brick* brick = brick_manager->GetBrick(x, y);

    if (brick && brick->team == frequency) {
      return false;
    }
  }

  return zero::IsSolidEmptyDoors(id);
}

u32 Map::GetChecksum(u32 key) const {
  constexpr u32 kTileStart = 1;
  constexpr u32 kTileEnd = 160;

  int basekey = key;

  for (int y = basekey % 32; y < 1024; y += 32) {
    for (int x = basekey % 31; x < 1024; x += 31) {
      u8 tile = (u8)GetTileId(x, y);

      if (tile == 250) {
        tile = 0;
      }

      if ((tile >= kTileStart && tile <= kTileEnd) || tile == kTileIdSafe) {
        key += basekey ^ tile;
      }
    }
  }

  return key;
}

CastResult Map::Cast(const Vector2f& from, const Vector2f& direction, float max_distance, u32 frequency) const {
  CastResult result;

  result.hit = false;

  Vector2f unit_step(sqrtf(1 + (direction.y / direction.x) * (direction.y / direction.x)),
                     sqrtf(1 + (direction.x / direction.y) * (direction.x / direction.y)));

  Vector2f check = Vector2f(floorf(from.x), floorf(from.y));
  Vector2f travel;

  Vector2f step;

  if (IsSolid(from, frequency)) {
    result.hit = true;
    result.distance = 0.0f;
    result.position = from;
    return result;
  }

  if (direction.x < 0) {
    step.x = -1.0f;
    travel.x = (from.x - check.x) * unit_step.x;
  } else {
    step.x = 1.0f;
    travel.x = (check.x + 1 - from.x) * unit_step.x;
  }

  if (direction.y < 0) {
    step.y = -1.0f;
    travel.y = (from.y - check.y) * unit_step.y;
  } else {
    step.y = 1.0f;
    travel.y = (check.y + 1 - from.y) * unit_step.y;
  }

  float distance = 0.0f;

  while (distance < max_distance) {
    // Walk along shortest path
    float clear_distance = distance;

    if (travel.x < travel.y) {
      check.x += step.x;
      distance = travel.x;
      travel.x += unit_step.x;
    } else {
      check.y += step.y;
      distance = travel.y;
      travel.y += unit_step.y;
    }

    if (IsSolid((unsigned short)floorf(check.x), (unsigned short)floorf(check.y), frequency)) {
      result.hit = true;
      result.distance = clear_distance;
      break;
    }
  }

  if (result.hit) {
    float dist;

    bool intersected = RayBoxIntersect(from, direction, check, Vector2f(1, 1), &dist, &result.normal);

    if (!intersected || dist > max_distance) {
      result.hit = false;
      result.position = from + direction * max_distance;
      result.distance = max_distance;
    } else {
      result.distance = dist;
      result.position = from + direction * dist;
    }
  } else {
    result.hit = false;
    result.distance = max_distance;
    result.position = from + direction * max_distance;
  }

  return result;
}

CastResult Map::CastTo(const Vector2f& from, const Vector2f& to, u32 frequency) const {
  Vector2f diff = to - from;
  float dist = diff.Length();
  Vector2f direction = Normalize(diff);

  return Cast(from, direction, dist, frequency);
}

// Loop over entire casted area to find minimal tiles to check against.
// When a solid tile is found, perform a minkowski sum so the new rect can be checked against a ray.
CastResult Map::CastShip(Player* player, float radius, const Vector2f& to) const {
  CastResult result = {};
  Vector2f trajectory = to.PixelRounded() - player->position.PixelRounded();
  Vector2f direction = Normalize(trajectory);
  float max_distance = trajectory.Length();
  u32 frequency = player->frequency;

  Vector2f sides[] = {Perpendicular(direction), -Perpendicular(direction)};

  Ray ray(player->position.PixelRounded(), direction);

  Vector2f from_start = player->position.PixelRounded();
  // Ignore 1 pixel in growth so it does exclusive check.
  Vector2f minkowski_growth(radius - 1.0f / 16.0f, radius - 1.0f / 16.0f);

  Vector2f e = to.PixelRounded();
  Vector2f s = player->position.PixelRounded();

  // Ignore any casts that end up in the current tile.
  if ((u16)e.x == (u16)s.x && (u16)e.y == (u16)s.y) return result;

  // Walk along each side and for each tile, do another walk forward along the trajectory to find solid tiles.
  for (Vector2f side : sides) {
    Vector2f from = from_start;
    Vector2f unit_step(sqrtf(1 + (side.y / side.x) * (side.y / side.x)),
                       sqrtf(1 + (side.x / side.y) * (side.x / side.y)));

    Vector2f check = Vector2f(floorf(from.x), floorf(from.y));
    Vector2f travel;
    Vector2f step;

    if (side.x < 0) {
      step.x = -1.0f;
      travel.x = (from.x - check.x) * unit_step.x;
    } else {
      step.x = 1.0f;
      travel.x = (check.x + 1 - from.x) * unit_step.x;
    }

    if (side.y < 0) {
      step.y = -1.0f;
      travel.y = (from.y - check.y) * unit_step.y;
    } else {
      step.y = 1.0f;
      travel.y = (check.y + 1 - from.y) * unit_step.y;
    }

    float distance = 0.0f;

    while (distance < radius + 1) {
      // We've reached a new tile, we should now travel along the trajectory
      {
        Vector2f traj_start = check;
        Vector2f forward_unit_step(sqrtf(1 + (direction.y / direction.x) * (direction.y / direction.x)),
                                   sqrtf(1 + (direction.x / direction.y) * (direction.x / direction.y)));

        Vector2f traj_check = Vector2f(floorf(traj_start.x), floorf(traj_start.y));
        Vector2f travel;
        Vector2f step;

        if (direction.x < 0) {
          step.x = -1.0f;
          travel.x = (traj_start.x - traj_check.x) * forward_unit_step.x;
        } else {
          step.x = 1.0f;
          travel.x = (traj_check.x + 1 - traj_start.x) * forward_unit_step.x;
        }

        if (direction.y < 0) {
          step.y = -1.0f;
          travel.y = (traj_start.y - traj_check.y) * forward_unit_step.y;
        } else {
          step.y = 1.0f;
          travel.y = (traj_check.y + 1 - traj_start.y) * forward_unit_step.y;
        }

        float distance = 0.0f;

        while (distance < max_distance) {
          if (IsSolid((unsigned short)floorf(traj_check.x), (unsigned short)floorf(traj_check.y), frequency)) {
            Vector2f top_left(floorf(traj_check.x), floorf(traj_check.y));
            Rectangle rect(top_left, top_left + Vector2f(1, 1));
            Rectangle collider = rect.Grow(minkowski_growth);

            if (RayBoxIntersect(ray, collider, &result.distance, nullptr)) {
              if (result.distance < max_distance) {
                result.hit = true;
                result.position = ray.origin + ray.direction * result.distance;
                return result;
              }
            }
          }

          if (travel.x < travel.y) {
            traj_check.x += step.x;
            distance = travel.x;
            travel.x += forward_unit_step.x;
          } else {
            traj_check.y += step.y;
            distance = travel.y;
            travel.y += forward_unit_step.y;
          }
        }
      }

      if (IsSolid((unsigned short)floorf(check.x), (unsigned short)floorf(check.y), frequency)) {
        Vector2f top_left(floorf(check.x), floorf(check.y));
        Rectangle rect(top_left, top_left + Vector2f(1, 1));
        Rectangle collider = rect.Grow(minkowski_growth);

        if (RayBoxIntersect(ray, collider, &result.distance, nullptr)) {
          if (result.distance < max_distance) {
            result.hit = true;
            result.position = ray.origin + ray.direction * result.distance;
            return result;
          }
        }
      }

      if (travel.x < travel.y) {
        check.x += step.x;
        distance = travel.x;
        travel.x += unit_step.x;
      } else {
        check.y += step.y;
        distance = travel.y;
        travel.y += unit_step.y;
      }
    }
  }

  return result;
}

}  // namespace zero

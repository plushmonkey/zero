#include "BrickManager.h"

#include <assert.h>
#include <zero/game/Buffer.h>
#include <zero/game/Camera.h>
#include <zero/game/Clock.h>
#include <zero/game/GameEvent.h>
#include <zero/game/Map.h>
#include <zero/game/PlayerManager.h>
#include <zero/game/net/Connection.h>
#include <zero/game/net/PacketDispatcher.h>

namespace zero {

static void OnBrickDroppedPkt(void* user, u8* pkt, size_t size) {
  BrickManager* manager = (BrickManager*)user;
  NetworkBuffer buffer(pkt, size, size);

  buffer.ReadU8();  // Type

  if (size < 17) {
    manager->Clear();
    return;
  }

  u16 x1 = buffer.ReadU16();
  u16 y1 = buffer.ReadU16();
  u16 x2 = buffer.ReadU16();
  u16 y2 = buffer.ReadU16();
  u16 team = buffer.ReadU16();
  u16 brick_id = buffer.ReadU16();
  u32 timestamp = buffer.ReadU32();

  u32 local_timestamp = timestamp - manager->connection.time_diff;

  Vector2f start((float)x1, (float)y1);
  Vector2f end((float)x2, (float)y2);
  Vector2f direction = Normalize(end - start);

  float distance = start.Distance(end);

  Player* self = manager->player_manager.GetSelf();
  Vector2f self_min;
  Vector2f self_max;

  if (self && self->ship != 8) {
    float radius = manager->connection.settings.ShipSettings[self->ship].GetRadius();

    self_min = self->position - Vector2f(radius, radius);
    self_max = self->position + Vector2f(radius, radius);
  }

  Vector2f position = start;
  for (float i = 0; i <= distance; ++i) {
    u16 x = (u16)position.x;
    u16 y = (u16)position.y;

    manager->InsertBrick(x, y, team, brick_id, local_timestamp);

    Vector2f brick_position((float)x, (float)y);

    // Perform brick warp on overlap
    if (self && self->frequency != team &&
        BoxBoxOverlap(self_min, self_max, brick_position, brick_position + Vector2f(1, 1))) {
      manager->player_manager.Spawn(false);
    }

    position += direction;
  }
}

BrickManager::BrickManager(MemoryArena& arena, Connection& connection, PlayerManager& player_manager,
                           PacketDispatcher& dispatcher)
    : arena(arena), connection(connection), player_manager(player_manager), brick_map(arena) {
  dispatcher.Register(ProtocolS2C::BrickDropped, OnBrickDroppedPkt, this);
}

void BrickManager::Update(Map& map, u32 frequency, float dt) {
  if (connection.login_state < Connection::LoginState::Complete) return;

  u32 tick = GetCurrentTick();

  Brick* current = bricks;
  Brick* previous = nullptr;

  while (current) {
    if (TICK_GT(tick, current->end_tick)) {
      if (previous) {
        previous->next = current->next;
      } else {
        bricks = current->next;
      }

      Brick* brick = current;
      current = current->next;

      map.SetTileId(brick->tile.x, brick->tile.y, 0);

      Event::Dispatch(BrickTileClearEvent(*brick));

      Brick** removed = brick_map.Remove(brick->tile);
      assert(removed);

      brick->next = free;
      free = brick;
    } else {
      map.SetTileId(current->tile.x, current->tile.y, 250);

      previous = current;
      current = current->next;
    }
  }
}

void BrickManager::InsertBrick(u16 x, u16 y, u16 team, u16 id, u32 timestamp) {
  Brick* brick = free;

  if (!brick) {
    brick = free = memory_arena_push_type(&arena, Brick);
    brick->next = nullptr;
  }

  free = free->next;

  brick->next = bricks;
  bricks = brick;

  brick->tile.x = x;
  brick->tile.y = y;
  brick->id = id;
  brick->team = team;
  brick->end_tick = timestamp + connection.settings.BrickTime;

  brick_map.Insert(brick->tile, brick);

  Event::Dispatch(BrickTileEvent(*brick));
}

void BrickManager::Clear() {
  Brick* current = bricks;

  while (current) {
    Brick* brick = current;
    current = current->next;

    Brick** removed = brick_map.Remove(brick->tile);
    assert(removed);

    brick->next = free;
    free = brick;
  }

  bricks = nullptr;
}

}  // namespace zero

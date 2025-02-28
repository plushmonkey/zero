#include "KDTree.h"

#include <zero/game/Memory.h>
#include <zero/game/PlayerManager.h>

namespace zero {

KDNode* FindMedian(KDNode* start, KDNode* end, size_t axis) {
  if (end <= start) return nullptr;
  if (end == start + 1) return start;

  KDNode* median = start + (end - start) / 2;

  while (true) {
    float pivot = median->position[axis];

    median->Swap(end - 1);

    KDNode* store = start;

    for (KDNode* p = start; p < end; ++p) {
      if (p->position[axis] < pivot) {
        if (p != store) {
          p->Swap(store);
        }
        ++store;
      }
    }
    store->Swap(end - 1);

    if (store->position[axis] == median->position[axis]) return median;

    if (store > median) {
      end = store;
    } else {
      start = store;
    }
  }

  return nullptr;
}

KDNode* PartitionSet(KDNode* node, size_t count, int axis) {
  if (count == 0) return nullptr;

  KDNode* n = FindMedian(node, node + count, axis);

  if (n) {
    axis ^= 1;
    n->left = PartitionSet(node, n - node, axis);
    n->right = PartitionSet(n + 1, node + count - (n + 1), axis);
  }

  return n;
}

KDNode* BuildPartition(MemoryArena& arena, PlayerManager& pm) {
  KDNode* start = memory_arena_push_type(&arena, KDNode);
  KDNode* current = start;

  u32 current_tick = GetCurrentTick();

  size_t count = 0;

  for (size_t i = 0; i < pm.player_count; ++i) {
    Player* player = pm.players + i;

    if (player->ship == 8) continue;
    if (player->enter_delay > 0) continue;
    if (!pm.IsSynchronized(*player, current_tick)) continue;

    current->player = player;
    current->position = player->position;
    current->left = nullptr;
    current->right = nullptr;

    current = memory_arena_push_type(&arena, KDNode);
    ++count;
  }

  return PartitionSet(start, count, 0);
}

}  // namespace zero

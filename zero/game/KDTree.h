#pragma once

#include <zero/Math.h>
#include <zero/game/Memory.h>
#include <algorithm>

namespace zero {

struct Player;

struct KDCollection {
  Player** players;
  size_t count;
};

struct KDNode {
  KDNode* left;
  KDNode* right;

  Player* player;
  // Store the position here to prevent pointer chase during build
  Vector2f position;

  void Swap(KDNode* other) {
    std::swap(player, other->player);
    std::swap(position, other->position);
  }

  // Looks for the node that is at least min_distance away.
  KDNode* RangeSearch(Vector2f from, float min_distance) {
    float min_dist_sq = min_distance * min_distance;

    // If we are within min_dist_sq then we must be the first node being searched, so we should return ourself.
    if (from.DistanceSq(position) < min_dist_sq) {
      return this;
    }

    // Check if our children are less than min_distance. We should return ourselves since we need to encompass the full
    // min_distance.
    if ((left && from.DistanceSq(left->position) < min_dist_sq) ||
        (right && from.DistanceSq(right->position) < min_dist_sq)) {
      return this;
    }

    KDNode* best_left = nullptr;
    KDNode* best_right = nullptr;

    if (left) {
      best_left = left->RangeSearch(from, min_distance);
    }

    if (right) {
      best_right = right->RangeSearch(from, min_distance);
    }

    float best_left_dist_sq = 1024 * 1024.0f;
    float best_right_dist_sq = 1024 * 1024.0f;

    if (best_left) {
      best_left_dist_sq = best_left->position.DistanceSq(from);
    }

    if (best_right) {
      best_right_dist_sq = best_right->position.DistanceSq(from);
    }

    // Return the side that is closest to the lookup position.
    if (best_left_dist_sq < best_right_dist_sq) return best_left;

    return best_right;
  }

  KDCollection Collect(MemoryArena& arena) {
    KDCollection result = {};

    result.players = (Player**)arena.Allocate(sizeof(Player*), 8);
    result.players[result.count++] = player;

    if (left) {
      left->CollectChildren(result, arena);
    }

    if (right) {
      right->CollectChildren(result, arena);
    }

    return result;
  }

 private:
  void CollectChildren(KDCollection& collection, MemoryArena& arena) {
    arena.Allocate(sizeof(Player*), 8);

    collection.players[collection.count++] = player;

    if (left) {
      left->CollectChildren(collection, arena);
    }

    if (right) {
      right->CollectChildren(collection, arena);
    }
  }
};

KDNode* FindMedian(KDNode* start, KDNode* end, size_t axis);
KDNode* PartitionSet(KDNode* node, size_t count, int axis);

KDNode* BuildPartition(struct MemoryArena& arena, struct PlayerManager& pm);

}  // namespace zero

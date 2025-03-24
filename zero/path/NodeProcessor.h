#pragma once

#include <zero/Types.h>
#include <zero/game/Game.h>
#include <zero/game/Map.h>
#include <zero/path/Node.h>

#include <vector>

namespace zero {

struct Game;

namespace path {

constexpr size_t kMaxNodes = 1024 * 1024;

struct EdgeSet {
  u8 set = 0;
  u8 dynamic = 0;

  inline bool IsSet(size_t index) const { return set & (1 << index); }
  void Set(size_t index) { set |= (1 << index); }
  void Erase(size_t index) { set &= ~(1 << index); }

  inline bool DynamicIsSet(size_t index) const { return dynamic & (1 << index); }
  void DynamicSet(size_t index) { dynamic |= (1 << index); }
};

// All of the coords and indexes stored in this must stay in the same order.
struct CoordOffset {
  s16 x;
  s16 y;

  CoordOffset() : x(0), y(0) {}
  CoordOffset(s16 x, s16 y) : x(x), y(y) {}

  static inline CoordOffset West() { return CoordOffset(-1, 0); }
  static inline CoordOffset East() { return CoordOffset(1, 0); }
  static inline CoordOffset North() { return CoordOffset(0, -1); }
  static inline CoordOffset NorthWest() { return CoordOffset(-1, -1); }
  static inline CoordOffset NorthEast() { return CoordOffset(1, -1); }
  static inline CoordOffset South() { return CoordOffset(0, 1); }
  static inline CoordOffset SouthWest() { return CoordOffset(-1, 1); }
  static inline CoordOffset SouthEast() { return CoordOffset(1, 1); }

  static inline CoordOffset FromIndex(size_t index) {
    static const CoordOffset kNeighbors[8] = {
        CoordOffset::North(),     CoordOffset::South(),     CoordOffset::West(),      CoordOffset::East(),
        CoordOffset::NorthWest(), CoordOffset::NorthEast(), CoordOffset::SouthWest(), CoordOffset::SouthEast()};

    return kNeighbors[index];
  }

  static inline size_t NorthIndex() { return 0; }
  static inline size_t SouthIndex() { return 1; }
  static inline size_t WestIndex() { return 2; }
  static inline size_t EastIndex() { return 3; }
  static inline size_t NorthWestIndex() { return 4; }
  static inline size_t NorthEastIndex() { return 5; }
  static inline size_t SouthWestIndex() { return 6; }
  static inline size_t SouthEastIndex() { return 7; }
};

// This determines how doors should be treated in the node processor.
enum class DoorSolidMethod {
  // This will treat all doors as solid even if they are currently open.
  AlwaysSolid,
  // This will treat all doors as open even if they are currently closed.
  AlwaysOpen,
  // This will generate the path depending on current door state.
  Dynamic
};

// Determines the node edges when using A*.
class NodeProcessor {
 public:
  NodeProcessor(Game& game) : game_(game), map_(game.connection.map) {}
  Game& GetGame() { return game_; }

  EdgeSet FindEdges(Node* node, float radius);
  EdgeSet CalculateEdges(Node* node, float radius, OccupiedRect* occupied_scratch);
  Node* GetNode(NodePoint point);
  bool IsSolid(u16 x, u16 y) { return map_.IsSolid(x, y, 0xFFFF); }

  void SetEdgeSet(u16 x, u16 y, EdgeSet set) {
    size_t index = (size_t)y * 1024 + x;
    edges_[index] = set;
  }

  // Calculate the node from the index.
  // This lets the node exist without storing its position so it fits in cache better.
  inline NodePoint GetPoint(const Node* node) const {
    size_t index = (node - &nodes_[0]);

    uint16_t world_y = (uint16_t)(index / 1024);
    uint16_t world_x = (uint16_t)(index % 1024);

    return NodePoint(world_x, world_y);
  }

  inline Node* GetNodeFromIndex(u32 index) {
    if (index >= kMaxNodes) return nullptr;
    return nodes_ + index;
  }

  inline u32 GetNodeIndex(Node* node) const { return (u32)(node - nodes_); }

  inline void SetDoorSolidMethod(DoorSolidMethod door_method) { door_method_ = door_method; }
  inline DoorSolidMethod GetDoorSolidMethod() const { return door_method_; }

  inline void SetBrickNode(s32 x, s32 y, bool exists) {
    Node* node = GetNode(NodePoint(x, y));
    if (!node) return;

    if (exists) {
      node->flags |= NodeFlag_Brick;
    } else {
      node->flags &= ~NodeFlag_Brick;
    }
  }

  // Goes through each dynamic point and marks the node as dirty.
  // This should happen on door updates.
  inline void MarkDynamicNodes() {
    for (NodePoint point : dynamic_points) {
      Node* node = GetNode(point);
      node->flags |= NodeFlag_DynamicEmpty | NodeFlag_Traversable;
    }
  }

  // This is a list of empty spaces where nearby doors could block us.
  std::vector<NodePoint> dynamic_points;

 private:
  EdgeSet edges_[kMaxNodes];
  Node nodes_[kMaxNodes];
  const Map& map_;
  Game& game_;
  DoorSolidMethod door_method_ = DoorSolidMethod::Dynamic;
};

}  // namespace path
}  // namespace zero

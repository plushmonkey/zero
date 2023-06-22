#pragma once

#include <zero/RegionRegistry.h>
#include <zero/path/NodeProcessor.h>

#include <memory>
#include <unordered_set>
#include <vector>

namespace zero {
namespace path {

template <typename T, typename Compare, typename Container = std::vector<T>>
class PriorityQueue {
 public:
  using const_iterator = typename Container::const_iterator;

  const_iterator begin() const { return container_.cbegin(); }
  const_iterator end() const { return container_.cend(); }

  void Push(T item) {
    container_.push_back(item);
    std::push_heap(container_.begin(), container_.end(), comparator_);
  }

  T Pop() {
    T item = container_.front();
    std::pop_heap(container_.begin(), container_.end(), comparator_);
    container_.pop_back();
    return item;
  }

  // sort from highest at beginning to lowest at end
  void Update() { std::make_heap(container_.begin(), container_.end(), comparator_); }

  void Clear() { container_.clear(); }
  std::size_t Size() const { return container_.size(); }
  bool Empty() const { return container_.empty(); }

 private:
  Container container_;
  Compare comparator_;
};

struct Pathfinder {
 public:
  Pathfinder(std::unique_ptr<NodeProcessor> processor, RegionRegistry& regions);
  std::vector<Vector2f> FindPath(const Map& map, const Vector2f& from, const Vector2f& to, float radius);

  const std::vector<Vector2f>& GetPath() { return path_; }
  void SetPath(std::vector<Vector2f> path) { path_ = path; }

  std::vector<Vector2f> SmoothPath(Game& game, const std::vector<Vector2f>& path, float ship_radius);

  std::vector<Vector2f> CreatePath(Game& game, Vector2f from, Vector2f to, float radius);

  void CreateMapWeights(const Map& map);

 private:
  float GetWallDistance(const Map& map, u16 x, u16 y, u16 radius);
  struct NodeCompare {
    bool operator()(const Node* lhs, const Node* rhs) const { return lhs->f > rhs->f; }
  };

  std::vector<Vector2f> path_;
  std::unique_ptr<NodeProcessor> processor_;
  RegionRegistry& regions_;
  PriorityQueue<Node*, NodeCompare> openset_;
  std::unordered_set<Node*> touched_nodes_;
};

}  // namespace path
}  // namespace zero

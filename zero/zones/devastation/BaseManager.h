#pragma once

#include <zero/RegionRegistry.h>
#include <zero/game/GameEvent.h>

#include <optional>
#include <unordered_map>

namespace zero {

struct ZeroBot;

namespace deva {

struct BasePoints {
  MapCoord first;
  MapCoord second;
};

struct BaseInfo {
  RegionIndex region;
  MapCoord spawn;
  MapCoord enemy_spawn;

  BaseInfo() : region(kUndefinedRegion) {}
};

struct BaseManager : EventHandler<RegionTileAddEvent>,
                     EventHandler<RegionBuildEvent>,
                     EventHandler<TeleportEvent>,
                     EventHandler<PlayerAttachEvent>,
                     EventHandler<PlayerFreqAndShipChangeEvent> {
  BaseInfo current_base;

  BaseManager(ZeroBot& bot) : bot(bot) {}

  inline std::optional<BasePoints> GetSpawns(RegionIndex region_index) {
    auto iter = base_points.find(region_index);

    if (iter == base_points.end()) return {};

    return std::make_optional(iter->second);
  }

  void HandleEvent(const RegionBuildEvent& event) override;
  void HandleEvent(const RegionTileAddEvent& event) override;
  void HandleEvent(const TeleportEvent& event) override;
  void HandleEvent(const PlayerAttachEvent& event) override;
  void HandleEvent(const PlayerFreqAndShipChangeEvent& event) override;

 private:
  void UpdateActiveBase(const Vector2f& position);

  ZeroBot& bot;

  std::unordered_map<RegionIndex, BasePoints> base_points;
};

}  // namespace deva
}  // namespace zero

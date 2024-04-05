#pragma once

#include <zero/Types.h>
#include <zero/game/Clock.h>
#include <zero/game/GameEvent.h>

#include <vector>

namespace zero {

struct Player;
struct PlayerManager;

enum class EnergyHeuristicType { None, Initial, Maximum, Average };

struct HeuristicEnergyData {
  float energy;
  u32 emp_ticks;
};

// Attempts to track player energy by listening for events and applying recharge.
struct HeuristicEnergyTracker : EventHandler<WeaponFireEvent>,
                                EventHandler<PlayerFreqAndShipChangeEvent>,
                                EventHandler<PlayerEnterEvent>,
                                EventHandler<PlayerDeathEvent>,
                                EventHandler<WeaponHitEvent> {
  PlayerManager& player_manager;
  Tick last_tick_time = 0;
  EnergyHeuristicType estimate_type = EnergyHeuristicType::Maximum;

  HeuristicEnergyData player_energy[65535];

  HeuristicEnergyTracker(PlayerManager& player_manager);

  float GetEnergy(Player& player) const;
  float GetEnergyPercent(Player& player) const;

  void Update();

  void HandleEvent(const WeaponFireEvent& event) override;
  void HandleEvent(const PlayerFreqAndShipChangeEvent& event) override;
  void HandleEvent(const PlayerEnterEvent& event) override;
  void HandleEvent(const PlayerDeathEvent& event) override;
  void HandleEvent(const WeaponHitEvent& event) override;
};

}  // namespace zero

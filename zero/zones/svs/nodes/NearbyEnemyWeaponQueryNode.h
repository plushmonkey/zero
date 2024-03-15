#pragma once

#include <zero/BotController.h>
#include <zero/RegionRegistry.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>

namespace zero {
namespace svs {

enum class ExtendedWeaponType {
  Mine,
};

struct WeaponTypeCombine {
  static constexpr size_t kExtendedIndexStart = 9;
  u16 set = 0;

  WeaponTypeCombine& operator|(WeaponType type) {
    if (type == WeaponType::None) return *this;
    if (type > WeaponType::Thor) return *this;

    u32 index = (u32)type;

    this->set |= (1 << index);
    return *this;
  }

  WeaponTypeCombine& operator|(ExtendedWeaponType type) {
    u32 index = (u32)type + kExtendedIndexStart;

    this->set |= (1 << index);
    return *this;
  }

  bool Contains(WeaponType type) const {
    if (type == WeaponType::None) return set == 0;
    if (type > WeaponType::Thor) return false;

    u32 index = (u32)type;

    return (this->set & (1 << index)) != 0;
  }

  bool Contains(ExtendedWeaponType type) const {
    u32 index = (u32)type + kExtendedIndexStart;

    return (this->set & (1 << index)) != 0;
  }
};

struct NearbyEnemyWeaponQueryNode : public behavior::BehaviorNode {
  NearbyEnemyWeaponQueryNode(WeaponTypeCombine types, float distance) : weapon_types(types), distance(distance) {}
  NearbyEnemyWeaponQueryNode(WeaponTypeCombine types, const char* distance_key)
      : weapon_types(types), distance_key(distance_key) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    Player* self = ctx.bot->game->player_manager.GetSelf();

    if (!self) return behavior::ExecuteResult::Failure;
    if (self->ship >= 8) return behavior::ExecuteResult::Failure;

    float check_distance = distance;
    if (distance_key != nullptr) {
      auto opt_distance = ctx.blackboard.Value<float>(distance_key);
      if (!opt_distance) return behavior::ExecuteResult::Failure;

      check_distance = *opt_distance;
    }

    float distance_sq = check_distance * check_distance;

    auto& weapon_man = ctx.bot->game->weapon_manager;
    for (size_t i = 0; i < weapon_man.weapon_count; ++i) {
      Weapon& weapon = weapon_man.weapons[i];

      if (weapon.frequency == self->frequency) continue;
      if (weapon.data.type == WeaponType::Bomb || weapon.data.type == WeaponType::ProximityBomb) {
        // Check if it's exclusively mines
        if (weapon_types.Contains(ExtendedWeaponType::Mine) && !weapon_types.Contains(WeaponType::Bomb) &&
            !weapon_types.Contains(WeaponType::ProximityBomb)) {
          if (weapon.velocity.LengthSq() > 0.0f) {
            continue;
          }
        } else {
          if (!weapon_types.Contains(weapon.data.type)) {
            continue;
          }

          Vector2f to_weapon = weapon.position - self->position;
          // Ignore any bombs that are moving away from self.
          if (Normalize(weapon.velocity).Dot(Normalize(to_weapon)) > 0.0f) {
            continue;
          }
        }
      } else {
        if (!weapon_types.Contains(weapon.data.type)) {
          continue;
        }
      }

      if (weapon.position.DistanceSq(self->position) <= distance_sq) {
        return behavior::ExecuteResult::Success;
      }
    }

    return behavior::ExecuteResult::Failure;
  }

  WeaponTypeCombine weapon_types;
  float distance = 0.0f;
  const char* distance_key = nullptr;
};

}  // namespace svs
}  // namespace zero

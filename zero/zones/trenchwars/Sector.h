#pragma once

#include <zero/Types.h>

namespace zero {
namespace tw {

enum class Sector {
  Flagroom,
  Entrance,
  Middle,
  Bottom,
  West,    // At least as high as middle, but in the west tube.
  East,    // At least as high as middle, but in the east tube.
  Roof,    // Outside of the base but higher than flagroom.
  Center,  // Outside of the base but not above roof.
};

// Returns the Sector that is directly above this Sector.
// Flagroom will return Flagroom. Center and Roof will return Bottom.
// Side areas will return Flagroom.
inline Sector GetAboveSector(Sector sector) {
  static constexpr Sector kAboveSectors[] = {
      Sector::Flagroom,  // Flagroom
      Sector::Flagroom,  // Entrance
      Sector::Entrance,  // Middle
      Sector::Middle,    // Bottom
      Sector::Flagroom,  // West
      Sector::Flagroom,  // East
      Sector::Bottom,    // Roof
      Sector::Bottom,    // Center
  };

  // Assert that our array matches Sector size and order in case it gets changed in the future.
  static_assert(sizeof(kAboveSectors) / sizeof(*kAboveSectors) == 8);
  static_assert((size_t)Sector::Flagroom == 0);
  static_assert((size_t)Sector::Entrance == 1);
  static_assert((size_t)Sector::Middle == 2);
  static_assert((size_t)Sector::Bottom == 3);
  static_assert((size_t)Sector::West == 4);
  static_assert((size_t)Sector::East == 5);
  static_assert((size_t)Sector::Roof == 6);
  static_assert((size_t)Sector::Center == 7);

  size_t index = (size_t)sector;

  if (index >= sizeof(kAboveSectors) / sizeof(*kAboveSectors)) return Sector::Bottom;

  return kAboveSectors[index];
}

// Returns the Sector that is directly below this Sector.
// Roof and Center will return Center.
// Side areas will return Bottom.
inline Sector GetBelowSector(Sector sector) {
  static constexpr Sector kBelowSectors[] = {
      Sector::Entrance,  // Flagroom
      Sector::Middle,    // Entrance
      Sector::Bottom,    // Middle
      Sector::Center,    // Bottom
      Sector::Bottom,    // West
      Sector::Bottom,    // East
      Sector::Center,    // Roof
      Sector::Center,    // Center
  };

  // Assert that our array matches Sector size and order in case it gets changed in the future.
  static_assert(sizeof(kBelowSectors) / sizeof(*kBelowSectors) == 8);
  static_assert((size_t)Sector::Flagroom == 0);
  static_assert((size_t)Sector::Entrance == 1);
  static_assert((size_t)Sector::Middle == 2);
  static_assert((size_t)Sector::Bottom == 3);
  static_assert((size_t)Sector::West == 4);
  static_assert((size_t)Sector::East == 5);
  static_assert((size_t)Sector::Roof == 6);
  static_assert((size_t)Sector::Center == 7);

  size_t index = (size_t)sector;

  if (index >= sizeof(kBelowSectors) / sizeof(*kBelowSectors)) return Sector::Bottom;

  return kBelowSectors[index];
}

// Returns true if 'sector' is above 'compare' inside the base.
// Not valid for Center and Roof since they aren't in the base.
inline bool IsSectorAbove(Sector sector, Sector compare) {
  if (compare == Sector::Flagroom) return false;

#define SBIT(s) (1 << ((size_t)(s)))

  constexpr u32 kAboveEntranceBits = SBIT(Sector::Flagroom);
  constexpr u32 kAboveMiddleBits =
      kAboveEntranceBits | SBIT(Sector::Entrance) | SBIT(Sector::West) | SBIT(Sector::East);
  constexpr u32 kAboveBottomBits = kAboveMiddleBits | SBIT(Sector::Middle);

  switch (compare) {
    case Sector::West:
    case Sector::East:
    case Sector::Entrance:
      return kAboveEntranceBits & SBIT(sector);
    case Sector::Middle:
      return kAboveMiddleBits & SBIT(sector);
    case Sector::Bottom:
      return kAboveBottomBits & SBIT(sector);
      break;
    default:
      break;
  }

#undef SBIT

  // compare must be outside the base if it wasn't handled in the switch, so if sector is inside the base, it is above.
  bool sector_is_inside_base = !(sector == Sector::Center || sector == Sector::Roof);

  return sector_is_inside_base;
}

}  // namespace tw
}  // namespace zero

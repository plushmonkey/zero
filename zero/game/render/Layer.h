#ifndef ZERO_RENDER_LAYER_H_
#define ZERO_RENDER_LAYER_H_

namespace zero {

enum class Layer {
  BelowAll,
  Background,
  AfterBackground,
  Tiles,
  AfterTiles,
  Weapons,
  AfterWeapons,
  Ships,
  AfterShips,
  Explosions,
  Gauges,
  AfterGauges,
  Chat,
  AfterChat,
  TopMost,

  Count
};

}  // namespace zero

#endif

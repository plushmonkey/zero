#ifndef ZERO_SETTINGS_H_
#define ZERO_SETTINGS_H_

#include <zero/Types.h>

namespace zero {

enum class EncryptMethod { Subspace, Continuum };
enum class WindowType { Windowed, Fullscreen, BorderlessFullscreen };

struct GameSettings {
  bool vsync;
  WindowType window_type;
  bool render_stars;
  bool debug_window = false;
  bool debug_behavior_tree = false;

  EncryptMethod encrypt_method = EncryptMethod::Continuum;

  bool sound_enabled;
  float sound_volume;
  // How many tiles outside of the screen that you can still hear sounds.
  float sound_radius_increase;

  bool notify_max_prizes;
  u16 target_bounty;

  u32 chat_namelen = 10;
};

extern GameSettings g_Settings;

}  // namespace zero

#endif

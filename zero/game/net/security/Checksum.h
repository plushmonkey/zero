#ifndef ZERO_CHECKSUM_H_
#define ZERO_CHECKSUM_H_

#include <zero/Types.h>

namespace zero {

u8 crc8(const u8* ptr, size_t len);
u32 crc32(const u8* ptr, size_t size);
u32 crc32_map(const u8* ptr, size_t size);

struct ArenaSettings;
u32 SettingsChecksum(u32 key, const ArenaSettings& settings);
u8 WeaponChecksum(const u8* data, size_t size);
u32 VieChecksum(u32 key);

}  // namespace zero

#endif

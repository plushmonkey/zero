#include "FileRequester.h"

#include <memory.h>
#include <stdio.h>
#include <zero/game/Inflate.h>
#include <zero/game/Logger.h>
#include <zero/game/Memory.h>
#include <zero/game/Platform.h>
#include <zero/game/net/Connection.h>
#include <zero/game/net/PacketDispatcher.h>
#include <zero/game/net/security/Checksum.h>

namespace zero {

extern const char* kServerName;
static const char* zone_folder = "zones";

static void GetFilePath(MemoryArena& temp_arena, char* buffer, const char* filename) {
  sprintf(buffer, "%s/%s/%s", zone_folder, kServerName, filename);

  const char* result = platform.GetStoragePath(temp_arena, buffer);

  strcpy(buffer, result);
}

static void CreateZoneFolder(MemoryArena& temp_arena) {
  char path[260];

  const char* zone_path = platform.GetStoragePath(temp_arena, zone_folder);
  platform.CreateFolder(zone_path);
  sprintf(path, "%s/%s", zone_path, kServerName);
  platform.CreateFolder(path);
}

inline static bool FileExists(MemoryArena& temp_arena, const char* filename) {
  char path[260];
  GetFilePath(temp_arena, path, filename);

  FILE* file = fopen(path, "rb");
  if (!file) return false;

  fseek(file, 0, SEEK_END);
  size_t file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  u8* file_data = temp_arena.Allocate(file_size);

  size_t read_amount = fread(file_data, 1, file_size, file);
  if (read_amount != file_size) {
    Log(LogLevel::Warning, "FileRequester FileExists to read entire file: %s", filename);
  }

  fclose(file);

  return true;
}

static void OnCompressedMapPkt(void* user, u8* pkt, size_t size) {
  FileRequester* requester = (FileRequester*)user;

  requester->OnCompressedFile(pkt, size);
}

FileRequester::FileRequester(MemoryArena& perm_arena, MemoryArena& temp_arena, Connection& connection,
                             PacketDispatcher& dispatcher)
    : perm_arena(perm_arena), temp_arena(temp_arena), connection(connection) {
  dispatcher.Register(ProtocolS2C::CompressedMap, OnCompressedMapPkt, this);
}

void FileRequester::OnCompressedFile(u8* pkt, size_t size) {
  if (current == nullptr) return;

  u8* data = pkt + 17;
  mz_ulong data_size = (mz_ulong)size - 17;

  ArenaSnapshot snapshot = temp_arena.GetSnapshot();

  if (current->decompress) {
    mz_ulong compressed_size = data_size;
    u8* uncompressed;
    int status;

    do {
      data_size *= 2;

      // Reset arena and try to allocate new space for the increased buffer
      temp_arena.Revert(snapshot);
      uncompressed = temp_arena.Allocate(data_size);

      status = mz_uncompress(uncompressed, &data_size, data, compressed_size);
    } while (status == MZ_BUF_ERROR);

    if (status != MZ_OK) {
      Log(LogLevel::Error, "Failed to uncompress map data.");
    } else {
      data = uncompressed;
    }
  }

  CreateZoneFolder(temp_arena);

  FILE* f = fopen(current->filename, "wb");
  if (f) {
    fwrite(data, 1, data_size, f);
    fclose(f);
  } else {
    Log(LogLevel::Error, "Failed to open %s for writing.", current->filename);
  }

  current->size = data_size;

  Log(LogLevel::Info, "Download complete: %s", current->filename);
  current->callback(current->user, current, data);

  temp_arena.Revert(snapshot);

  if (current == requests) {
    requests = requests->next;
  } else {
    FileRequest* check = requests;
    while (check) {
      if (check->next == current) {
        check->next = current->next;
        break;
      }
      check = check->next;
    }
  }

  current->next = free;
  free = current;
  current = requests;

  if (current) {
    SendNextRequest();
  }
}

void FileRequester::Request(const char* filename, u16 index, u32 size, u32 checksum, bool decompress,
                            RequestCallback callback, void* user) {
  FileRequest* request = free;

  if (request) {
    free = free->next;
  } else {
    request = memory_arena_push_type(&perm_arena, FileRequest);
  }

  GetFilePath(temp_arena, request->filename, filename);
  request->index = index;
  request->size = size;
  request->checksum = checksum;
  request->callback = callback;
  request->user = user;
  request->decompress = decompress;

  if (FileExists(temp_arena, filename)) {
    constexpr size_t kReadTries = 20;

    MemoryRevert reverter = temp_arena.GetReverter();
    u8* data = nullptr;

    for (size_t i = 0; i < kReadTries; ++i) {
      FILE* f = fopen(request->filename, "rb");
      fseek(f, 0, SEEK_END);
      long filesize = ftell(f);
      fseek(f, 0, SEEK_SET);

      request->size = filesize;

      temp_arena.Revert(reverter.snapshot);
      data = (u8*)temp_arena.Allocate(filesize);

      size_t read_amount = fread(data, 1, filesize, f);
      if (read_amount != filesize) {
        Log(LogLevel::Warning, "FileRequester failed to read entire file: %s", filename);
        data = nullptr;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }

      fclose(f);
      break;
    }

    if (data && crc32(data, request->size) == checksum) {
      callback(user, request, data);

      request->next = free;
      free = request;
      return;
    }
  }

  Log(LogLevel::Info, "Requesting download: %s", filename);
  request->next = requests;
  requests = request;

  if (current == nullptr) {
    SendNextRequest();
  }
}

void FileRequester::SendNextRequest() {
  current = requests;

#pragma pack(push, 1)
  struct {
    u8 type;
    u16 index;
  } file_request = {0x0C, current->index};
#pragma pack(pop)

  connection.packet_sequencer.SendReliableMessage(connection, (u8*)&file_request, sizeof(file_request));
}

}  // namespace zero

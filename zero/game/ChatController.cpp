#include "ChatController.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <zero/game/Camera.h>
#include <zero/game/Clock.h>
#include <zero/game/Logger.h>
#include <zero/game/Platform.h>
#include <zero/game/PlayerManager.h>
#include <zero/game/ShipController.h>
#include <zero/game/net/Connection.h>
#include <zero/game/net/PacketDispatcher.h>

namespace zero {

static void OnChatPacketRaw(void* user, u8* packet, size_t size) {
  ChatController* controller = (ChatController*)user;

  controller->OnChatPacket(packet, size);
}

ChatController::ChatController(PacketDispatcher& dispatcher, Connection& connection, PlayerManager& player_manager)
    : connection(connection), player_manager(player_manager) {
  dispatcher.Register(ProtocolS2C::Chat, OnChatPacketRaw, this);
}

void ChatController::SendMessage(ChatType type, const char* mesg) {
  u8 data[kMaxPacketSize];
  NetworkBuffer buffer(data, kMaxPacketSize);
  size_t size = strlen(mesg) + 1;

  buffer.WriteU8(0x06);
  buffer.WriteU8((u8)type);
  buffer.WriteU8(0x00);  // Sound
  buffer.WriteU16(0x00);
  buffer.WriteString(mesg, size);

  connection.packet_sequencer.SendReliableMessage(connection, buffer.data, buffer.GetSize());
}

void ChatController::SendPrivateMessage(const char* mesg, u16 pid) {
  u8 data[kMaxPacketSize];
  NetworkBuffer buffer(data, kMaxPacketSize);
  size_t size = strlen(mesg) + 1;

  buffer.WriteU8(0x06);
  buffer.WriteU8((u8)ChatType::Private);
  buffer.WriteU8(0x00);  // Sound
  buffer.WriteU16(pid);
  buffer.WriteString(mesg, size);

  connection.packet_sequencer.SendReliableMessage(connection, buffer.data, buffer.GetSize());
}

Player* ChatController::GetBestPlayerNameMatch(char* name, size_t length) {
  Player* best_match = nullptr;

  // Loop through each player looking at the first 'length' characters of their name.
  // If they match up to the length then add them as a candidate.
  // If they match up to the length and the name is exactly the same length as the check name then return that one.
  for (size_t i = 0; i < player_manager.player_count; ++i) {
    Player* p = player_manager.players + i;

    bool is_match = true;

    for (size_t j = 0; j < ZERO_ARRAY_SIZE(p->name) && j < length; ++j) {
      char p_curr = tolower(p->name[j]);
      char n_curr = tolower(name[j]);

      if (p_curr != n_curr) {
        is_match = false;
        break;
      }
    }

    if (is_match) {
      best_match = p;

      // If they match up until the length of the check name and they are the same length then it must be exact
      if (strlen(p->name) == length) {
        return p;
      }
    }
  }

  return best_match;
}

inline int GetShipStatusPercent(u32 upgrade, u32 maximum, u32 current) {
  if (upgrade == 0) return 100;

  u32 maximum_upgrades = maximum / upgrade;
  u32 current_upgrades = current / upgrade;

  return (current_upgrades * 100) / maximum_upgrades;
}

void ChatController::Update(float dt) {}

char GetChatTypePrefix(ChatType type) {
  static const char kPrefixes[] = {'A', ' ', ' ', 'T', 'O', 'P', 'W', 'R', 'E', 'C'};

  u8 index = (u8)type;
  if (index >= 0 && index <= (u8)ChatType::Channel) {
    return kPrefixes[index];
  }

  if (type == ChatType::Fuchsia) {
    return 'F';
  }

  return ' ';
}

void ChatController::OnChatPacket(u8* packet, size_t size) {
  ChatType type = (ChatType) * (packet + 1);
  u8 sound = *(packet + 2);
  u16 sender_id = *(u16*)(packet + 3);

  ChatEntry* entry = PushEntry((char*)packet + 5, size - 5, type);

  Player* player = player_manager.GetPlayerById(sender_id);
  if (player) {
    memcpy(entry->sender, player->name, 20);

    char prefix = GetChatTypePrefix(type);

    if (entry->type == ChatType::Private && player->id != player_manager.player_id) {
      history.InsertRecent(player->name);
    }

    if (type == ChatType::RemotePrivate || type == ChatType::Arena || type == ChatType::RedWarning ||
        type == ChatType::RedError) {
      Log(LogLevel::Info, "%c %s", prefix, entry->message);
    } else {
      Log(LogLevel::Info, "%c %s> %s", prefix, entry->sender, entry->message);
    }
  }

  if (entry->type == ChatType::RemotePrivate) {
    if (entry->message[0] == '(') {
      char* sender = entry->message + 1;
      char* current = entry->message;

      while (*current++) {
        if (*current == ')') {
          char name[20];

          sprintf(name, "%.*s", (u32)(current - sender), sender);

          history.InsertRecent(name);
          break;
        }
      }
    }
  }

  entry->sound = sound;
}

ChatEntry* ChatController::PushEntry(const char* mesg, size_t size, ChatType type) {
  ChatEntry* entry = entries + (entry_index++ % ZERO_ARRAY_SIZE(entries));

  memcpy(entry->message, mesg, size);
  entry->sender[0] = 0;
  entry->type = type;
  entry->sound = 0;

  return entry;
}

void ChatController::AddMessage(ChatType type, const char* fmt, ...) {
  ChatEntry* entry = PushEntry("", 0, type);

  va_list args;
  va_start(args, fmt);

  vsprintf(entry->message, fmt, args);

  va_end(args);
}

void PrivateHistory::InsertRecent(char* name) {
  RecentSenderNode* node = recent;
  RecentSenderNode* alloc_node = nullptr;

  size_t count = 0;

  while (node) {
    ++count;

    if (strcmp(node->name, name) == 0) {
      // Name is already in the list so set this one to the allocation node
      alloc_node = node;

      // Set the count high so it doesn't try to allocate
      count = ZERO_ARRAY_SIZE(nodes);
      break;
    }

    alloc_node = node;
    node = node->next;
  }

  if (count < ZERO_ARRAY_SIZE(nodes)) {
    // Allocate off the nodes until the recent list is fully populated
    node = nodes + count;
  } else {
    // Pop the last node off or the node that was a match for existing name
    RemoveNode(alloc_node);

    node = alloc_node;
  }

  strcpy(node->name, name);
  node->next = recent;
  recent = node;
}

void PrivateHistory::RemoveNode(RecentSenderNode* node) {
  RecentSenderNode* current = recent;

  while (current) {
    if (current->next == node) {
      current->next = node->next;
      break;
    }

    current = current->next;
  }
}

char* PrivateHistory::GetPrevious(char* current) {
  RecentSenderNode* node = recent;

  while (current && node) {
    if (strcmp(node->name, current) == 0) {
      RecentSenderNode* next = node->next;

      // If this is the last node in the list then return the first one
      if (!next) {
        next = recent;
      }

      return next->name;
    }

    node = node->next;
  }

  return recent ? recent->name : nullptr;
}

}  // namespace zero

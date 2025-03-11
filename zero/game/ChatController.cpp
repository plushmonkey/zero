#include "ChatController.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <zero/game/Camera.h>
#include <zero/game/Clock.h>
#include <zero/game/GameEvent.h>
#include <zero/game/Logger.h>
#include <zero/game/Platform.h>
#include <zero/game/PlayerManager.h>
#include <zero/game/ShipController.h>
#include <zero/game/net/Connection.h>
#include <zero/game/net/PacketDispatcher.h>

namespace zero {

constexpr float kFontWidth = 8.0f;
constexpr float kFontHeight = 12.0f;

struct ChatSpan {
  const char* begin;
  const char* end;
};

void WrapChat(const char* mesg, s32 linesize, ChatSpan* lines, size_t* linecount, bool skip_spaces = true) {
  // Trim front
  while (skip_spaces && *mesg && *mesg == ' ') ++mesg;

  s32 size = (s32)strlen(mesg);

  if (size < linesize) {
    lines[0].begin = mesg;
    lines[0].end = mesg + size;
    *linecount = 1;
    return;
  }

  s32 last_end = 0;
  for (int count = 1; count <= 16; ++count) {
    s32 end = last_end + linesize;

    if (end >= size) {
      end = size;
      lines[count - 1].begin = mesg + last_end;
      lines[count - 1].end = mesg + end;
      *linecount = count;
      break;
    }

    if (skip_spaces) {
      if (mesg[end] == ' ') {
        // Go backwards to trim off last space
        for (; end >= 0; --end) {
          if (mesg[end] != ' ') {
            ++end;
            break;
          }
        }
      } else {
        for (; end >= 0; --end) {
          // Go backwards looking for a space
          if (mesg[end] == ' ') {
            break;
          }
        }
      }
    }

    if (end <= last_end) {
      end = last_end + linesize;
    }

    lines[count - 1].begin = mesg + last_end;
    lines[count - 1].end = mesg + end;

    last_end = end;
    *linecount = count;

    // Trim again for next line
    while (skip_spaces && last_end < size && mesg[last_end] == ' ') {
      ++last_end;
    }
  }
}

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

void ChatController::Render(Camera& camera, SpriteRenderer& renderer) {
  // TODO: pull radar size from somewhere else
  float radar_size = camera.surface_dim.x * 0.165f + 11;
  float name_size = 12 * kFontWidth;
  float y = camera.surface_dim.y;
  size_t display_amount = (size_t)((camera.surface_dim.y / 100) + 1);
  ChatSpan lines[16];
  size_t linecount;

  if (entry_index == 0) return;

  size_t start = entry_index;
  size_t end = entry_index - display_amount;

  if (start < display_amount) {
    end = 0;
  }

  for (size_t i = start; i > end; --i) {
    size_t index = (i - 1) % ZERO_ARRAY_SIZE(entries);
    ChatEntry* entry = entries + index;

    u32 max_characters = (u32)((camera.surface_dim.x - radar_size - name_size) / kFontWidth);

    if (entry->type == ChatType::Arena || entry->type == ChatType::RedWarning || entry->type == ChatType::RedError ||
        entry->type == ChatType::Channel) {
      max_characters = (u32)((camera.surface_dim.x - radar_size) / kFontWidth);
    }

    WrapChat(entry->message, max_characters, lines, &linecount);

    y -= kFontHeight * linecount;

    switch (entry->type) {
      case ChatType::RemotePrivate:
      case ChatType::Fuchsia:
      case ChatType::Arena: {
        char output[512];

        for (size_t j = 0; j < linecount; ++j) {
          ChatSpan* span = lines + j;
          u32 length = (u32)(span->end - span->begin);

          sprintf(output, "%.*s", length, span->begin);

          TextColor color = entry->type == ChatType::Fuchsia ? TextColor::Fuschia : TextColor::Green;

          renderer.PushText(camera, output, color, Vector2f(0, y + j * kFontHeight), Layer::Chat);
        }
      } break;
      case ChatType::PublicMacro:
      case ChatType::Public: {
        char output[512];

        for (size_t j = 0; j < linecount; ++j) {
          ChatSpan* span = lines + j;
          u32 length = (u32)(span->end - span->begin);

          u32 spaces = 0;
          u32 sender_length = (u32)strlen(entry->sender);

          if (sender_length < g_Settings.chat_namelen) {
            spaces = g_Settings.chat_namelen - sender_length;
          }

          sprintf(output, "%*s%.*s> %.*s", spaces, "", g_Settings.chat_namelen, entry->sender, length, span->begin);

          renderer.PushText(camera, output, TextColor::Blue, Vector2f(0, y + j * kFontHeight), Layer::Chat);
        }
      } break;
      case ChatType::Team: {
        char output[512];

        for (size_t j = 0; j < linecount; ++j) {
          ChatSpan* span = lines + j;
          u32 length = (u32)(span->end - span->begin);

          u32 spaces = 0;
          u32 sender_length = (u32)strlen(entry->sender);

          if (sender_length < g_Settings.chat_namelen) {
            spaces = g_Settings.chat_namelen - sender_length;
          }

          sprintf(output, "%*s%.*s> %.*s", spaces, "", g_Settings.chat_namelen, entry->sender, length, span->begin);

          renderer.PushText(camera, output, TextColor::Yellow, Vector2f(0, y + j * kFontHeight), Layer::Chat);
        }
      } break;
      case ChatType::OtherTeam: {
        char output[512];

        for (size_t j = 0; j < linecount; ++j) {
          ChatSpan* span = lines + j;
          u32 length = (u32)(span->end - span->begin);

          u32 spaces = 0;
          u32 sender_length = (u32)strlen(entry->sender);

          if (sender_length < g_Settings.chat_namelen) {
            spaces = g_Settings.chat_namelen - sender_length;
          }

          sprintf(output, "%*s%.*s> ", spaces, "", g_Settings.chat_namelen, entry->sender);
          float skip = strlen(output) * kFontWidth;
          renderer.PushText(camera, output, TextColor::Green, Vector2f(0, y + j * kFontHeight), Layer::Chat);

          sprintf(output, "%.*s", length, span->begin);

          renderer.PushText(camera, output, TextColor::Blue, Vector2f(skip, y + j * kFontHeight), Layer::Chat);
        }
      } break;
      case ChatType::Private: {
        char output[512];

        for (size_t j = 0; j < linecount; ++j) {
          ChatSpan* span = lines + j;
          u32 length = (u32)(span->end - span->begin);

          u32 spaces = 0;
          u32 sender_length = (u32)strlen(entry->sender);

          if (sender_length < g_Settings.chat_namelen) {
            spaces = g_Settings.chat_namelen - sender_length;
          }

          sprintf(output, "%*s%.*s> %.*s", spaces, "", g_Settings.chat_namelen, entry->sender, length, span->begin);

          renderer.PushText(camera, output, TextColor::Green, Vector2f(0, y + j * kFontHeight), Layer::Chat);
        }
      } break;
      case ChatType::RedWarning:
      case ChatType::RedError: {
        char output[512];

        for (size_t j = 0; j < linecount; ++j) {
          ChatSpan* span = lines + j;
          u32 length = (u32)(span->end - span->begin);

          sprintf(output, "%.*s", length, span->begin);

          renderer.PushText(camera, output, TextColor::DarkRed, Vector2f(0, y + j * kFontHeight), Layer::Chat);
        }
      } break;
      case ChatType::Channel: {
        char output[512];

        for (size_t j = 0; j < linecount; ++j) {
          ChatSpan* span = lines + j;
          u32 length = (u32)(span->end - span->begin);

          sprintf(output, "%.*s", length, span->begin);

          renderer.PushText(camera, output, TextColor::Red, Vector2f(0, y + j * kFontHeight), Layer::Chat);
        }
      } break;
      default: {
      } break;
    }
  }
}

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

  // Don't output empty sound messages.
  if (type == ChatType::Arena && sound != 0 && size <= 6) return;

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

  Event::Dispatch(ChatEvent(entry->type, entry->sender, entry->message));
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

  if (node != nullptr && node == recent) {
    recent = node->next;
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

#include "ChatQueue.h"

#include <string.h>
#include <zero/game/Logger.h>
#include <zero/game/PlayerManager.h>

namespace zero {

ChatQueue::QueueEntry* ChatQueue::AcquireEntry() {
  size_t next_write_index = (write_index + 1) % ZERO_ARRAY_SIZE(entries);

  if (next_write_index == send_index) {
    Log(LogLevel::Warning, "ChatQueue: Failed to enqueue message. Queue was full.");
    return nullptr;
  }

  QueueEntry* entry = entries + write_index;

  write_index = next_write_index;

  return entry;
}

void ChatQueue::SendPublic(const char* message) {
  QueueEntry* entry = AcquireEntry();
  if (!entry) return;

  entry->type = ChatType::Public;
  strcpy(entry->message, message);
}

void ChatQueue::SendPrivate(const char* target_name, const char* message) {
  QueueEntry* entry = AcquireEntry();
  if (!entry) return;

  entry->type = ChatType::Private;
  strcpy(entry->message, message);
  strcpy(entry->target_name, target_name);
}

void ChatQueue::SendTeam(const char* message) {
  auto self = chat_controller.player_manager.GetSelf();
  if (!self) return;

  SendFrequency(self->frequency, message);
}

void ChatQueue::SendFrequency(u16 frequency, const char* message) {
  auto self = chat_controller.player_manager.GetSelf();
  if (!self) return;

  QueueEntry* entry = AcquireEntry();
  if (!entry) return;

  entry->type = frequency == self->frequency ? ChatType::Team : ChatType::OtherTeam;
  strcpy(entry->message, message);
  entry->frequency = frequency;
}

void ChatQueue::HandleEvent(const ChatQueueEvent& event) {
  switch (event.type) {
    case ChatType::Public: {
      SendPublic(event.message.data());
    } break;
    case ChatType::Private: {
      SendPrivate(event.target_name, event.message.data());
    } break;
    case ChatType::Team: {
      SendTeam(event.message.data());
    } break;
    case ChatType::OtherTeam: {
      SendFrequency(event.frequency, event.message.data());
    } break;
    default: {
    } break;
  }
}

void ChatQueue::HandleEvent(const ChatEvent& event) {
  recv_queue.push_back(ChatEntry());
  ChatEntry& entry = recv_queue.back();

  entry.type = event.type;
  strcpy(entry.message, event.message);
  strcpy(entry.sender, event.sender);
}

void ChatQueue::Update() {
  recv_queue.clear();

  u8 data[kMaxPacketSize];

  while (send_index != write_index) {
    Tick current_tick = GetCurrentTick();

    s32 d = TICK_DIFF(current_tick, last_check_tick) / 100;

    if (d > 0) {
      // TODO: This could be more accurate if synced with the server by listening to reliable message packets of the
      // chat messages that were sent. It's technically possible to flood if theres any ping spikes if using local count
      // instead of server.
      sent_message_count >>= d;
      last_check_tick = current_tick;
    }

    // Only send more messages if we're below the flood limit.
    if (sent_message_count >= flood_limit - 1) break;

    size_t next_send_index = (send_index + 1) % ZERO_ARRAY_SIZE(entries);
    QueueEntry* entry = entries + send_index;

    send_index = next_send_index;

    u16 target_pid = 0;

    if (entry->type == ChatType::Private) {
      auto player = chat_controller.player_manager.GetPlayerByName(entry->target_name);

      if (!player) {
        // We need to send this remotely if they aren't in the arena.

        NetworkBuffer buffer(data, kMaxPacketSize);
        // Size of the chat header + reliable message header
        constexpr size_t kHeaderSize = 5 + 6;
        char combined_message[kMaxPacketSize - kHeaderSize];
        size_t combined_size =
            snprintf(combined_message, sizeof(combined_message), ":%s:%s", entry->target_name, entry->message) + 1;

        buffer.WriteU8(0x06);
        buffer.WriteU8((u8)ChatType::RemotePrivate);
        buffer.WriteU8(0x00);  // Sound
        buffer.WriteU16(0x00);
        buffer.WriteString(combined_message, combined_size);

        auto& connection = chat_controller.connection;
        connection.packet_sequencer.SendReliableMessage(connection, buffer.data, buffer.GetSize());
        ++sent_message_count;

        // Add the sent message to the chat output box.
        ChatEntry* controller_entry =
            chat_controller.PushEntry(combined_message, combined_size, ChatType::RemotePrivate);

        Player* self = chat_controller.player_manager.GetSelf();
        if (self) {
          memcpy(controller_entry->sender, self->name, 20);
        }

        continue;
      }

      target_pid = player->id;
    } else if (entry->type == ChatType::OtherTeam) {
      bool found_target = false;

      for (size_t i = 0; i < chat_controller.player_manager.player_count; ++i) {
        Player* player = chat_controller.player_manager.players + i;
        if (player->frequency == entry->frequency) {
          target_pid = player->id;
          found_target = true;
          break;
        }
      }

      if (!found_target) {
        Log(LogLevel::Warning, "ChatQueue: Failed to send to frequency %hu. Could not find player on frequency.",
            entry->frequency);
        continue;
      }
    }

    NetworkBuffer buffer(data, kMaxPacketSize);
    size_t size = strlen(entry->message) + 1;

    buffer.WriteU8(0x06);
    buffer.WriteU8((u8)entry->type);
    buffer.WriteU8(0x00);  // Sound
    buffer.WriteU16(target_pid);
    buffer.WriteString(entry->message, size);

    auto& connection = chat_controller.connection;
    connection.packet_sequencer.SendReliableMessage(connection, buffer.data, buffer.GetSize());
    ++sent_message_count;

    if (size > 2 && entry->message[0] == '?' && entry->message[1] != '?') {
      // Treat commands as double cost.
      ++sent_message_count;
    } else if (entry->type == ChatType::Public) {
      // Treat public messages as more expensive
      sent_message_count += 2;
    }

    // Add the sent message to the chat output box.
    ChatEntry* controller_entry = chat_controller.PushEntry(entry->message, size, entry->type);

    Player* self = chat_controller.player_manager.GetSelf();
    if (self) {
      memcpy(controller_entry->sender, self->name, 20);
    }
  }
}

void ChatQueue::Reset() {
  write_index = 0;
  send_index = 0;
  sent_message_count = 0;
  last_check_tick = 0;
}

}  // namespace zero

#pragma once

#include <zero/Event.h>
#include <zero/game/ChatController.h>
#include <zero/game/GameEvent.h>

#include <vector>

namespace zero {

// This event can be dispatched from anywhere to have the ChatQueue pick it up and enqueue it.
struct ChatQueueEvent {
  ChatType type = ChatType::Public;
  const char* message = nullptr;
  const char* target_name = nullptr;
  u16 frequency = 0;

  static ChatQueueEvent Public(const char* message) {
    ChatQueueEvent event;

    event.type = ChatType::Public;
    event.message = message;

    return event;
  }

  static ChatQueueEvent Private(const char* target_name, const char* message) {
    ChatQueueEvent event;

    event.type = ChatType::Private;
    event.message = message;
    event.target_name = target_name;

    return event;
  }

  static ChatQueueEvent Team(const char* message) {
    ChatQueueEvent event;

    event.type = ChatType::Team;
    event.message = message;

    return event;
  }

  static ChatQueueEvent Frequency(u16 frequency, const char* message) {
    ChatQueueEvent event;

    event.type = ChatType::OtherTeam;
    event.message = message;
    event.frequency = frequency;

    return event;
  }

 private:
  ChatQueueEvent() {}
};

struct ChatQueue : EventHandler<ChatQueueEvent>, EventHandler<ChatEvent> {
 private:
  struct QueueEntry {
    ChatType type;
    char message[256];

    union {
      char target_name[32];
      u16 frequency;
    };
  };

 public:
  // This is the received chat messages since the last Update() call.
  std::vector<ChatEntry> recv_queue;

  // Circular buffer of send entries.
  QueueEntry entries[128];

  // The cirular buffer is fully processed when write_index is equal to send_index.
  size_t write_index = 0;
  size_t send_index = 0;

  ChatController& chat_controller;

  // This is the same implementation asss uses for a leaky bucket regulator.
  // This count is decreased every second and must be kept below the flood limit.
  u32 sent_message_count = 0;
  u32 last_check_tick = 0;

  ChatQueue(ChatController& chat) : chat_controller(chat) {}

  void Update();
  void Reset();

  void SendPublic(const char* message);
  void SendPrivate(const char* target_name, const char* message);
  void SendTeam(const char* message);
  void SendFrequency(u16 frequency, const char* message);

  void HandleEvent(const ChatQueueEvent& event) override;
  void HandleEvent(const ChatEvent& event) override;

 private:
  QueueEntry* AcquireEntry();
};

}  // namespace zero

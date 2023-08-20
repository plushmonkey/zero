#pragma once

#include <utility>
#include <vector>

namespace zero {

template <typename T>
struct EventTypeDispatcher;

struct Event {
  virtual ~Event() {}

  template <typename T>
  static void Dispatch(const T& event) {
    EventTypeDispatcher<T>::Get().Dispatch(event);
  }
};

// This is an event handler for a specific type of event.
// Users of this should inherit from each type of EventHandler<T> that they want to process.
// Implement void HandleEvent(const T& event) to handle the dispatched event.
template <typename T>
struct EventHandler {
 protected:
  EventHandler() { EventTypeDispatcher<T>::Get().RegisterHandler(this); }

 public:
  virtual ~EventHandler() { EventTypeDispatcher<T>::Get().UnregisterHandler(this); }

  virtual void HandleEvent(const T& event) = 0;
};

// This class stores the handlers for the given Event type T.
template <typename T>
struct EventTypeDispatcher {
 private:
  std::vector<EventHandler<T>*> handlers;

  virtual ~EventTypeDispatcher() {}

 public:
  static EventTypeDispatcher& Get() {
    static EventTypeDispatcher instance;
    return instance;
  }

  void Dispatch(const T& event) {
    for (auto& handler : handlers) {
      handler->HandleEvent(event);
    }
  }

  void RegisterHandler(EventHandler<T>* handler) { handlers.push_back(handler); }

  void UnregisterHandler(EventHandler<T>* handler) {
    for (size_t i = 0; i < handlers.size(); ++i) {
      auto check = handlers[i];
      if (check == handler) {
        std::swap(handlers[i], handlers[handlers.size() - 1]);
        handlers.pop_back();
        break;
      }
    }
  }
};

}  // namespace zero

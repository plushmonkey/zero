#pragma once

#include <stdio.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/game/Game.h>
#include <zero/game/Logger.h>

namespace zero {
namespace behavior {

struct DebugPrintNode : public BehaviorNode {
  DebugPrintNode(const char* message, LogLevel level = LogLevel::Debug) : message(message), level(level) {}
  DebugPrintNode(LogLevel level, const char* message) : message(message), level(level) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    Log(level, "%s", message);
    return ExecuteResult::Success;
  }

  const char* message;
  LogLevel level;
};

struct BlackboardSetQueryNode : public BehaviorNode {
  BlackboardSetQueryNode(const char* key) : key(key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    bool has = ctx.blackboard.Has(key);

    return has ? ExecuteResult::Success : ExecuteResult::Failure;
  }

  const char* key;
};

struct BlackboardEraseNode : public BehaviorNode {
  BlackboardEraseNode(const char* key) : key(key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    ctx.blackboard.Erase(key);
    return ExecuteResult::Success;
  }

  const char* key;
};

struct ReadConfigStringNode : public BehaviorNode {
  ReadConfigStringNode(const char* config_key_name, const char* output_key)
      : config_key_name(config_key_name), output_key(output_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    // Assign standard variables with priority of zone name section then General if that fails.
    const char* group_lookups[] = {to_string(ctx.bot->server_info.zone), "General"};

    auto opt_key_value = ctx.bot->config->GetString(group_lookups, ZERO_ARRAY_SIZE(group_lookups), config_key_name);
    if (!opt_key_value) return ExecuteResult::Failure;

    ctx.blackboard.Set<std::string>(output_key, std::string(*opt_key_value));

    return ExecuteResult::Success;
  }

  const char* config_key_name = nullptr;
  const char* output_key = nullptr;
};

template <typename T>
struct ReadConfigIntNode : public BehaviorNode {
  ReadConfigIntNode(const char* config_key_name, const char* output_key)
      : config_key_name(config_key_name), output_key(output_key) {}

  ExecuteResult Execute(ExecuteContext& ctx) override {
    // Assign standard variables with priority of zone name section then General if that fails.
    const char* group_lookups[] = {to_string(ctx.bot->server_info.zone), "General"};

    auto opt_key_value = ctx.bot->config->GetInt(group_lookups, ZERO_ARRAY_SIZE(group_lookups), config_key_name);
    if (!opt_key_value) return ExecuteResult::Failure;

    ctx.blackboard.Set<T>(output_key, (T)*opt_key_value);

    return ExecuteResult::Success;
  }

  const char* config_key_name = nullptr;
  const char* output_key = nullptr;
};

template <typename T>
struct ValueCompareQuery : public behavior::BehaviorNode {
  ValueCompareQuery(const char* key, T compare_value) : key(key), compare_value(compare_value) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto opt_value = ctx.blackboard.Value<T>(key);
    if (!opt_value.has_value()) return behavior::ExecuteResult::Failure;

    T current_value = opt_value.value();

    return current_value == compare_value ? behavior::ExecuteResult::Success : behavior::ExecuteResult::Failure;
  }

  const char* key;
  T compare_value;
};

}  // namespace behavior
}  // namespace zero

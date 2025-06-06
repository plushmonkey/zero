#pragma once

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/BehaviorTree.h>

namespace zero {
namespace tw {

// This node will toggle afterburner on and off.
// It will activate it at fully energy, drain to off_percent (default 0.5f), then recharge back to full.
struct AfterburnerThresholdNode : public behavior::BehaviorNode {
  AfterburnerThresholdNode() {}
  AfterburnerThresholdNode(float off_percent, float on_percent) : off_percent(off_percent), on_percent(on_percent) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    auto self = ctx.bot->game->player_manager.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    float max_energy = (float)ctx.bot->game->ship_controller.ship.energy;
    if (max_energy <= 0) return behavior::ExecuteResult::Success;

    float energy_pct = self->energy / max_energy;

    auto& input = *ctx.bot->bot_controller->input;
    auto& last_input = ctx.bot->bot_controller->last_input;

    // Keep using afterburners above off_percent. Disable until full energy, then enable again.
    if (last_input.IsDown(InputAction::Afterburner)) {
      input.SetAction(InputAction::Afterburner, self->energy > max_energy * off_percent);
    } else if (energy_pct >= on_percent) {
      input.SetAction(InputAction::Afterburner, true);
    }

    return behavior::ExecuteResult::Success;
  }

  float off_percent = 0.5f;
  float on_percent = 0.95f;
};

}  // namespace tw
}  // namespace zero

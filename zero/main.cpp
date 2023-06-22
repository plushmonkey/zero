#define NOMINMAX

#include <stdio.h>
#include <zero/ZeroBot.h>
#include <zero/game/Buffer.h>
#include <zero/game/Clock.h>
#include <zero/game/Game.h>
#include <zero/game/Memory.h>
#include <zero/game/Settings.h>
#include <zero/game/WorkQueue.h>
#include <zero/path/Pathfinder.h>

//
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

zero::ZeroBot* g_Bot = nullptr;

// Force disconnect when closing the console so the bot doesn't stick around in the zone waiting to be timed out.
BOOL WINAPI ConsoleCloserHandler(DWORD dwCtrlType) {
  if (g_Bot && g_Bot->game) {
    g_Bot->game->connection.SendDisconnect();
  }

  ExitProcess(0);
  return TRUE;
}

#endif

namespace zero {

ServerInfo kServers[] = {
    {"local", "192.168.0.169", 5000}, {"subgame", "192.168.0.169", 5002}, {"SSCE Hyperspace", "162.248.95.143", 5005},
    //{"SSCJ Devastation", "69.164.220.203", 7022},
    //{"SSCJ MetalGear CTF", "69.164.220.203", 14000},
    //{"SSCU Extreme Games", "208.118.63.35", 7900},
};

constexpr size_t kServerIndex = 0;
const char* kLoginName = "ZeroBot";
const char* kLoginPassword = "none";

const char* kServerName = kServers[kServerIndex].name;

static_assert(kServerIndex < ZERO_ARRAY_SIZE(kServers), "Bad server index");

}  // namespace zero

int main(void) {
#ifdef _WIN32
  SetConsoleCtrlHandler(ConsoleCloserHandler, TRUE);
#endif

  zero::ZeroBot bot;
  g_Bot = &bot;

  if (!bot.Initialize(zero::kLoginName, zero::kLoginPassword)) {
    return 1;
  }

  bot.JoinZone(zero::kServers[zero::kServerIndex]);
  bot.Run();

  return 0;
}

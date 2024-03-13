#include <zero/Config.h>
#include <zero/ZeroBot.h>
#include <zero/game/Buffer.h>
#include <zero/game/Clock.h>
#include <zero/game/Game.h>
#include <zero/game/Logger.h>
#include <zero/game/Memory.h>
#include <zero/game/Settings.h>
#include <zero/game/WorkQueue.h>
#include <zero/path/Pathfinder.h>
//
#include <stdio.h>
#include <stdlib.h>

#include <memory>

//
#ifdef APIENTRY
#undef APIENTRY
#endif

zero::ZeroBot* g_Bot = nullptr;

#ifdef _WIN32
#include <Windows.h>

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

const char* kSecurityServiceIp = "127.0.0.1";

ServerInfo kServers[] = {
    {"local", "127.0.0.1", 5000, Zone::Local},
    {"subgame", "127.0.0.1", 5002, Zone::Subgame},
    {"SSCE Hyperspace", "162.248.95.143", 5005, Zone::Hyperspace},
    {"SSCJ Devastation", "69.164.220.203", 7022, Zone::Devastation},
    {"SSCJ MetalGear CTF", "69.164.220.203", 14000, Zone::MetalGear},
    {"SSCU Extreme Games", "208.118.63.35", 7900, Zone::ExtremeGames},
};

const char* kLoginName = "ZeroBot";
const char* kLoginPassword = "none";

const char* kServerName = kServers[0].name.data();

static ServerInfo* GetServerByName(std::string_view name) {
  for (size_t i = 0; i < ZERO_ARRAY_SIZE(kServers); ++i) {
    std::string_view enum_name(to_string(kServers[i].zone));

    if (name == enum_name) {
      return kServers + i;
    }
  }

  return nullptr;
}

}  // namespace zero

int main(int argc, char* argv[]) {
#ifdef _WIN32
  SetConsoleCtrlHandler(ConsoleCloserHandler, TRUE);
#endif

  zero::ZeroBot bot;
  g_Bot = &bot;

  const char* login_name = zero::kLoginName;
  const char* login_password = zero::kLoginPassword;

  const char* cfg_path = "zero.cfg";

  if (argc > 1) {
    cfg_path = argv[1];
  }

  zero::ServerInfo* server = zero::kServers;

  auto cfg = zero::Config::Load(cfg_path);
  if (!cfg) {
    zero::Log(zero::LogLevel::Warning, "Failed to load config '%s'. Falling back to 'zero.cfg.dist'", cfg_path);
    cfg = zero::Config::Load("zero.cfg.dist");

    if (!cfg) {
      zero::Log(zero::LogLevel::Error, "Failed to load any config file.");
      return 1;
    }
  } else {
    zero::Log(zero::LogLevel::Info, "Using config file '%s'", cfg_path);
  }

  if (cfg) {
    auto username = cfg->GetString("Login", "Username");
    if (username) {
      login_name = *username;
    }

    auto password = cfg->GetString("Login", "Password");
    if (password) {
      login_password = *password;
    }

    auto server_name = cfg->GetString("Login", "Server");
    if (server_name) {
      auto requested_server = zero::GetServerByName(*server_name);
      if (requested_server) {
        server = requested_server;
      } else {
        zero::Log(zero::LogLevel::Error, "Login::Server value was not a valid server name.");
        return 1;
      }
    }

    auto encryption = cfg->GetString("Login", "Encryption");
    if (encryption) {
      std::string_view view(*encryption);
      if (view == "Subspace") {
        zero::g_Settings.encrypt_method = zero::EncryptMethod::Subspace;
      } else if (view == "Continuum") {
        zero::g_Settings.encrypt_method = zero::EncryptMethod::Continuum;
      } else {
        zero::Log(zero::LogLevel::Error,
                  "Login::Encryption value was not a valid type. Should be either Subspace or Continuum.");
        return 1;
      }
    }

    auto render_window = cfg->GetString("Debug", "RenderWindow");
    if (render_window) {
      zero::g_Settings.debug_window = strtol(*render_window, nullptr, 10) != 0;
    }

    auto print_behavior_tree = cfg->GetString("Debug", "RenderBehaviorTree");
    if (print_behavior_tree) {
      zero::g_Settings.debug_behavior_tree = strtol(*print_behavior_tree, nullptr, 10) != 0;
    }

    // Go through the servers that were configured and loading the data.
    zero::ConfigGroup servers_group = cfg->GetOrCreateGroup("Servers");
    for (auto& kv : servers_group.map) {
      auto server = zero::GetServerByName(kv.first);
      if (server) {
        std::string_view ip_port = kv.second.data();
        auto port_index = ip_port.find(':');

        std::string ip(ip_port.substr(0, port_index).substr());
        if (port_index != std::string_view::npos) {
          unsigned short port = (unsigned short)strtol(ip_port.substr(port_index + 1).data(), nullptr, 10);
          server->port = port;
        }

        server->ipaddr = ip;
      }
    }
  }

  const char* encrypt_type =
      zero::g_Settings.encrypt_method == zero::EncryptMethod::Subspace ? "Subspace" : "Continuum";
  zero::Log(zero::LogLevel::Info, "Attempting to login to '%s:%d' with encryption type '%s'.", server->ipaddr.data(),
            (int)server->port, encrypt_type);

  if (!bot.Initialize(login_name, login_password)) {
    return 1;
  }

  zero::kServerName = server->name.data();

  bot.JoinZone(*server);
  bot.Run();

  return 0;
}

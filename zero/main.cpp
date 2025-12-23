#include <zero/Utility.h>
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
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <memory>
#include <string>
#include <unordered_map>

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

#else
#include <signal.h>

static void SignalHandler(int signum) {
  if (g_Bot && g_Bot->game) {
    g_Bot->game->connection.SendDisconnect();
  }

  exit(0);
}

#endif

namespace zero {

const char* kSecurityServiceIp = "127.0.0.1";
char kLogPath[1024];

ServerInfo kServers[] = {
    {"local", "127.0.0.1", 5000, Zone::Local},
    {"subgame", "127.0.0.1", 5002, Zone::Subgame},
    {"SSCE Hyperspace", "162.248.95.143", 5005, Zone::Hyperspace},
    {"SSCJ Devastation", "69.164.220.203", 7022, Zone::Devastation},
    {"SSCJ MetalGear CTF", "69.164.220.203", 14000, Zone::MetalGear},
    {"SSCU Extreme Games", "208.118.63.35", 7900, Zone::ExtremeGames},
    {"SSCU Trench Wars", "127.0.0.1", 5000, Zone::TrenchWars},
    {"Nexus", "127.0.0.1", 5000, Zone::Nexus},
};

static_assert(ZERO_ARRAY_SIZE(kServers) == (size_t)Zone::Count - 1);

// Maps lowercase input to ServerInfo*. Input should always be lowercase to simplify lookups.
const std::unordered_map<std::string_view, ServerInfo*> kServerMap = {
    {"local", &kServers[0]},

    {"subgame", &kServers[1]},
    {"sub", &kServers[1]},
    {"sg", &kServers[1]},

    {"ssce hyperspace", &kServers[2]},
    {"hyperspace", &kServers[2]},
    {"hs", &kServers[2]},

    {"sscj devastation", &kServers[3]},
    {"devastation", &kServers[3]},
    {"deva", &kServers[3]},

    {"sscj metalgear ctf", &kServers[4]},
    {"metalgear ctf", &kServers[4]},
    {"metalgear", &kServers[4]},
    {"mg", &kServers[4]},

    {"sscu extreme games", &kServers[5]},
    {"extreme games", &kServers[5]},
    {"extremegames", &kServers[5]},
    {"eg", &kServers[5]},

    {"sscu trench wars", &kServers[6]},
    {"trench wars", &kServers[6]},
    {"trenchwars", &kServers[6]},
    {"trench", &kServers[6]},
    {"tw", &kServers[6]},

    {"nexus", &kServers[7]},
};

const char* kLoginName = "ZeroBot";
const char* kLoginPassword = "none";

const char* kServerName = kServers[0].name.data();

static ServerInfo* GetServerByName(std::string_view name) {
  std::string name_lookup = zero::Lowercase(name);

  auto iter = kServerMap.find(name_lookup);
  if (iter == kServerMap.end()) return nullptr;

  return iter->second;
}

}  // namespace zero

static void PrintUsage(std::string_view path) {
  size_t exe_begin = path.find_last_of("/\\");
  std::string_view exe_name = path;

  if (exe_begin != std::string_view::npos) {
    exe_name = path.substr(exe_begin + 1);
  }

  printf(
      "Usage: %s [OPTION]\n"
      "Subspace bot driven by config file.\n"
      "Override config arguments with options below.\n"
      "\n"
      "-n, --name\t\t\toverrides login name\n"
      "-p, --password\t\t\toverrides login password\n"
      "-c, --config\t\t\toverrides config file\n"
      "-e, --encryption\t\toverrides encryption type\n"
      "\t\t\t\tvalues: subspace, continuum\n"
      "-s, --server\t\t\toverrides server name\n"
      "\t\t\t\tvalues: local, subgame, hs\n"
      "\t\t\t\t\tdeva, mg, eg, tw, nexus\n"
      "-a, --arena\t\t\tsets default arena\n"
      "--ship\t\t\t\tsets default ship\n"
      "-b, --behavior\t\t\tsets default behavior\n"
      "\t\t\t\tvalues depend on server\n"
      "-l, --loglevel\t\t\toverrides log level\n"
      "\t\t\t\tvalues: j, d, i, w, e\n"
#ifdef GLFW_AVAILABLE
      "--render\t\t\tenables render window\n"
#endif
      "",
      exe_name.data());
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
  SetConsoleCtrlHandler(ConsoleCloserHandler, TRUE);
#else
  signal(SIGINT, SignalHandler);
#endif

  srand((unsigned int)time(NULL));

  zero::ZeroBot bot;
  g_Bot = &bot;

  zero::g_LogPrintLevel = zero::LogLevel::Info;

  std::string login_name = zero::kLoginName;
  std::string login_password = zero::kLoginPassword;

  const char* cfg_path = "zero.cfg";

  // Handle special case of setting config as only argument.
  // This is useful for dragging config file onto exe for easy running.
  if (argc == 2 && argv[1][0] != '-') {
    cfg_path = argv[1];
  }

  std::unique_ptr<zero::ArgParser> args = std::make_unique<zero::ArgParser>(argc, argv);

  if (args->HasParameter({"help", "h"})) {
    PrintUsage(argv[0]);
    return 0;
  }

  std::string_view config_override = args->GetValue({"config", "c"});
  if (!config_override.empty()) {
    cfg_path = config_override.data();
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
      std::string type = zero::Lowercase(*encryption);

      if (type == "subspace") {
        zero::g_Settings.encrypt_method = zero::EncryptMethod::Subspace;
      } else if (type == "continuum") {
        zero::g_Settings.encrypt_method = zero::EncryptMethod::Continuum;
      } else {
        zero::Log(zero::LogLevel::Error,
                  "Login::Encryption value was not a valid type. Should be either Subspace or Continuum.");
        return 1;
      }
    }

    auto log_level_str = cfg->GetString("General", "LogLevel");
    if (log_level_str) {
      std::string type = zero::Lowercase(*log_level_str);

      if (type == "jabber") {
        zero::g_LogPrintLevel = zero::LogLevel::Jabber;
      } else if (type == "debug") {
        zero::g_LogPrintLevel = zero::LogLevel::Debug;
      } else if (type == "info") {
        zero::g_LogPrintLevel = zero::LogLevel::Info;
      } else if (type == "warning") {
        zero::g_LogPrintLevel = zero::LogLevel::Warning;
      } else if (type == "error") {
        zero::g_LogPrintLevel = zero::LogLevel::Error;
      } else {
        zero::Log(zero::LogLevel::Error, "General::LogLevel that was specified is not valid.");
        return 1;
      }
    }

    auto owner_str = cfg->GetString("General", "Owner");
    if (owner_str) {
      bot.owner = *owner_str;
    } else {
      bot.owner = "*unset*";
    }

    auto render_window = cfg->GetString("Debug", "RenderWindow");
    if (render_window) {
      zero::g_Settings.debug_window = strtol(*render_window, nullptr, 10) != 0;
    }

    auto print_behavior_tree = cfg->GetString("Debug", "RenderBehaviorTree");
    if (print_behavior_tree) {
      zero::g_Settings.debug_behavior_tree = strtol(*print_behavior_tree, nullptr, 10) != 0;
    }

    // Go through the servers that were configured and load the data.
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

  bot.config = std::move(cfg);

  // Check arguments for overrides.
  {
    std::string_view name_override = args->GetValue({"name", "n"});
    std::string_view password_override = args->GetValue({"password", "p"});
    std::string_view encryption_override = args->GetValue({"encryption", "e"});
    std::string_view server_override = args->GetValue({"server", "s"});
    std::string_view loglevel_override = args->GetValue({"loglevel", "l"});

    if (!name_override.empty()) {
      login_name = name_override.data();
    }

    if (!password_override.empty()) {
      login_password = password_override.data();
    }

    if (!encryption_override.empty()) {
      std::string type = zero::Lowercase(encryption_override);

      if (type == "subspace") {
        zero::g_Settings.encrypt_method = zero::EncryptMethod::Subspace;
      } else if (type == "continuum") {
        zero::g_Settings.encrypt_method = zero::EncryptMethod::Continuum;
      }
    }

    if (!server_override.empty()) {
      auto requested_server = zero::GetServerByName(server_override);
      if (requested_server) {
        server = requested_server;
      } else {
        zero::Log(zero::LogLevel::Error, "Login::Server value was not a valid server name.");
        return 1;
      }
    }

    if (!loglevel_override.empty()) {
      char l = tolower(loglevel_override[0]);

      switch (l) {
        case 'j': {
          zero::g_LogPrintLevel = zero::LogLevel::Jabber;
        } break;
        case 'd': {
          zero::g_LogPrintLevel = zero::LogLevel::Debug;
        } break;
        case 'i': {
          zero::g_LogPrintLevel = zero::LogLevel::Info;
        } break;
        case 'w': {
          zero::g_LogPrintLevel = zero::LogLevel::Warning;
        } break;
        case 'e': {
          zero::g_LogPrintLevel = zero::LogLevel::Error;
        } break;
        default: {
          zero::Log(zero::LogLevel::Error, "Invalid log level specified as argument.\n");
        }
      }
    }

    if (args->HasParameter("render")) {
      zero::g_Settings.debug_window = true;
    }
  }

  sprintf(zero::kLogPath, "%s-%s.log", login_name.data(), server->name.data());
  zero::g_LogPath = zero::kLogPath;

  zero::Log(zero::LogLevel::Info, "==================================================");

  const char* encrypt_type =
      zero::g_Settings.encrypt_method == zero::EncryptMethod::Subspace ? "Subspace" : "Continuum";
  zero::Log(zero::LogLevel::Info, "Attempting to login to '%s:%d' with encryption type '%s'.", server->ipaddr.data(),
            (int)server->port, encrypt_type);

  if (!bot.Initialize(std::move(args), login_name.data(), login_password.data())) {
    return 1;
  }

  zero::kServerName = server->name.data();

  bot.JoinZone(*server);
  bot.Run();

  return 0;
}

#include "Logger.h"

namespace zero {

LogLevel g_LogPrintLevel = LogLevel::Jabber;

void LogArgs(LogLevel level, const char* fmt, va_list args) {
  static const char kLevelCharacters[] = {'J', 'D', 'I', 'W', 'E'};

  static_assert(ZERO_ARRAY_SIZE(kLevelCharacters) == (size_t)LogLevel::Count);

  if ((size_t)level < (size_t)g_LogPrintLevel) return;

  auto file = stdout;

  if ((size_t)level >= (size_t)LogLevel::Warning) {
    file = stderr;
  }

  char buffer[2048];

#ifdef _WIN32
  size_t size = vsprintf_s(buffer, fmt, args);
#else
  size_t size = vsprintf(buffer, fmt, args);
#endif

  time_t now = time(nullptr);
  tm* tm = localtime(&now);

  char level_c = 'D';

  if ((size_t)level < ZERO_ARRAY_SIZE(kLevelCharacters)) {
    level_c = kLevelCharacters[(size_t)level];
  }

  fprintf(file, "%c [%02d:%02d:%02d] %s\n", level_c, tm->tm_hour, tm->tm_min, tm->tm_sec, buffer);

  fflush(stdout);
}

}  // namespace zero

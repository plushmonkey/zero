#pragma once

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

namespace zero {

enum class LogLevel { Debug, Info, Warning, Error };

static const LogLevel g_LogPrintLevel = LogLevel::Debug;

inline void Log(LogLevel level, const char* fmt, ...) {
  static const char kLevelCharacters[] = { 'D', 'I', 'W', 'E' };

  if ((size_t)level < (size_t)g_LogPrintLevel) return;

  auto file = stdout;

  if ((size_t)level >= (size_t)LogLevel::Warning) {
    file = stderr;
  }

  char buffer[2048];

  va_list args;

  va_start(args, fmt);

#ifdef _WIN32
  size_t size = vsprintf_s(buffer, fmt, args);
#else
  size_t size = vsprintf(buffer, fmt, args);
#endif

  va_end(args);

  time_t now = time(nullptr);
  tm* tm = gmtime(&now);

  char level_c = 'D';

  if ((size_t)level < ZERO_ARRAY_SIZE(kLevelCharacters)) {
    level_c = kLevelCharacters[(size_t)level];
  }

  fprintf(file, "%c [%d:%d:%d] %s\n", level_c, tm->tm_hour, tm->tm_min, tm->tm_sec, buffer);

  fflush(stdout);
}

}  // namespace zero

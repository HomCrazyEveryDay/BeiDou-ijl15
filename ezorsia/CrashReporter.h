#pragma once

#include <string>
#include <windows.h>

namespace CrashReporter {
void Install(bool enabled, const std::string& dumpType, bool traceEnabled);
void RecordEvent(const char* category, const char* format, ...);
void RecordRecentEvent(const char* category, const char* format, ...);
void RecordIncomingPacket(unsigned short opcode, unsigned long size, unsigned int offset);
LONG CaptureHandledException(const char* category, const char* stage, EXCEPTION_POINTERS* exceptionInfo);
}

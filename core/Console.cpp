#include "core/Console.h"
#include "core/Config.h"
#include "core/LockGuard.h"
#include "core/Mutex.h"
#include "core/RdeAssert.h"
#include "core/System.h"
#include "core/Thread.h"
#include <cstdarg>
#include <cstdio>

namespace
{
const long					MAX_PRINT_SIZE(4096);
rde::uint32					s_enabledStreams = (rde::uint32)rde::Console::Streams::ALL;
rde::Mutex					s_printMutex;
int							s_numWarnings(0);
int							s_numListeners(0);
rde::Console::PrintListener	s_listeners[rde::Console::kMaxListeners];

void SignalListeners(rde::Console::Streams::Enum stream, const char* msg)
{
	for (int i = 0; i < s_numListeners; ++i)
	{
		if (s_listeners[i])
			s_listeners[i](stream, msg);
	}
}

} // <anonymous> namespace

namespace rde
{
namespace Console
{
bool AddPrintListener(const PrintListener& listener)
{
	if (s_numListeners < kMaxListeners)
	{
#if RDE_DEBUG
		static int s_mainThread = Thread::GetCurrentThreadId();
		RDE_ASSERT(s_mainThread == Thread::GetCurrentThreadId() &&
			"AddPrintListener() can only be called from single thread!");
#endif
		// @TODO: make thread safe.
		s_listeners[s_numListeners++] = listener;
		return true;
	}
	return false;
}

void Print(Streams::Enum stream, FILE* file, const char* str)
{
	s_printMutex.Acquire();
	if (IsStreamEnabled(stream))
	{
		const char* currentThreadName = Thread::GetCurrentThreadName();
		if (currentThreadName == 0)
			::fprintf(file, "%s", str);
		else
			::fprintf(file, "%s: %s", currentThreadName, str);
		SignalListeners(stream, str);
	}
	s_printMutex.Release();
	Sys::DebugPrint(str);
}

void Printf(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt); 
	char buff[MAX_PRINT_SIZE];
	const int size = _vsnprintf(buff, RDE_ARRAY_COUNT(buff), fmt, args);
	RDE_ASSERT(size >= 0);
	Print(Streams::INFO, stdout, buff);
	va_end(args);
}
void Debugf(const char* fmt, ...)
{
	if (IsStreamEnabled(Streams::DEBUG))
	{
		va_list args;
		va_start(args, fmt); 
		char buff[MAX_PRINT_SIZE];
		const int size = _vsnprintf(buff, RDE_ARRAY_COUNT(buff), fmt, args);
		RDE_ASSERT(size >= 0);
		Print(Streams::DEBUG, stdout, buff);
		va_end(args);
	}
}
void Profilef(const char* fmt, ...)
{
	if (IsStreamEnabled(Streams::PROFILE))
	{
		va_list args;
		va_start(args, fmt); 
		char buff[MAX_PRINT_SIZE];
		const int size = _vsnprintf(buff, RDE_ARRAY_COUNT(buff), fmt, args);
		RDE_ASSERT(size >= 0);
		Print(Streams::PROFILE, stdout, buff);
		va_end(args);
	}
}
void Warningf(const char* fmt, ...)
{
	if (IsStreamEnabled(Streams::WARNING))
	{
		va_list args;
		va_start(args, fmt); 
		char buff[MAX_PRINT_SIZE];
		int size = ::sprintf_s(buff, "Warning (%d): ", s_numWarnings);
		RDE_ASSERT(size >= 0);
		size = _vsnprintf(buff + size, RDE_ARRAY_COUNT(buff)-size, fmt, args);
		RDE_ASSERT(size >= 0);
		va_end(args);
		Print(Streams::WARNING, stderr, buff);
	}
	++s_numWarnings;
}
void Errorf(const char* fmt, ...)
{
	static int s_numErrors(0);
	if (IsStreamEnabled(Streams::NORMAL_ERROR))
	{
		va_list args;
		va_start(args, fmt); 
		char buff[MAX_PRINT_SIZE];
		int size = ::sprintf_s(buff, "Error (%d): ", s_numErrors);
		RDE_ASSERT(size >= 0);
		size = _vsnprintf(buff + size, RDE_ARRAY_COUNT(buff)-size, fmt, args);
		RDE_ASSERT(size >= 0);
		va_end(args);
		Print(Streams::NORMAL_ERROR, stderr, buff);

		const Sys::ErrorHandlerDelegate& errHandler = Sys::GetErrorHandler();
		if (errHandler)
		{
			const bool isFatalError = false;
			errHandler(isFatalError, buff);
		}
	}
	++s_numErrors;
}
void FatalErrorf(const char* fmt, ...)
{
	// We've to do it even if the stream is disabled.
	// It wont be printed anyway in such case, but we need buffer for our handler
	// (OnFatalError)
	va_list args;
	va_start(args, fmt); 
	char buff[MAX_PRINT_SIZE];
	const int size = _vsnprintf(buff, RDE_ARRAY_COUNT(buff), fmt, args);
	RDE_ASSERT(size >= 0);
	Print(Streams::FATAL_ERROR, stderr, buff);
	va_end(args);
	Sys::OnFatalError(buff);
}

void EnableStream(Streams::Enum stream, bool enabled)
{
	if (enabled)
		s_enabledStreams |= stream;
	else
		s_enabledStreams &= ~stream;
}
bool IsStreamEnabled(Streams::Enum stream)
{
	return (s_enabledStreams & stream) == (rde::uint32)stream;
}

} // console
} // rde

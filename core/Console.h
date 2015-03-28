#ifndef CORE_CONSOLE_H
#define CORE_CONSOLE_H

#include <external/srutil/delegate.hpp>

// Not nice, but we really do not want to include whole header.
#if _WIN32
	struct _iobuf;
	typedef struct _iobuf FILE;
#	define FILE_HANDLE FILE*
#endif

// Very final build version, that lets you to disable console
// almost completely - it'll still try to log fatal errors, but that's it.
//#define RDE_DISABLE_CONSOLE	0

namespace rde
{
namespace Console
{
namespace Streams
{
	enum Enum
	{
		// Just a general info.
		INFO			= 0x1,
		// Debug message.
		DEBUG			= 0x2,
		// Profiler message.
		PROFILE			= 0x4,
		// Warning, something went wrong, but we can carry on.
		WARNING			= 0x8,
		// Serious error, we'll try to continue, though.
		NORMAL_ERROR	= 0x10,
		// Fatal error, trying to close the application.
		FATAL_ERROR		= 0x20,
		ALL				= 0xFFFFFFFF
	};
} // Streams namespace

typedef srutil::delegate2<void, Streams::Enum, const char*>	PrintListener;

// Thread-safe.
void Print(Streams::Enum stream, FILE* file, const char* str);

// Up to kMaxListeners listeners.
const int kMaxListeners = 8;
// False if too many listeners already registered.
// Listener list is NOT thread-safe.
bool AddPrintListener(const PrintListener&);
// @todo: Add possibility of removing listeners?

#if !RDE_DISABLE_CONSOLE
void Printf(const char* fmt, ...);
void Debugf(const char* fmt, ...);
void Profilef(const char* fmt, ...);
void Warningf(const char* fmt, ...);
void Errorf(const char* fmt, ...);
void FatalErrorf(const char* fmt, ...);
#else
void Printf(const char*, ...) {}
void Debugf(const char*, ...) {}
void Profilef(const char*, ...) {}
void Warningf(const char*, ...) {}
void Errorf(const char*, ...) {}
void FatalErrorf(const char* fmt, ...);
#endif
// Stream state management not thread-safe.
void EnableStream(Streams::Enum, bool enable);
bool IsStreamEnabled(Streams::Enum);
} // Console
} // rde

#endif // CORE_CONSOLE

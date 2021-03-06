#include "core/RdeAssert.h"
#include "core/Atomic.h"
#include "core/Console.h"

namespace
{
rde::Assertion::Handler s_handler(0);
long					s_numAssertionFailures(0);
}

namespace rde
{
Assertion::Handler Assertion::SetHandler(Handler newHandler)
{
	Handler prevHandler = s_handler;
	s_handler = newHandler;
	return prevHandler;
}

bool Assertion::Failure(const char* expr, const char* file, int line)
{
	Interlocked::Increment(&s_numAssertionFailures);
	// Potentially risky (can crash in case of low system resources).
	if (!IsDebuggerPresent())
		Console::Printf("%s (%s:%d)\n", expr, file, line);
	if (s_handler)
		return s_handler(expr, file, line);
	return false;
}
} // rde

#ifndef CORE_BACKOFF_H
#define CORE_BACKOFF_H

#include "core/Thread.h"

namespace rde
{
// Waits increasing (exponentially) number of "machine pauses" with
// every call to Wait().
class Backoff
{
public:
	Backoff():	m_delay(1) {}
	void Wait()
	{
		rde::Thread::MachinePause(m_delay);
		m_delay *= 2;
	}
private:
	int	m_delay;
};
}

#endif

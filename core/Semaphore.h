#ifndef CORE_SEMAPHORE_H
#define CORE_SEMAPHORE_H

#include "core/Config.h"
#include "core/MaxAlign.h"

namespace rde
{
class Semaphore
{
public:
	explicit Semaphore(int initialValue = 0);
	~Semaphore();

	void WaitInfinite();
	bool WaitTimeout(long milliseconds);
	void Signal(int num = 1);

private:
	RDE_FORBID_COPY(Semaphore);
	struct Impl;
	union
	{
		MaxAlign	m_align;
		uint8		m_implMem[4];
	};
};
}

#endif // CORE_SEMAPHORE_H

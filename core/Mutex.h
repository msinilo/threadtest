#ifndef CORE_MUTEX_H
#define CORE_MUTEX_H

#include "core/MaxAlign.h"

namespace rde
{
class Mutex
{
public:
	// Spin count > 0: try to spin N times before sleeping.
	explicit Mutex(long spinCount = 0);
	~Mutex();

	void Acquire() const;
	bool TryAcquire() const;
	void Release() const;

	bool IsLocked() const;
	void* GetSystemRepresentation() const;

private:
	RDE_FORBID_COPY(Mutex);
	struct Impl;
	union
	{
		MaxAlign	m_aligner;
		uint8		m_implMem[32];
	};
	Impl*			m_impl;
};

}

#endif 

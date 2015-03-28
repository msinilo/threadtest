#ifndef CORE_SPIN_MUTEX_H
#define CORE_SPIN_MUTEX_H

namespace rde
{

class SpinMutex
{
public:
	SpinMutex();
	~SpinMutex();

	void Acquire();
	bool TryAcquire();
	void Release();

private:
	char	m_locked;
};

}

#endif

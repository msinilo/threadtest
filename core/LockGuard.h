#ifndef CORE_LOCK_GUARD_H
#define CORE_LOCK_GUARD_H

namespace rde
{
// Acquires mutex when entering scope,
// releases when leaving.
template<class TMutex>
class LockGuard
{
public:
	explicit LockGuard(TMutex& m)
	:	m_mutex(m) 
	{
		m_mutex.Acquire();
	}
	~LockGuard()
	{
		m_mutex.Release();
	}

private:
	LockGuard(const LockGuard&);
	LockGuard& operator=(const LockGuard&);

	TMutex&	m_mutex;
};

}

#endif 

#pragma once

#include "ThreadPool_Lock.h"
#include "rdestl/pair.h"

namespace rde
{
struct TaskGroup_DEQueue;
class Scheduler;

struct TaskData_DEQueue
{
	TaskData_DEQueue(): m_group(0) {}

	TaskGroup_DEQueue*	m_group;
	int					m_groupID;
};

namespace internal
{
struct TaskAllocatorProxy
{
	explicit TaskAllocatorProxy(Scheduler& scheduler): m_scheduler(scheduler) {}

	void* Allocate(size_t bytes) const;
	void Free(void* ptr) const;

	Scheduler&	m_scheduler;
};
}

class ThreadPool_DEQueue
{
public:
	typedef TaskData_DEQueue* (*TaskFunc)(void*);

	explicit ThreadPool_DEQueue(int numThreads);
	~ThreadPool_DEQueue();

	TaskGroupID CreateTaskGroup(const char* name, TaskFunc func);
	template<typename T>
	TaskGroupID CreateTaskGroup(const char* name, TaskData_DEQueue* (*func)(T*))
	{
		return CreateTaskGroup(name, (TaskFunc)func);
	}
	void AddTask(TaskGroupID groupID, TaskData_DEQueue* data);
	void WaitForGroup(TaskGroupID groupID);

	internal::TaskAllocatorProxy AllocateTask();

	// pop/steal
	rde::pair<float, float> GetAveragePopStealOverhead() const;

	struct Impl;
private:
	rde::ScopedPtr<Impl>	m_impl;
};

} // rde

inline void* operator new(size_t bytes, const rde::internal::TaskAllocatorProxy& p)
{
	return p.Allocate(bytes);
}
inline void operator delete(void* ptr, const rde::internal::TaskAllocatorProxy& p)
{
	p.Free(ptr);
}



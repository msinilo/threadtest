
#pragma once

#include "ThreadPool_Lock.h"

namespace rde
{
class ThreadPool_SPMC
{
public:
	typedef void (*TaskFunc)(void*, int);

	explicit ThreadPool_SPMC(int numThreads);
	~ThreadPool_SPMC();

	TaskGroupID CreateTaskGroup(const char* name, TaskFunc func);
	template<typename T>
	TaskGroupID CreateTaskGroup(const char* name, void (*func)(T*, int))
	{
		return CreateTaskGroup(name, (TaskFunc)func);
	}

	void WaitForAllTasks(TaskGroupID group, TaskData* data, int numTasks);

	float GetAveragePopOverhead() const;

	struct Impl;
private:
	rde::ScopedPtr<Impl>	m_impl;
};

}

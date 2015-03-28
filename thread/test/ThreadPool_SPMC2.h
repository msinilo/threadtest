#pragma once

#include "ThreadPool_Lock.h"

namespace rde
{
class ThreadPool_SPMC2
{
public:
	typedef void (*TaskFunc)(void*);

	explicit ThreadPool_SPMC2(int numThreads);
	~ThreadPool_SPMC2();

	TaskGroupID CreateTaskGroup(const char* name, TaskFunc func);
	template<typename T>
	TaskGroupID CreateTaskGroup(const char* name, void (*func)(T*))
	{
		return CreateTaskGroup(name, (TaskFunc)func);
	}
	void StartTasks(TaskGroupID groupID, TaskData* data, int numTasks, int dataPitch);
	void WaitForAllTasks();

	struct Impl;
private:
	rde::ScopedPtr<Impl>	m_impl;
};

}

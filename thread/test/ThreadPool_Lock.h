#pragma once

#include "core/ScopedPtr.h"

namespace rde
{
typedef int32 TaskGroupID;

struct TaskData
{
	TaskGroupID	m_groupID;
	TaskData*	next;
};

class ThreadPool_Lock
{
public:
	typedef void (*TaskFunc)(void*);

	explicit ThreadPool_Lock(int numThreads);
	~ThreadPool_Lock();

	TaskGroupID CreateTaskGroup(const char* name, TaskFunc func);
	template<typename T>
	TaskGroupID CreateTaskGroup(const char* name, void (*func)(T*))
	{
		return CreateTaskGroup(name, (TaskFunc)func);
	}
	void AddTask(TaskGroupID groupID, TaskData* data);
	void WaitForAllTasks();

	float GetAveragePopOverhead() const;

	struct Impl;
private:
	rde::ScopedPtr<Impl>	m_impl;
};

}

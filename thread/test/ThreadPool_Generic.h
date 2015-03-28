#include "ThreadPool_Lock.h"

namespace rde
{
class THREAD_POOL_CLASS
{
public:
	typedef void (*TaskFunc)(void*);

	explicit THREAD_POOL_CLASS(int numThreads);
	~THREAD_POOL_CLASS();

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

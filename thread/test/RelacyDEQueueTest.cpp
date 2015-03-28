#pragma warning(push, 1)
#pragma warning(disable: 4311)
#pragma warning(disable: 4312)

#define RDE_DBG_RELACY_TEST 0

#if RDE_DBG_RELACY_TEST

#include <external/relacy/relacy_std.hpp>
#include "thread/LockFreeDEQueue.h"

namespace
{
const int kNumThreads		= 1 + 3;
const int kNumItemsInQueue	= 100;
struct LockFreeDEQueueTest : rl::test_suite<LockFreeDEQueueTest, kNumThreads>
{
	rde::LockFreeDEQueue<int>	m_queue;
	int							m_numConsumed[kNumThreads];

	LockFreeDEQueueTest():	m_queue(1000) {}
		
	void before()
	{
		for (int i = 0; i < kNumThreads; ++i)
			m_numConsumed[i] = 0;
	}
	void after()
	{
		int totalConsumed(0);
		for (int i = 0; i < kNumThreads; ++i)
			totalConsumed += m_numConsumed[i];
		RL_ASSERT(totalConsumed == kNumItemsInQueue);
	}

    void thread(unsigned index)
    {
		if (index == 0)	// producer/consumer (bottom)
		{
			int data[kNumItemsInQueue];
			for (int i = 0; i < kNumItemsInQueue; ++i)
			{
				data[i] = 555;
				m_queue.PushBottom(&data[i]);
			}
			for (int i = 0; i < kNumItemsInQueue; ++i)
			{
				const int* idata = m_queue.PopBottom();
				RL_ASSERT(idata == 0 || *idata == 555);
				if (idata != 0)
					++m_numConsumed[index];
			}
		}
		else	// consumer (top)
		{
			for (int i = 0; i < kNumItemsInQueue; ++i)
			{
				const int* idata = (m_queue.IsEmpty() ? 0 : m_queue.PopTop());
				RL_ASSERT(idata == 0 || *idata == 555);
				if (idata != 0)
					++m_numConsumed[index];
			}
		}
	}
};


} // namespace

void LockFreeDEQueue_Test_Run()
{
	// Random scheduler
	rl::simulate<LockFreeDEQueueTest>();

//    rl::test_params params;
  //  params.search_type = rl::fair_context_bound_scheduler_type;
    //params.context_bound = 2;
    //rl::simulate<LockFreeDEQueueTest>(params);
}

#else

void LockFreeDEQueue_Test_Run() {}

#endif // RDE_DBG_RELACY_TEST

#pragma warning(pop)


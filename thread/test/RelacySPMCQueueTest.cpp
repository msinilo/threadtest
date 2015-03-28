#pragma warning(push, 1)
#pragma warning(disable: 4311)
#pragma warning(disable: 4312)

#define RDE_DBG_RELACY_TEST 0

#if RDE_DBG_RELACY_TEST

#include <external/relacy/relacy_std.hpp>
#include "thread/SPMCQueue.h"

namespace
{
const int kNumItems = 50;
struct SPMCQueue_Test : rl::test_suite<SPMCQueue_Test, 2>
{
	rde::SPMCQueue<int, 128, false> q;
	std::atomic<int> numPopped;
	int poppedItems[kNumItems];
	std::atomic<int> done;

	void before()
	{
		numPopped.store(0, rl::memory_order_relaxed);
		done.store(0, rl::memory_order_relaxed);
		for (int i = 0; i < kNumItems; ++i)
			poppedItems[i] = 0;
	}
	void after()
	{
		RL_ASSERT(numPopped($) == kNumItems);
		// No number should be popped twice, and they should all be there.
		for (int i = 0; i < kNumItems; ++i)
		{
			for (int j = 0; j < kNumItems; ++j)
			{
				RL_ASSERT(poppedItems[j] != poppedItems[i] || i == j);
				RDE_ASSERT(poppedItems[j] != poppedItems[i] || i == j);
			}
		}
	}

    void thread(unsigned thread_index)
	{
        if (0 == thread_index)
        {
			for (int i = 0; i < kNumItems; ++i)
				q.Push(i);
			done.store(1, rl::memory_order_seq_cst);
        }
        else
        {
            int data = 0;
			rl::backoff b;
			while (done($) == 0)
				b.yield($);
			while (numPopped($) < kNumItems)
			{
				if (q.Pop(data))
				{
					const int i = numPopped.fetch_add(1, rl::memory_order_seq_cst);
					poppedItems[i] = data;
				}
				b.yield($);
			}
        }
	}
};

} // namespace

void SPMCQueue_Test_Run()
{
	// Random scheduler
	rl::simulate<SPMCQueue_Test>();

    //rl::test_params params;
    //params.search_type = rl::fair_context_bound_scheduler_type;
    //params.context_bound = 3;
    //rl::simulate<SPMCQueue_Test>(params);
}

#else

void SPMCQueue_Test_Run() {}

#endif // RDE_DBG_RELACY_TEST

#pragma warning(pop)

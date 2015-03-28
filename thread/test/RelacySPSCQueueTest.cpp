#pragma warning(push, 1)
#pragma warning(disable: 4311)
#pragma warning(disable: 4312)

#define RDE_DBG_RELACY_TEST 0

#if RDE_DBG_RELACY_TEST

#include <external/relacy/relacy_std.hpp>
#include "thread/SPSCQueue.h"

struct SPSCQueue_Test : rl::test_suite<SPSCQueue_Test, 2>
{
	rde::SPSCQueue<int, 8, false> q;
    void thread(unsigned thread_index)
	{
        if (0 == thread_index)
        {
            q.Push(11);
        }
        else
        {
            int data = 0;
			rl::backoff b;
			while (q.IsEmpty())
				b.yield($);
			q.Pop(data);
            RL_ASSERT(11 == data);
        }
	}
};

void SPSCQueue_Test_Run()
{
	// Random scheduler
	rl::simulate<SPSCQueue_Test>();

    rl::test_params params;
    params.search_type = rl::fair_context_bound_scheduler_type;
    params.context_bound = 3;
    rl::simulate<SPSCQueue_Test>(params);
}

#else

void SPSCQueue_Test_Run() {}

#endif // RDE_DBG_RELACY_TEST

#pragma warning(pop)

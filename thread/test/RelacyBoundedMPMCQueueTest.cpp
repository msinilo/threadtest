#pragma warning(push, 1)
#pragma warning(disable: 4311)
#pragma warning(disable: 4312)

#define RDE_DBG_RELACY_TEST 0

#if RDE_DBG_RELACY_TEST

#include <external/relacy/relacy_std.hpp>
#include "thread/BoundedMPMCQueue.h"

namespace
{
struct BoundedMPMCQueueTest : rl::test_suite<BoundedMPMCQueueTest, 8>
{
	rde::BoundedMPMCQueue<int> q;

	BoundedMPMCQueueTest(): q(1024) {}
	
	void before()
	{
	}
	void after()
	{
	}

    void thread(unsigned index)
    {
        if (0 == index)
        {
            for (size_t i = 0; i != 4; ++i)
            {
                q.Enqueue(10);
            }
            for (size_t i = 0; i != 5; ++i)
            {
				int data;
				bool popped = q.Dequeue(data);
                RL_ASSERT(!popped || data == 10);
            }
            for (size_t i = 0; i != 4; ++i)
            {
				q.Enqueue(10);
				int data;
				bool popped = q.Dequeue(data);
                RL_ASSERT(!popped || data == 10);
            }

            for (size_t i = 0; i != 4; ++i)
            {
                q.Enqueue(10);
                q.Enqueue(10);

				int data1, data2;
				bool popped = q.Dequeue(data1);
                RL_ASSERT(!popped || 10 == data1);
				popped = q.Dequeue(data2);
				RL_ASSERT(!popped || 10 == data2);
            }

            for (size_t i = 0; i != 14; ++i)
            {
                q.Enqueue(7);
				int data;
				bool popped = q.Dequeue(data);
                RL_ASSERT(!popped || 10 == data || 7 == data);
            }
		}
		else
		{
			rl::var<int> counter(0);	// dbg
            for (size_t i = 0; i != 4; ++i)
            {
				int data;
				bool popped = q.Dequeue(data);
                RL_ASSERT(!popped || data == 10 || data == 7);

				counter($) = counter($) + 1;
            }
		}
	}
};

} // namespace

void BoundedMPMCQueue_Test_Run()
{
	// Random scheduler
	rl::simulate<BoundedMPMCQueueTest>();

    //rl::test_params params;
    //params.search_type = rl::fair_context_bound_scheduler_type;
    //params.context_bound = 2;
    //rl::simulate<BoundedMPMCQueueTest>(params);
}

#else

void BoundedMPMCQueue_Test_Run() {}

#endif

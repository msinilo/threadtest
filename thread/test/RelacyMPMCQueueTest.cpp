#pragma warning(push, 1)
#pragma warning(disable: 4311)
#pragma warning(disable: 4312)

#define RDE_DBG_RELACY_TEST 0

#if RDE_DBG_RELACY_TEST

#include <external/relacy/relacy_std.hpp>
#include "thread/LockFreeQueue.h"

namespace
{

template <typename T>
struct StackAllocator
{
	void Init(int numElements)
	{
		m_mem($) = new T[numElements];
		m_top.store(0, rl::memory_order_relaxed);
		m_size = numElements;
	}
	void Close()
	{
		delete[] m_mem($);
	}

	T* Alloc()
	{
		T* ret = m_mem($);
		int top = m_top.fetch_add(1, rl::memory_order_seq_cst);
		RL_ASSERT(top < m_size);
		ret += top;

		return ret;
	}

	rl::var<T*>	m_mem;
	std::atomic<int> m_top;
	int m_size;
};

struct LockFreeQueueTest : rl::test_suite<LockFreeQueueTest, 8>
{
	struct QueueNode
	{
		int i;
		RDE_ATOMIC_VAR(QueueNode*) next;
	};
	rde::LockFreeQueue<QueueNode> q;
	StackAllocator<QueueNode> nodeAlloc;

	void before()
	{
		nodeAlloc.Init(1024);
	}
	void after()
	{
		nodeAlloc.Close();
	}

    void thread(unsigned index)
    {
        if (0 == index)
        {
            for (size_t i = 0; i != 4; ++i)
            {
				QueueNode* qn = nodeAlloc.Alloc();
				qn->i = 10;
                q.Enqueue(qn);
            }
            for (size_t i = 0; i != 5; ++i)
            {
				QueueNode* qn = q.Dequeue();
                RL_ASSERT(qn == 0 || qn->i == 10);
            }
            for (size_t i = 0; i != 4; ++i)
            {
				QueueNode* qn = nodeAlloc.Alloc();
				qn->i = 10;
				q.Enqueue(qn);
				QueueNode* qn2 = q.Dequeue();
                RL_ASSERT(qn2 == 0 || qn2->i == 10);
            }

            for (size_t i = 0; i != 4; ++i)
            {
				QueueNode* qn = nodeAlloc.Alloc();
				qn->i = 10;
                q.Enqueue(qn);
				qn = nodeAlloc.Alloc();
				qn->i = 10;
                q.Enqueue(qn);

				QueueNode* qn2 = q.Dequeue();
                RL_ASSERT(qn2 == 0 || 10 == qn2->i);
				QueueNode* qn3 = q.Dequeue();
				RL_ASSERT(qn3 == 0 || 10 == qn3->i);
            }

            for (size_t i = 0; i != 4; ++i)
            {
				QueueNode* qn = nodeAlloc.Alloc();
				qn->i = 10;
                q.Enqueue(qn);
				qn = nodeAlloc.Alloc();
				qn->i = 10;
                q.Enqueue(qn);
				qn = nodeAlloc.Alloc();
				qn->i = 10;
                q.Enqueue(qn);

				QueueNode* qn2 = q.Dequeue();
                RL_ASSERT(qn2 == 0 || 10 == qn2->i);
            }

            for (size_t i = 0; i != 14; ++i)
            {
				QueueNode* qn = nodeAlloc.Alloc();
				qn->i = 10;
                q.Enqueue(qn);
				QueueNode* qn2 = q.Dequeue();
                RL_ASSERT(qn2 == 0 || 10 == qn2->i);
            }
		}
		else
		{
			rl::var<int> counter(0);	// dbg
            for (size_t i = 0; i != 4; ++i)
            {
				QueueNode* qn = q.Dequeue();
                RL_ASSERT(qn == 0 || qn->i == 10);

				counter($) = counter($) + 1;
            }
		}
	}
};

} // namespace

void LockFreeQueue_Test_Run()
{
	// Random scheduler
	rl::simulate<LockFreeQueueTest>();

    //rl::test_params params;
    //params.search_type = rl::fair_context_bound_scheduler_type;
    //params.context_bound = 2;
    //rl::simulate<LockFreeQueueTest>(params);
}

#else

void LockFreeQueue_Test_Run() {}

#endif // RDE_DBG_RELACY_TEST

#pragma warning(pop)

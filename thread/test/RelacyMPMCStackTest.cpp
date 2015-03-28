#pragma warning(push, 1)
#pragma warning(disable: 4311)
#pragma warning(disable: 4312)

#define RDE_DBG_RELACY_TEST 0

#if RDE_DBG_RELACY_TEST

#include <external/relacy/relacy_std.hpp>
#include "thread/LockFreeStack.h"

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

struct LockFreeStackTest : rl::test_suite<LockFreeStackTest, 4>
{
	struct Node
	{
		int i;
		RDE_ATOMIC_VAR(Node*) next;
	};

	rde::LockFreeStack<Node>	s;
	StackAllocator<Node>	nodeAlloc;
    int producedCount;
    int consumedCount;

    void before()
    {
		nodeAlloc.Init(8);
        producedCount = 0;
        consumedCount = 0;
    }

    void after()
    {
        typedef rl::test_suite<LockFreeStackTest, 4> base_t;
        RL_ASSERT(base_t::params::thread_count == producedCount);
        RL_ASSERT(base_t::params::thread_count == consumedCount);

		nodeAlloc.Close();
    }

    void thread(unsigned /*index*/)
    {
		Node* n = nodeAlloc.Alloc();
		n->i = rand() + 1;
		s.Push(n);
        producedCount += 1;
        n = s.Pop();
        RL_ASSERT(n && n->i);
        consumedCount += 1;
    }
};

void LockFreeStack_Test_Run()
{
	// Random scheduler
	rl::simulate<LockFreeStackTest>();

	// 
//    rl::test_params params;
    //params.search_type = rl::fair_context_bound_scheduler_type;
    //params.context_bound = 2;
    //rl::simulate<LockFreeStackTest>(params);
}

#else

void LockFreeStack_Test_Run() {}

#endif // RDE_DBG_RELACY_TEST

#pragma warning(pop)

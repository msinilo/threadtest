#pragma once

#include "core/ThreadProfiler.h"

namespace rde
{
namespace ThreadPoolEvent
{
	enum Enum
	{
		TASK_FUNC_START	= ThreadProfiler::Event::USER_EVENT0,
		TASK_FUNC_END,
		SIGNAL_WORK,
		WAIT_FOR_WORK_START,
		WAIT_FOR_WORK_END,
		WORKER_THREAD_START,
		YIELD,
	};
}
}
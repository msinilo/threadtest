#ifndef CORE_CPU_H
#define CORE_CPU_H

#include "core/Config.h"

namespace rde
{
RDE_FORCEINLINE uint64 GetCPUTicks();
double GetCPUTicksPerSecond();
}

#if RDE_COMPILER_MSVC
#	include "core/msvc/MsvcCPU.h"
#else
#	error "Compiler not supported!";
#endif

#endif 

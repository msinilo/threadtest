#pragma once
#ifndef GATE_H
#define GATE_H

#include "core/LockGuard.h"
#include "core/Mutex.h"
#include "core/ThreadEvent.h"

namespace rde
{
class Gate
{
	typedef Mutex	MutexType;
public:
	typedef intptr_t	State;

	Gate():	m_state(0), m_mutex(2000), m_event(ThreadEvent::MANUAL_RESET) {}

	// Sets new state if old state == comparand.
	// Signals if old state was "disabled" (zero).
	void UpdateIfStateEqual(State newState, State comparand)
	{
		RDE_ASSERT(newState != 0 || comparand != 0);
		LockGuard<MutexType> lock(m_mutex);
		State oldState(m_state);
		if (oldState == comparand)
		{
			m_state = newState;
			if (!oldState)
				m_event.Signal();
			else if (!newState)
				m_event.Reset();
		}
	}
	// Sets new state if old state != comparand.
	// Signals if old state was "disabled" (zero).
	void UpdateIfStateNotEqual(State newState, State comparand)
	{
		RDE_ASSERT(newState != 0 || comparand != 0);
		LockGuard<MutexType> lock(m_mutex);
		State oldState(m_state);
		if (oldState != comparand)
		{
			m_state = newState;
			if (!oldState)
				m_event.Signal();
			else if (!newState)
				m_event.Reset();
		}
	}
	// Wait for state to become non-zero.
	void Wait() const
	{
		if (m_state == 0)
			m_event.WaitInfinite();
	}
	State GetState() const	{ return m_state; }

private:
	State		m_state;
	MutexType	m_mutex;
	ThreadEvent	m_event;
};
} // rde

#endif // GATE_H

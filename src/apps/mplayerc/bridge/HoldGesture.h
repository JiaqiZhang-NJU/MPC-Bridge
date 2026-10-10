// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ShortcutProfile.h"
#include <optional>

namespace Bridge {
// Platform-independent tap/hold arbitration. Own a copy so preset changes cannot dangle pointers.
class HoldGesture {
	enum class Phase { Idle, Pending, LongPress, Canceled };
	Phase m_phase = Phase::Idle;
	Binding m_binding;
	std::uint64_t m_started = 0;
public:
	bool Owns(std::uint16_t key) const { return m_phase != Phase::Idle && key == m_binding.key; }
	bool Pending() const { return m_phase == Phase::Pending; }
	const Binding& HeldBinding() const { return m_binding; }
	bool Begin(const Binding& binding, std::uint64_t now) {
		if (!binding.hold) { return false; }
		m_binding = binding;
		m_started = now;
		m_phase = Phase::Pending;
		return true;
	}
	bool Tick(std::uint64_t now) {
		if (!Pending() || now - m_started < m_binding.hold->delayMs) { return false; }
		m_phase = Phase::LongPress;
		return true;
	}
	std::optional<Binding> Release(std::uint64_t now) {
		std::optional<Binding> tap;
		if (Pending() && now - m_started < m_binding.hold->delayMs) { tap = m_binding; }
		m_phase = Phase::Idle;
		return tap;
	}
	void Cancel() {
		// Still swallow repeats and key-up; a fresh non-repeat down starts a new gesture.
		if (m_phase != Phase::Idle) { m_phase = Phase::Canceled; }
	}
};
}

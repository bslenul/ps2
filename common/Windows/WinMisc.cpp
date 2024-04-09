/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2021  PCSX2 Dev Team
 *
 *  PCSX2 is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU Lesser General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  PCSX2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with PCSX2.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#if defined(_WIN32)

#include "common/Pcsx2Defs.h"
#include "common/RedtapeWindows.h"
#include "common/StringUtil.h"
#include "common/Threading.h"
#include "common/General.h"
#include "common/WindowInfo.h"

#include "fmt/core.h"

#include <mmsystem.h>
#include <timeapi.h>
#include <VersionHelpers.h>

alignas(16) static LARGE_INTEGER lfreq;

// This gets leaked... oh well.
static thread_local HANDLE s_sleep_timer;
static thread_local bool s_sleep_timer_created = false;

static HANDLE GetSleepTimer()
{
	if (s_sleep_timer_created)
		return s_sleep_timer;

	s_sleep_timer_created = true;
	s_sleep_timer = CreateWaitableTimerEx(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
	if (!s_sleep_timer)
		s_sleep_timer = CreateWaitableTimer(nullptr, TRUE, nullptr);

	return s_sleep_timer;
}

void InitCPUTicks()
{
	QueryPerformanceFrequency(&lfreq);
}

u64 GetTickFrequency()
{
	return lfreq.QuadPart;
}

u64 GetCPUTicks()
{
	LARGE_INTEGER count;
	QueryPerformanceCounter(&count);
	return count.QuadPart;
}

void Threading::Sleep(int ms)
{
	::Sleep(ms);
}

void Threading::SleepUntil(u64 ticks)
{
	// This is definitely sub-optimal, but there's no way to sleep until a QPC timestamp on Win32.
	const s64 diff = static_cast<s64>(ticks - GetCPUTicks());
	if (diff <= 0)
		return;

	const HANDLE hTimer = GetSleepTimer();
	if (!hTimer)
		return;

	const u64 one_hundred_nanos_diff = (static_cast<u64>(diff) * 10000000ULL) / GetTickFrequency();
	if (one_hundred_nanos_diff == 0)
		return;

	LARGE_INTEGER fti;
	fti.QuadPart = -static_cast<s64>(one_hundred_nanos_diff);

	if (SetWaitableTimer(hTimer, &fti, 0, nullptr, nullptr, FALSE))
	{
		WaitForSingleObject(hTimer, INFINITE);
		return;
	}
}

#endif

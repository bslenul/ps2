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

#include "PrecompiledHeader.h"
#include "Common.h"
#include "R3000A.h"
#include "VUmicro.h"
#include "newVif.h"
#include "MTVU.h"

#include "Elfheader.h"

#include "common/Align.h"
#include "common/Console.h"
#include "common/StringUtil.h"

#include "CDVD/CDVD.h"
#include "GS/Renderers/Common/GSFunctionMap.h"
#include "ps2/BiosTools.h"

namespace HostMemoryMap
{
	// For debuggers
	extern "C" {
#ifdef _WIN32
	uptr EEmem, IOPmem, VUmem, EErec, IOPrec, VIF0rec, VIF1rec, mVU0rec, mVU1rec, SWjit, bumpAllocator;
#else
	uptr EEmem, IOPmem, VUmem, EErec, IOPrec, VIF0rec, VIF1rec, mVU0rec, mVU1rec, SWjit, bumpAllocator;
#endif
	}
} // namespace HostMemoryMap

/// Attempts to find a spot near static variables for the main memory
static VirtualMemoryManagerPtr makeMemoryManager(const char* file_mapping_name, size_t size, size_t offset_from_base)
{
#if defined(_WIN32)
	// Everything looks nicer when the start of all the sections is a nice round looking number.
	// Also reduces the variation in the address due to small changes in code.
	// Breaks ASLR but so does anything else that tries to make addresses constant for our debugging pleasure
	uptr codeBase = (uptr)(void*)makeMemoryManager / (1 << 28) * (1 << 28);

	// The allocation is ~640mb in size, slighly under 3*2^28.
	// We'll hope that the code generated for the PCSX2 executable stays under 512mb (which is likely)
	// On x86-64, code can reach 8*2^28 from its address [-6*2^28, 4*2^28] is the region that allows for code in the 640mb allocation to reach 512mb of code that either starts at codeBase or 256mb before it.
	// We start high and count down because on macOS code starts at the beginning of useable address space, so starting as far ahead as possible reduces address variations due to code size.  Not sure about other platforms.  Obviously this only actually affects what shows up in a debugger and won't affect performance or correctness of anything.
	for (int offset = 4; offset >= -6; offset--)
	{
		uptr base = codeBase + (offset << 28) + offset_from_base;
		// VTLB will throw a fit if we try to put EE main memory here
		if ((sptr)base < 0 || (sptr)(base + size - 1) < 0)
			continue;
		auto mgr = std::make_shared<VirtualMemoryManager>(file_mapping_name, base, size, /*upper_bounds=*/0, /*strict=*/true);
		if (mgr->IsOk())
			return mgr;
	}
#endif
	return std::make_shared<VirtualMemoryManager>(file_mapping_name, 0, size);
}

// --------------------------------------------------------------------------------------
//  SysReserveVM  (implementations)
// --------------------------------------------------------------------------------------
SysMainMemory::SysMainMemory()
	: m_mainMemory(makeMemoryManager("pcsx2", HostMemoryMap::MainSize, 0))
	, m_codeMemory(makeMemoryManager(nullptr, HostMemoryMap::CodeSize, HostMemoryMap::MainSize))
	, m_bumpAllocator(m_mainMemory, HostMemoryMap::bumpAllocatorOffset, HostMemoryMap::MainSize - HostMemoryMap::bumpAllocatorOffset)
{
	uptr main_base = (uptr)MainMemory()->GetBase();
	uptr code_base = (uptr)MainMemory()->GetBase();
	HostMemoryMap::EEmem = main_base + HostMemoryMap::EEmemOffset;
	HostMemoryMap::IOPmem = main_base + HostMemoryMap::IOPmemOffset;
	HostMemoryMap::VUmem = main_base + HostMemoryMap::VUmemOffset;
	HostMemoryMap::EErec = code_base + HostMemoryMap::EErecOffset;
	HostMemoryMap::IOPrec = code_base + HostMemoryMap::IOPrecOffset;
	HostMemoryMap::VIF0rec = code_base + HostMemoryMap::VIF0recOffset;
	HostMemoryMap::VIF1rec = code_base + HostMemoryMap::VIF1recOffset;
	HostMemoryMap::mVU0rec = code_base + HostMemoryMap::mVU0recOffset;
	HostMemoryMap::mVU1rec = code_base + HostMemoryMap::mVU1recOffset;
	HostMemoryMap::bumpAllocator = main_base + HostMemoryMap::bumpAllocatorOffset;
}

SysMainMemory::~SysMainMemory()
{
	Release();
}

bool SysMainMemory::Allocate()
{
	Console.WriteLn(Color_StrongBlue, "Allocating host memory for virtual systems...");
	m_ee.Assign(MainMemory());
	m_iop.Assign(MainMemory());
	m_vu.Assign(MainMemory());

	vtlb_Core_Alloc();

	return true;
}

void SysMainMemory::Reset()
{
	Console.WriteLn(Color_StrongBlue, "Resetting host memory for virtual systems...");
	m_ee.Reset();
	m_iop.Reset();
	m_vu.Reset();

	// Note: newVif is reset as part of other VIF structures.
	// Software is reset on the GS thread.
}

void SysMainMemory::Release()
{
	Console.WriteLn(Color_Blue, "Releasing host memory for virtual systems...");

	vtlb_Core_Free(); // Just to be sure... (calling order could result in it getting missed during Decommit).

	m_ee.Release();
	m_iop.Release();
	m_vu.Release();
}

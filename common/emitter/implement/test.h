/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2010  PCSX2 Dev Team
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

#pragma once

// Implementations found here: TEST + BTS/BT/BTC/BTR + BSF/BSR! (for lack of better location)

namespace x86Emitter
{

	// --------------------------------------------------------------------------------------
	//  xImpl_Test
	// --------------------------------------------------------------------------------------
	//
	struct xImpl_Test
	{
		void operator()(const xRegisterInt& to, const xRegisterInt& from) const;
		void operator()(const xIndirect64orLess& dest, int imm) const;
		void operator()(const xRegisterInt& to, int imm) const;
	};

	// --------------------------------------------------------------------------------------
	//  BSR
	// --------------------------------------------------------------------------------------
	// 16/32 operands are available.  No 8 bit ones, not that any of you cared, I bet.
	//
	struct xImpl_BitScan
	{
		// 0xbc [fwd] / 0xbd [rev]
		u16 Opcode;

		void operator()(const xRegister16or32or64& to, const xRegister16or32or64& from) const;
		void operator()(const xRegister16or32or64& to, const xIndirectVoid& sibsrc) const;
	};
} // End namespace x86Emitter

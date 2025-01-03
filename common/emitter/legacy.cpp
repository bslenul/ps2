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
/*
 * ix86 core v0.6.2
 * Authors: linuzappz <linuzappz@pcsx.net>
 *			alexey silinov
 *			goldfinger
 *			zerofrog(@gmail.com)
 *			cottonvibes(@gmail.com)
 */

//------------------------------------------------------------------
// ix86 legacy emitter functions
//------------------------------------------------------------------

#include "common/emitter/legacy_internal.h"

emitterT void ModRM(uint mod, uint reg, uint rm)
{
	xWrite8((mod << 6) | (reg << 3) | rm);
}

using namespace x86Emitter;

//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////
// From here on are instructions that have NOT been implemented in the new emitter.
//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////

static emitterT u8* J8Rel(int cc, int to)
{
	xWrite8(cc);
	xWrite8(to);
	return (u8*)(x86Ptr - 1);
}

static emitterT u32* J32Rel(int cc, u32 to)
{
	xWrite8(0x0F);
	xWrite8(cc);
	xWrite32(to);
	return (u32*)(x86Ptr - 4);
}

/********************/
/* IX86 instructions */
/********************/

////////////////////////////////////
// jump instructions		   /
////////////////////////////////////

/* jmp rel8 */
emitterT u8* JMP8(u8 to)
{
	xWrite8(0xEB);
	xWrite8(to);
	return x86Ptr - 1;
}

/* jmp rel32 */
emitterT u32* JMP32(uptr to)
{
	xWrite8(0xE9);
	xWrite32(to);
	return (u32*)(x86Ptr - 4);
}

/* je rel8 */
emitterT u8* JE8(u8 to)
{
	return J8Rel(0x74, to);
}

/* jz rel8 */
emitterT u8* JZ8(u8 to)
{
	return J8Rel(0x74, to);
}

/* jns rel8 */
emitterT u8* JNS8(u8 to)
{
	return J8Rel(0x79, to);
}

/* jg rel8 */
emitterT u8* JG8(u8 to)
{
	return J8Rel(0x7F, to);
}

/* jge rel8 */
emitterT u8* JGE8(u8 to)
{
	return J8Rel(0x7D, to);
}

/* jl rel8 */
emitterT u8* JL8(u8 to)
{
	return J8Rel(0x7C, to);
}

emitterT u8* JAE8(u8 to)
{
	return J8Rel(0x73, to);
}

/* jb rel8 */
emitterT u8* JB8(u8 to)
{
	return J8Rel(0x72, to);
}

/* jbe rel8 */
emitterT u8* JBE8(u8 to)
{
	return J8Rel(0x76, to);
}

/* jle rel8 */
emitterT u8* JLE8(u8 to)
{
	return J8Rel(0x7E, to);
}

/* jne rel8 */
emitterT u8* JNE8(u8 to)
{
	return J8Rel(0x75, to);
}

/* jnz rel8 */
emitterT u8* JNZ8(u8 to)
{
	return J8Rel(0x75, to);
}

/* je rel32 */
emitterT u32* JE32(u32 to)
{
	return J32Rel(0x84, to);
}

/* jz rel32 */
emitterT u32* JZ32(u32 to)
{
	return J32Rel(0x84, to);
}

/* jg rel32 */
emitterT u32* JG32(u32 to)
{
	return J32Rel(0x8F, to);
}

/* jge rel32 */
emitterT u32* JGE32(u32 to)
{
	return J32Rel(0x8D, to);
}

/* jl rel32 */
emitterT u32* JL32(u32 to)
{
	return J32Rel(0x8C, to);
}

/* jle rel32 */
emitterT u32* JLE32(u32 to)
{
	return J32Rel(0x8E, to);
}

/* jne rel32 */
emitterT u32* JNE32(u32 to)
{
	return J32Rel(0x85, to);
}

/* jnz rel32 */
emitterT u32* JNZ32(u32 to)
{
	return J32Rel(0x85, to);
}

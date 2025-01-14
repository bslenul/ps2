/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2023  PCSX2 Dev Team
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

#include <array>

#include "Global.h"
#include "../GS/GSVector.h"

__forceinline s32 V_Core::RevbGetIndexer(s32 offset)
{
	u32 start = EffectsStartA & 0x3f'ffff;
	u32 end   = (EffectsEndA & 0x3f'ffff) | 0xffff;
	u32 x     = ((Cycles >> 1) + offset) % ((end - start) + 1);
	return ((x + start) & 0xf'ffff);
}

StereoOut32 V_Core::DoReverb(StereoOut32 Input)
{
	if (EffectsStartA >= EffectsEndA)
	{
		StereoOut32 ret;
		ret.Left = ret.Right = 0;
		return ret;
	}

	Input.Left  = std::clamp(Input.Left, -0x8000, 0x7fff);
	Input.Right = std::clamp(Input.Right, -0x8000, 0x7fff);

	RevbDownBuf[0][RevbSampleBufPos] = Input.Left;
	RevbDownBuf[1][RevbSampleBufPos] = Input.Right;
	RevbDownBuf[0][RevbSampleBufPos | 64] = Input.Left;
	RevbDownBuf[1][RevbSampleBufPos | 64] = Input.Right;

	bool R = Cycles & 1;

	// Calculate the read/write addresses we'll be needing for this session of reverb.

	const u32 same_src = RevbGetIndexer(R ? Revb.SAME_R_SRC : Revb.SAME_L_SRC);
	const u32 same_dst = RevbGetIndexer(R ? Revb.SAME_R_DST : Revb.SAME_L_DST);
	const u32 same_prv = RevbGetIndexer(R ? Revb.SAME_R_DST - 1 : Revb.SAME_L_DST - 1);

	const u32 diff_src = RevbGetIndexer(R ? Revb.DIFF_L_SRC : Revb.DIFF_R_SRC);
	const u32 diff_dst = RevbGetIndexer(R ? Revb.DIFF_R_DST : Revb.DIFF_L_DST);
	const u32 diff_prv = RevbGetIndexer(R ? Revb.DIFF_R_DST - 1 : Revb.DIFF_L_DST - 1);

	const u32 comb1_src = RevbGetIndexer(R ? Revb.COMB1_R_SRC : Revb.COMB1_L_SRC);
	const u32 comb2_src = RevbGetIndexer(R ? Revb.COMB2_R_SRC : Revb.COMB2_L_SRC);
	const u32 comb3_src = RevbGetIndexer(R ? Revb.COMB3_R_SRC : Revb.COMB3_L_SRC);
	const u32 comb4_src = RevbGetIndexer(R ? Revb.COMB4_R_SRC : Revb.COMB4_L_SRC);

	const u32 apf1_src = RevbGetIndexer(R ? (Revb.APF1_R_DST - Revb.APF1_SIZE) : (Revb.APF1_L_DST - Revb.APF1_SIZE));
	const u32 apf1_dst = RevbGetIndexer(R ? Revb.APF1_R_DST : Revb.APF1_L_DST);
	const u32 apf2_src = RevbGetIndexer(R ? (Revb.APF2_R_DST - Revb.APF2_SIZE) : (Revb.APF2_L_DST - Revb.APF2_SIZE));
	const u32 apf2_dst = RevbGetIndexer(R ? Revb.APF2_R_DST : Revb.APF2_L_DST);

	// -----------------------------------------
	//          Optimized IRQ Testing !
	// -----------------------------------------

	// This test is enhanced by using the reverb effects area begin/end test as a
	// shortcut, since all buffer addresses are within that area.  If the IRQA isn't
	// within that zone then the "bulk" of the test is skipped, so this should only
	// be a slowdown on a few evil games.

	for (int i = 0; i < 2; i++)
	{
		if (FxEnable && Cores[i].IRQEnable && ((Cores[i].IRQA >= EffectsStartA) && (Cores[i].IRQA <= EffectsEndA)))
		{
			if ((Cores[i].IRQA == same_src) || (Cores[i].IRQA == diff_src) ||
				(Cores[i].IRQA == same_dst) || (Cores[i].IRQA == diff_dst) ||
				(Cores[i].IRQA == same_prv) || (Cores[i].IRQA == diff_prv) ||

				(Cores[i].IRQA == comb1_src) || (Cores[i].IRQA == comb2_src) ||
				(Cores[i].IRQA == comb3_src) || (Cores[i].IRQA == comb4_src) ||

				(Cores[i].IRQA == apf1_dst) || (Cores[i].IRQA == apf1_src) ||
				(Cores[i].IRQA == apf2_dst) || (Cores[i].IRQA == apf2_src))
				has_to_call_irq[i] = true;
		}
	}

	// Reverb algorithm pretty much directly ripped from http://drhell.web.fc2.com/ps1/
	// minus the 35 step FIR which just seems to break things.

	s32 apf2;

#define MUL(x, y) ((x) * (y) >> 15)
	s32 in   = MUL(R ? Revb.IN_COEF_R : Revb.IN_COEF_L, ReverbDownsample(*this, R));

	s32 same = MUL(Revb.IIR_VOL, in + MUL(Revb.WALL_VOL, _spu2mem[same_src]) - _spu2mem[same_prv]) + _spu2mem[same_prv];
	s32 diff = MUL(Revb.IIR_VOL, in + MUL(Revb.WALL_VOL, _spu2mem[diff_src]) - _spu2mem[diff_prv]) + _spu2mem[diff_prv];

	s32 out  = MUL(Revb.COMB1_VOL, _spu2mem[comb1_src]) + MUL(Revb.COMB2_VOL, _spu2mem[comb2_src]) + MUL(Revb.COMB3_VOL, _spu2mem[comb3_src]) + MUL(Revb.COMB4_VOL, _spu2mem[comb4_src]);

	s32 apf1 = out - MUL(Revb.APF1_VOL, _spu2mem[apf1_src]);
	out      = _spu2mem[apf1_src] + MUL(Revb.APF1_VOL, apf1);
	apf2     = out - MUL(Revb.APF2_VOL, _spu2mem[apf2_src]);
	out      = _spu2mem[apf2_src] + MUL(Revb.APF2_VOL, apf2);

	// According to no$psx the effects always run but don't always write back, see check in V_Core::Mix
	if (FxEnable)
	{
		_spu2mem[same_dst] = std::clamp(same, -0x8000, 0x7fff);
		_spu2mem[diff_dst] = std::clamp(diff, -0x8000, 0x7fff);
		_spu2mem[apf1_dst] = std::clamp(apf1, -0x8000, 0x7fff);
		_spu2mem[apf2_dst] = std::clamp(apf2, -0x8000, 0x7fff);
	}

	out = std::clamp(out, -0x8000, 0x7fff);

	RevbUpBuf[R][RevbSampleBufPos] = out;
	RevbUpBuf[!R][RevbSampleBufPos] = 0;

	RevbUpBuf[R][RevbSampleBufPos | 64] = out;
	RevbUpBuf[!R][RevbSampleBufPos | 64] = 0;

	RevbSampleBufPos = (RevbSampleBufPos + 1) & 63;

	return ReverbUpsample(*this);
}

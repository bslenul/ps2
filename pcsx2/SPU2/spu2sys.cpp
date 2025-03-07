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

// ======================================================================================
//  spu2sys.cpp -- Emulation module for the SPU2 'virtual machine'
// ======================================================================================
// This module contains (most!) stuff which is directly related to SPU2 emulation.
// Contents should be cross-platform compatible whenever possible.

#include "../IopCounters.h"
#include "../IopDma.h"
#include "../IopHw.h"
#include "../R3000A.h"
#include "Dma.h"
#include "Global.h"
#include "spu2.h"
#include <libretro.h>

extern retro_audio_sample_t sample_cb;

s16 spu2regs[0x010000 / sizeof(s16)];
s16 _spu2mem[0x200000 / sizeof(s16)];

V_Core Cores[2];
V_SPDIF Spdif;

StereoOut32 DCFilterIn, DCFilterOut;
u16 OutPos;
u16 InputPos;
u32 Cycles;

int PlayMode;

bool has_to_call_irq[2]     = { false, false };
bool has_to_call_irq_dma[2] = { false, false };
StereoOut32 (*ReverbUpsample)(V_Core& core);
s32 (*ReverbDownsample)(V_Core& core, bool right);

static bool psxmode = false;

// writes a signed value to the SPU2 ram
// Invalidates the ADPCM cache in the process.
__forceinline void spu2M_Write(u32 addr, s16 value)
{
	// Make sure the cache is invalidated:
	// (note to self : addr address WORDs, not bytes)

	addr &= 0xfffff;
	if (addr >= SPU2_DYN_MEMLINE)
	{
		const int cacheIdx = addr / pcm_WordsPerBlock;
		pcm_cache_data[cacheIdx].Validated = false;
	}
	*GetMemPtr(addr) = value;
}

void V_Core::Init(int index)
{
	ReverbDownsample = MULTI_ISA_SELECT(ReverbDownsample);
	ReverbUpsample = MULTI_ISA_SELECT(ReverbUpsample);

	// Explicitly initializing variables instead.
	Mute = false;
	DMABits = 0;
	NoiseClk = 0;
	NoiseCnt = 0;
	NoiseOut = 0;
	AutoDMACtrl = 0;
	InputDataLeft = 0;
	InputPosWrite = 0x100;
	InputDataProgress = 0;
	InputDataTransferred = 0;
	LastEffect.Left = 0;
	LastEffect.Right = 0;
	CoreEnabled = 0;
	AttrBit0 = 0;
	DmaMode = 0;
	DMAPtr = nullptr;
	KeyOn = 0;
	OutPos = 0;
	DCFilterIn = {};
	DCFilterOut = {};

	psxmode = false;
	psxSoundDataTransferControl = 0;
	psxSPUSTAT = 0;

	const int c  = Index = index;

	Regs.STATX   = 0;
	Regs.ATTR    = 0;
	ExtVol.Left  = 0x7FFF;
	ExtVol.Right = 0x7FFF;
	InpVol.Left  = 0x7FFF;
	InpVol.Right = 0x7FFF;
	FxVol.Left   = 0;
	FxVol.Right  = 0;
	MasterVol.Left.Reg_VOL = 0;
	MasterVol.Left.Counter = 0;
	MasterVol.Left.Value   = 0;
	MasterVol.Right.Reg_VOL = 0;
	MasterVol.Right.Counter = 0;
	MasterVol.Right.Value   = 0;

	memset(&DryGate, -1, sizeof(DryGate));
	memset(&WetGate, -1, sizeof(WetGate));
	DryGate.ExtL = 0;
	DryGate.ExtR = 0;
	if (!c)
	{
		WetGate.ExtL = 0;
		WetGate.ExtR = 0;
	}

	Regs.MMIX = c ? 0xFFC : 0xFF0; // PS2 confirmed (f3c and f30 after BIOS ran, ffc and ff0 after sdinit)
	Regs.VMIXL = 0xFFFFFF;
	Regs.VMIXR = 0xFFFFFF;
	Regs.VMIXEL = 0xFFFFFF;
	Regs.VMIXER = 0xFFFFFF;
	EffectsStartA = c ? 0xFFFF8 : 0xEFFF8;
	EffectsEndA = c ? 0xFFFFF : 0xEFFFF;

	FxEnable = false; // Uninitialized it's 0 for both cores. Resetting libs however may set this to 0 or 1.
	// These are real PS2 values, mainly constant apart from a few bits: 0x3220EAA4, 0x40505E9C.
	// These values mean nothing.  They do not reflect the actual address the SPU2 is testing,
	// it would seem that reading the IRQA register returns the last written value, not the
	// value of the internal register.  Rewriting the registers with their current values changes
	// whether interrupts fire (they do while uninitialised, but do not when rewritten).
	// The exact boot value is unknown and probably unknowable, but it seems to be somewhere
	// in the input or output areas, so we're using 0x800.
	// F1 2005 is known to rely on an uninitialised IRQA being an address which will be hit.
	IRQA = 0x800;
	IRQEnable = false; // PS2 confirmed

	for (uint v = 0; v < NumVoices; ++v)
	{
		VoiceGates[v].DryL = -1;
		VoiceGates[v].DryR = -1;
		VoiceGates[v].WetL = -1;
		VoiceGates[v].WetR = -1;

		Voices[v].Volume.Left.Reg_VOL = 0;
		Voices[v].Volume.Left.Counter = 0;
		Voices[v].Volume.Left.Value   = 0;
		Voices[v].Volume.Right.Reg_VOL = 0;
		Voices[v].Volume.Right.Counter = 0;
		Voices[v].Volume.Right.Value   = 0;
		Voices[v].SCurrent = 28;

		Voices[v].ADSR.Counter = 0;
		Voices[v].ADSR.Value = 0;
		Voices[v].ADSR.Phase = 0;
		Voices[v].Pitch = 0x3FFF;
		Voices[v].NextA = 0x2801;
		Voices[v].StartA = 0x2800;
		Voices[v].LoopStartA = 0x2800;
	}

	DMAICounter = 0;
	AdmaInProgress = false;

	Regs.STATX = 0x80;
	Regs.ENDX = 0xffffff; // PS2 confirmed

	RevbSampleBufPos = 0;
	memset(RevbDownBuf, 0, sizeof(RevbDownBuf));
	memset(RevbUpBuf, 0, sizeof(RevbUpBuf));
}

#define TICKINTERVAL 768
#define SANITYINTERVAL 4800
/* TICKINTERVAL * SANITYINTERVAL = 3686400 */
#define SAMPLECOUNT 3686400 

__forceinline void TimeUpdate(u32 cClocks)
{
	u32 dClocks = cClocks - lClocks;

	// Sanity Checks:
	//  It's not totally uncommon for the IOP's clock to jump backwards a cycle or two, and in
	//  such cases we just want to ignore the TimeUpdate call.

	if (dClocks > (u32)-15)
		return;

	//  But if for some reason our clock value seems way off base (typically due to bad dma
	//  timings from PCSX2), just mix out a little bit, skip the rest, and hope the ship
	//  "rights" itself later on.

	if (dClocks > SAMPLECOUNT)
	{
		dClocks = SAMPLECOUNT;
		lClocks = cClocks - dClocks;
	}

	short snd_buffer[2];

	snd_buffer[0] = snd_buffer[1] = 0;

	//Update Mixing Progress
	while (dClocks >= TICKINTERVAL)
	{
		for (int i = 0; i < 2; i++)
		{
			if (has_to_call_irq[i])
			{
				has_to_call_irq[i] = false;
				if (!(Spdif.Info & (4 << i)) && Cores[i].IRQEnable)
				{
					Spdif.Info |= (4 << i);
					spu2Irq();
				}
			}
		}

		dClocks -= TICKINTERVAL;
		lClocks += TICKINTERVAL;
		Cycles++;

		// Start Queued Voices, they start after 2T (Tested on real HW)
		for(int c = 0; c < 2; c++)
		{
			for (int v = 0; v < 24; v++)
			{
				if (Cores[c].KeyOn & (1 << v))
				{
					V_Voice& vc(Cores[c].Voices[v]);
					if ((Cycles - vc.PlayCycle) >= 2) /* Start queued voice? */
					{
						if (vc.StartA & 7)
							vc.StartA = (vc.StartA + 0xFFFF8) + 0x8;

						vc.ADSR.Phase   = PHASE_ATTACK;
						vc.ADSR.Counter = 0;
						vc.ADSR.Value   = 0;

						vc.SCurrent     = 28;
						vc.LoopMode     = 0;

						// When SP >= 0 the next sample will be grabbed, we don't want this to happen
						// instantly because in the case of pitch being 0 we want to delay getting
						// the next block header. This is a hack to work around the fact that unlike
						// the HW we don't update the block header on every cycle.
						vc.SP           = -1;

						vc.LoopFlags    = 0;
						vc.NextA        = vc.StartA | 1;
						vc.Prev1        = 0;
						vc.Prev2        = 0;

						vc.PV1 = vc.PV2 = 0;
						vc.PV3 = vc.PV4 = 0;
						vc.NextCrest    = -0x8000;
						Cores[c].KeyOn &= ~(1 << v);
					}
				}
			}
		}
		Mix(&snd_buffer[0], &snd_buffer[1]);
	}

	if (sample_cb && snd_buffer[0] != 0 && snd_buffer[1] != 0)
		sample_cb(snd_buffer[0], snd_buffer[1]);

	//Update DMA4 interrupt delay counter
	if (Cores[0].DMAICounter > 0 && (psxRegs.cycle - Cores[0].LastClock) > 0)
	{
		const u32 amt = std::min(psxRegs.cycle - Cores[0].LastClock, (u32)Cores[0].DMAICounter);
		Cores[0].DMAICounter -= amt;
		Cores[0].LastClock = psxRegs.cycle;
		if(!Cores[0].AdmaInProgress)
			HW_DMA4_MADR += amt / 2;

		if (Cores[0].DMAICounter <= 0)
		{
			for (int i = 0; i < 2; i++)
			{
				if (has_to_call_irq_dma[i])
				{
					has_to_call_irq_dma[i] = false;
					if (!(Spdif.Info & (4 << i)) && Cores[i].IRQEnable)
					{
						Spdif.Info |= (4 << i);
						spu2Irq();
					}
				}
			}

			if (((Cores[0].AutoDMACtrl & 1) != 1) && Cores[0].ReadSize)
			{
				if (Cores[0].IsDMARead)
					Cores[0].FinishDMAread();
				else
					Cores[0].FinishDMAwrite();
			}

			if (Cores[0].DMAICounter <= 0)
			{
				HW_DMA4_MADR = HW_DMA4_TADR;
				if (Cores[0].DmaMode)
					Cores[0].Regs.STATX |= 0x80;
				Cores[0].Regs.STATX &= ~0x400;
				Cores[0].TSA = Cores[0].ActiveTSA;
				if (HW_DMA4_CHCR & 0x01000000)
				{
					HW_DMA4_CHCR &= ~0x01000000;
					psxDmaInterrupt(4);
				}
			}
		}
		else
		{
			if (((psxCounters[6].startCycle + psxCounters[6].deltaCycles) - psxRegs.cycle) > (u32)Cores[0].DMAICounter)
			{
				psxCounters[6].startCycle  = psxRegs.cycle;
				psxCounters[6].deltaCycles = Cores[0].DMAICounter;

				psxNextDeltaCounter -= (psxRegs.cycle - psxNextStartCounter);
				psxNextStartCounter = psxRegs.cycle;
				if (psxCounters[6].deltaCycles < psxNextDeltaCounter)
					psxNextDeltaCounter = psxCounters[6].deltaCycles;
			}
		}
	}

	//Update DMA7 interrupt delay counter
	if (Cores[1].DMAICounter > 0 && (psxRegs.cycle - Cores[1].LastClock) > 0)
	{
		const u32 amt = std::min(psxRegs.cycle - Cores[1].LastClock, (u32)Cores[1].DMAICounter);
		Cores[1].DMAICounter -= amt;
		Cores[1].LastClock = psxRegs.cycle;
		if (!Cores[1].AdmaInProgress)
			HW_DMA7_MADR += amt / 2;

		if (Cores[1].DMAICounter <= 0)
		{
			for (int i = 0; i < 2; i++)
			{
				if (has_to_call_irq_dma[i])
				{
					has_to_call_irq_dma[i] = false;
					if (!(Spdif.Info & (4 << i)) && Cores[i].IRQEnable)
					{
						Spdif.Info |= (4 << i);
						spu2Irq();
					}
				}
			}

			if (((Cores[1].AutoDMACtrl & 2) != 2) && Cores[1].ReadSize)
			{
				if (Cores[1].IsDMARead)
					Cores[1].FinishDMAread();
				else
					Cores[1].FinishDMAwrite();
			}

			if (Cores[1].DMAICounter <= 0)
			{
				HW_DMA7_MADR = HW_DMA7_TADR;
				if (Cores[1].DmaMode)
					Cores[1].Regs.STATX |= 0x80;
				Cores[1].Regs.STATX &= ~0x400;
				Cores[1].TSA = Cores[1].ActiveTSA;
				if (HW_DMA7_CHCR & 0x01000000)
				{
					HW_DMA7_CHCR &= ~0x01000000;
					psxDmaInterrupt2(0);
				}
			}
		}
		else
		{
			if (((psxCounters[6].startCycle + psxCounters[6].deltaCycles) - psxRegs.cycle) > (u32)Cores[1].DMAICounter)
			{
				psxCounters[6].startCycle  = psxRegs.cycle;
				psxCounters[6].deltaCycles = Cores[1].DMAICounter;

				psxNextDeltaCounter -= (psxRegs.cycle - psxNextStartCounter);
				psxNextStartCounter = psxRegs.cycle;
				if (psxCounters[6].deltaCycles < psxNextDeltaCounter)
					psxNextDeltaCounter = psxCounters[6].deltaCycles;
			}
		}
	}
}

__forceinline void UpdateSpdifMode(void)
{
	if (Spdif.Out & 0x4) // use 24/32bit PCM data streaming
	{
		PlayMode = 8;
		return;
	}

	if (Spdif.Out & SPDIF_OUT_BYPASS)
	{
		PlayMode = 2;
		if (!(Spdif.Mode & SPDIF_MODE_BYPASS_BITSTREAM))
			PlayMode = 4; //bitstream bypass
	}
	else
	{
		PlayMode = 0; //normal processing
		if (Spdif.Out & SPDIF_OUT_PCM)
			PlayMode = 1;
	}
}

#define map_spu1to2(addr) ((addr) * 4 + ((addr) >= 0x200 ? 0xc0000 : 0))
#define map_spu2to1(addr) (((addr) - ((addr) >= 0xc0000 ? 0xc0000 : 0)) / 4)

void V_Core::WriteRegPS1(u32 mem, u16 value)
{
	const u32 reg = mem & 0xffff;

	if ((reg >= 0x1c00) && (reg < 0x1d80))
	{
		//voice values
		u8 voice = ((reg - 0x1c00) >> 4);
		const u8 vval = reg & 0xf;
		switch (vval)
		{
			case 0x0: //VOLL (Volume L)
				Voices[voice].Volume.Left.Reg_VOL = value;
				if (!Voices[voice].Volume.Left.Enable)
					Voices[voice].Volume.Left.Value = (s16)(value << 1);
				break;
			case 0x2: //VOLR (Volume R)
				Voices[voice].Volume.Right.Reg_VOL = value;
				if (!Voices[voice].Volume.Right.Enable)
					Voices[voice].Volume.Right.Value = (s16)(value << 1);
				break;
			case 0x4:
				Voices[voice].Pitch = value;
				break;
			case 0x6:
				Voices[voice].StartA = map_spu1to2(value);
				break;

			case 0x8: // ADSR1 (Envelope)
				Voices[voice].ADSR.regADSR1 = value;
				ADSR_UpdateCache(Voices[voice].ADSR);
				break;

			case 0xa: // ADSR2 (Envelope)
				Voices[voice].ADSR.regADSR2 = value;
				ADSR_UpdateCache(Voices[voice].ADSR);
				break;
			case 0xc: // Voice 0..23 ADSR Current Volume
				// not commonly set by games
				Voices[voice].ADSR.Value = value;
				break;
			case 0xe:
				Voices[voice].LoopStartA = map_spu1to2(value);
				break;
			default:
				break;
		}
	}

	else
		switch (reg)
		{
			case 0x1d80: //         Mainvolume left
				MasterVol.Left.Reg_VOL = value;
				if (!MasterVol.Left.Enable)
					MasterVol.Left.Value = (s16)(value << 1);
				break;

			case 0x1d82: //         Mainvolume right
				MasterVol.Right.Reg_VOL = value;
				if (!MasterVol.Right.Enable)
					MasterVol.Right.Value = (s16)(value << 1);
				break;

			case 0x1d84: //         Reverberation depth left
				FxVol.Left = (s16)value;
				break;

			case 0x1d86: //         Reverberation depth right
				FxVol.Right = (s16)value;
				break;

			case 0x1d88: //         Voice ON  (0-15)
				tbl_reg_writes[((REG_S_KON) & 0x7ff) / 2](value);
				break;
			case 0x1d8a: //         Voice ON  (16-23)
				tbl_reg_writes[((REG_S_KON + 2) & 0x7ff) / 2](value);
				break;

			case 0x1d8c: //         Voice OFF (0-15)
				tbl_reg_writes[((REG_S_KOFF) & 0x7ff) / 2](value);
				break;
			case 0x1d8e: //         Voice OFF (16-23)
				tbl_reg_writes[((REG_S_KOFF + 2) & 0x7ff) / 2](value);
				break;

			case 0x1d90: //         Channel FM (pitch lfo) mode (0-15)
				tbl_reg_writes[((REG_S_PMON) & 0x7ff) / 2](value);
				break;

			case 0x1d92: //         Channel FM (pitch lfo) mode (16-23)
				tbl_reg_writes[((REG_S_PMON + 2) & 0x7ff) / 2](value);
				break;


			case 0x1d94: //         Channel Noise mode (0-15)
				tbl_reg_writes[((REG_S_NON) & 0x7ff) / 2](value);
				break;

			case 0x1d96: //         Channel Noise mode (16-23)
				tbl_reg_writes[((REG_S_NON + 2) & 0x7ff) / 2](value);
				break;

			case 0x1d98: //         1F801D98h - Voice 0..23 Reverb mode aka Echo On (EON) (R/W)
				tbl_reg_writes[((REG_S_VMIXEL) & 0x7ff) / 2](value);
				tbl_reg_writes[((REG_S_VMIXER) & 0x7ff) / 2](value);
				break;

			case 0x1d9a: //         1F801D98h + 2 - Voice 0..23 Reverb mode aka Echo On (EON) (R/W)
				tbl_reg_writes[((REG_S_VMIXEL + 2) & 0x7ff) / 2](value);
				tbl_reg_writes[((REG_S_VMIXER + 2) & 0x7ff) / 2](value);
				break;

			case 0x1d9c: // Voice 0..15 ON/OFF (status) (ENDX) (R) // writeable but hw overrides it shortly after
			case 0x1d9e: //         // Voice 15..23 ON/OFF (status) (ENDX) (R) // writeable but hw overrides it shortly after
				break;

			case 0x1da2: //         Reverb work area start
				EffectsStartA = map_spu1to2(value);
				break;

			case 0x1da4:
				IRQA = map_spu1to2(value);
				break;

			case 0x1da6:
				TSA = map_spu1to2(value);
				break;

			case 0x1da8: // Spu Write to Memory
				Cores[0].ActiveTSA = Cores[0].TSA;
				if (Cores[0].IRQEnable && (Cores[0].IRQA <= Cores[0].ActiveTSA))
				{
					has_to_call_irq[0] = true;
					spu2Irq();
				}
				DmaWrite(value);
				break;

			case 0x1daa:
				tbl_reg_writes[((REG_C_ATTR) & 0x7ff) / 2](value);
				break;

			case 0x1dac: // 1F801DACh - Sound RAM Data Transfer Control (should be 0004h)
				psxSoundDataTransferControl = value;
				break;

			case 0x1dae: // 1F801DAEh - SPU Status Register (SPUSTAT) (R)
						 // The SPUSTAT register should be treated read-only (writing is possible in so far that the written
						 // value can be read-back for a short moment, however, thereafter the hardware is overwriting that value).
						 //Regs.STATX = value;
			case 0x1DB0: // 1F801DB0h 4  CD Volume Left/Right
			case 0x1DB2:
			case 0x1DB4: // 1F801DB4h 4  Extern Volume Left / Right
			case 0x1DB6:
			case 0x1DB8: // 1F801DB8h 4  Current Main Volume Left/Right
			case 0x1DBA:
			case 0x1DBC: // 1F801DBCh 4  Unknown? (R/W)
			case 0x1DBE:
				break;

			case 0x1DC0:
				Revb.APF1_SIZE = value * 4;
				break;
			case 0x1DC2:
				Revb.APF2_SIZE = value * 4;
				break;
			case 0x1DC4:
				Revb.IIR_VOL = value;
				break;
			case 0x1DC6:
				Revb.COMB1_VOL = value;
				break;
			case 0x1DC8:
				Revb.COMB2_VOL = value;
				break;
			case 0x1DCA:
				Revb.COMB3_VOL = value;
				break;
			case 0x1DCC:
				Revb.COMB4_VOL = value;
				break;
			case 0x1DCE:
				Revb.WALL_VOL = value;
				break;
			case 0x1DD0:
				Revb.APF1_VOL = value;
				break;
			case 0x1DD2:
				Revb.APF2_VOL = value;
				break;
			case 0x1DD4:
				Revb.SAME_L_DST = value * 4;
				break;
			case 0x1DD6:
				Revb.SAME_R_DST = value * 4;
				break;
			case 0x1DD8:
				Revb.COMB1_L_SRC = value * 4;
				break;
			case 0x1DDA:
				Revb.COMB1_R_SRC = value * 4;
				break;
			case 0x1DDC:
				Revb.COMB2_L_SRC = value * 4;
				break;
			case 0x1DDE:
				Revb.COMB2_R_SRC = value * 4;
				break;
			case 0x1DE0:
				Revb.SAME_L_SRC = value * 4;
				break;
			case 0x1DE2:
				Revb.SAME_R_SRC = value * 4;
				break;
			case 0x1DE4:
				Revb.DIFF_L_DST = value * 4;
				break;
			case 0x1DE6:
				Revb.DIFF_R_DST = value * 4;
				break;
			case 0x1DE8:
				Revb.COMB3_L_SRC = value * 4;
				break;
			case 0x1DEA:
				Revb.COMB3_R_SRC = value * 4;
				break;
			case 0x1DEC:
				Revb.COMB4_L_SRC = value * 4;
				break;
			case 0x1DEE:
				Revb.COMB4_R_SRC = value * 4;
				break;
			case 0x1DF0:
				Revb.DIFF_L_SRC = value * 4;
				break; // DIFF_R_SRC and DIFF_L_SRC supposedly swapped on SPU2
			case 0x1DF2:
				Revb.DIFF_R_SRC = value * 4;
				break; // but I don't believe it! (games in psxmode sound better unswapped)
			case 0x1DF4:
				Revb.APF1_L_DST = value * 4;
				break;
			case 0x1DF6:
				Revb.APF1_R_DST = value * 4;
				break;
			case 0x1DF8:
				Revb.APF2_L_DST = value * 4;
				break;
			case 0x1DFA:
				Revb.APF2_R_DST = value * 4;
				break;
			case 0x1DFC:
				Revb.IN_COEF_L = value;
				break;
			case 0x1DFE:
				Revb.IN_COEF_R = value;
				break;
		}

	spu2Ru16(mem) = value;
}

u16 V_Core::ReadRegPS1(u32 mem)
{
	u16 value = spu2Ru16(mem);

	const u32 reg = mem & 0xffff;

	if ((reg >= 0x1c00) && (reg < 0x1d80))
	{
		//voice values
		const u8 voice = ((reg - 0x1c00) >> 4);
		const u8 vval = reg & 0xf;
		switch (vval)
		{
			case 0x0: //VOLL (Volume L)
				return Voices[voice].Volume.Left.Reg_VOL;
			case 0x2: //VOLR (Volume R)
				return Voices[voice].Volume.Right.Reg_VOL;
			case 0x4:
				return Voices[voice].Pitch;
			case 0x6:
				return map_spu2to1(Voices[voice].StartA);
			case 0x8:
				return Voices[voice].ADSR.regADSR1;
			case 0xa:
				return Voices[voice].ADSR.regADSR2;
			case 0xc: // Voice 0..23 ADSR Current Volume
				return Voices[voice].ADSR.Value;
			case 0xe:
				return map_spu2to1(Voices[voice].LoopStartA);
			default:
				break;
		}
	}
	else
		switch (reg)
		{
			case 0x1d80:
				return MasterVol.Left.Value;
			case 0x1d82:
				return MasterVol.Right.Value;
			case 0x1d84:
				return FxVol.Left;
			case 0x1d86:
				return FxVol.Right;
			case 0x1d88:
			case 0x1d8a:
			case 0x1d8c:
			case 0x1d8e:
				return 0;
			case 0x1d90:
				return (Regs.PMON & 0xFFFF);
			case 0x1d92:
				return (Regs.PMON >> 16);
			case 0x1d94:
				return (Regs.NON & 0xFFFF);
			case 0x1d96:
				return (Regs.NON >> 16);
			case 0x1d98:
				return (Regs.VMIXEL & 0xFFFF);
			case 0x1d9a:
				return (Regs.VMIXEL >> 16);
			case 0x1d9c:
				return Regs.ENDX & 0xFFFF;
			case 0x1d9e:
				return Regs.ENDX >> 16;
			case 0x1da2:
				return map_spu2to1(EffectsStartA);
			case 0x1da4:
				return map_spu2to1(IRQA);
			case 0x1da6:
				return map_spu2to1(TSA);
			case 0x1da8:
				ActiveTSA = TSA;
				return DmaRead();
			case 0x1daa:
				return Cores[0].Regs.ATTR;
			case 0x1dac: // 1F801DACh - Sound RAM Data Transfer Control (should be 0004h)
				return psxSoundDataTransferControl;
			case 0x1dae:
				return Cores[0].Regs.STATX;
		}

	return value;
}

static void StartVoices(V_Core& thiscore, int core, u32 value)
{
	thiscore.KeyOn     |=  value;
	thiscore.Regs.ENDX &= ~value;

	for (u8 vc = 0; vc < V_Core::NumVoices; vc++)
	{
		if (!((value >> vc) & 1))
			continue;

		if ((Cycles - thiscore.Voices[vc].PlayCycle) < 2)
			continue;

		thiscore.Voices[vc].PlayCycle        = Cycles;
		thiscore.Voices[vc].LoopCycle        = Cycles - 1; // Get it out of the start range as to not confuse it
		thiscore.Voices[vc].PendingLoopStart = false;
	}
}

static void StopVoices(V_Core& thiscore, int core, u32 value)
{
	for (u8 vc = 0; vc < V_Core::NumVoices; vc++)
	{
		if (!((value >> vc) & 1))
			continue;

		if (Cycles - thiscore.Voices[vc].PlayCycle < 2)
			continue;

		ADSR_Release(thiscore.Voices[vc].ADSR);
	}
}

template <int CoreIdx, int VoiceIdx, int param>
static void RegWrite_VoiceParams(u16 value)
{
	const int core = CoreIdx;
	const int voice = VoiceIdx;

	V_Voice& thisvoice = Cores[core].Voices[voice];

	switch (param)
	{
		case 0: //VOLL (Volume L)
			thisvoice.Volume.Left.Reg_VOL = value;
			if (!thisvoice.Volume.Left.Enable)
				thisvoice.Volume.Left.Value = (s16)(value << 1);
			break;
		
		case 1: //VOLR (Volume R)
			thisvoice.Volume.Right.Reg_VOL = value;
			if (!thisvoice.Volume.Right.Enable)
				thisvoice.Volume.Right.Value = (s16)(value << 1);
			break;

		case 2:
			thisvoice.Pitch = value;
			break;

		case 3: // ADSR1 (Envelope)
			thisvoice.ADSR.regADSR1 = value;
			ADSR_UpdateCache(thisvoice.ADSR);
			break;

		case 4: // ADSR2 (Envelope)
			thisvoice.ADSR.regADSR2 = value;
			ADSR_UpdateCache(thisvoice.ADSR);
			break;

			// REG_VP_ENVX, REG_VP_VOLXL and REG_VP_VOLXR are all writable, only ENVX has any effect when written to.
			// Colin McRae Rally 2005 triggers case 5 (ADSR), but it doesn't produce issues enabled or disabled.

		case 5:
			thisvoice.ADSR.Value = value;
			break;
		case 6:
		case 7:
		default:
			break;
	}
}

template <int CoreIdx, int VoiceIdx, int address>
static void RegWrite_VoiceAddr(u16 value)
{
	const int core = CoreIdx;
	const int voice = VoiceIdx;

	V_Voice& thisvoice = Cores[core].Voices[voice];

	switch (address)
	{
		case 0: // SSA (Waveform Start Addr) (hiword, 4 bits only)
			thisvoice.StartA = ((u32)(value & 0x0F) << 16) | (thisvoice.StartA & 0xFFF8);
			break;

		case 1: // SSA (loword)
			thisvoice.StartA = (thisvoice.StartA & 0x0F0000) | (value & 0xFFF8);
			break;

		case 2:
			{
				u32* LoopReg;
				if ((Cycles - thisvoice.PlayCycle) < 4 && (int)(thisvoice.LoopCycle - thisvoice.PlayCycle) < 0)
				{
					LoopReg = &thisvoice.PendingLoopStartA;
					thisvoice.PendingLoopStart = true;
				}
				else
				{
					LoopReg = &thisvoice.LoopStartA;
					thisvoice.LoopMode = 1;
				}

				*LoopReg = ((u32)(value & 0x0F) << 16) | (*LoopReg & 0xFFF8);
			}
			break;

		case 3:
			{
				u32* LoopReg;
				if ((Cycles - thisvoice.PlayCycle) < 4 && (int)(thisvoice.LoopCycle - thisvoice.PlayCycle) < 0)
				{
					LoopReg = &thisvoice.PendingLoopStartA;
					thisvoice.PendingLoopStart = true;
				}
				else
				{
					LoopReg = &thisvoice.LoopStartA;
					thisvoice.LoopMode = 1;
				}

				*LoopReg = (*LoopReg & 0x0F0000) | (value & 0xFFF8);
			}
			break;


			// NAX is confirmed to be writable on hardware (decoder will start decoding at new location).
			//
			// Example games:
			// FlatOut
			// Soul Reaver 2
			// Wallace And Gromit: Curse Of The Were-Rabbit.

		case 4:
			thisvoice.NextA = ((u32)(value & 0x0F) << 16) | (thisvoice.NextA & 0xFFF8) | 1;
			thisvoice.SCurrent = 28;
			break;

		case 5:
			thisvoice.NextA = (thisvoice.NextA & 0x0F0000) | (value & 0xFFF8) | 1;
			thisvoice.SCurrent = 28;
			break;
	}
}

template <int CoreIdx, int cAddr>
static void RegWrite_Core(u16 value)
{
	const int omem = cAddr;
	const int core = CoreIdx;
	V_Core& thiscore = Cores[core];

	switch (omem)
	{
		case REG__1AC:
			// ----------------------------------------------------------------------------
			// 0x1ac / 0x5ac : direct-write to DMA address : special register (undocumented)
			// ----------------------------------------------------------------------------
			// On the GS, DMAs are actually pushed through a hardware register.  Chances are the
			// SPU works the same way, and "technically" *all* DMA data actually passes through
			// the HW registers at 0x1ac (core0) and 0x5ac (core1).  We handle normal DMAs in
			// optimized block copy fashion elsewhere, but some games will write this register
			// directly, so handle those here:

			// Performance Note: The PS2 Bios uses this extensively right before booting games,
			// causing massive slowdown if we don't shortcut it here.
			thiscore.ActiveTSA = thiscore.TSA;
			for (int i = 0; i < 2; i++)
			{
				if (Cores[i].IRQEnable && (Cores[i].IRQA == thiscore.ActiveTSA))
					has_to_call_irq[i] = true;
			}
			thiscore.DmaWrite(value);
			break;

		case REG_C_ATTR:
		{
			bool irqe = thiscore.IRQEnable;
			u8 oldDmaMode = thiscore.DmaMode;

			thiscore.AttrBit0 = (value >> 0) & 0x01;  //1 bit
			thiscore.DMABits = (value >> 1) & 0x07;   //3 bits
			thiscore.DmaMode = (value >> 4) & 0x03;   //2 bit (not necessary, we get the direction from the iop)
			thiscore.IRQEnable = (value >> 6) & 0x01; //1 bit
			thiscore.FxEnable = (value >> 7) & 0x01;  //1 bit
			thiscore.NoiseClk = (value >> 8) & 0x3f;  //6 bits
			thiscore.Mute = 0;
			// no clue
			thiscore.Regs.ATTR = value & 0xffff;

			if (!thiscore.DmaMode && !(thiscore.Regs.STATX & 0x400))
				thiscore.Regs.STATX &= ~0x80;
			else if(!oldDmaMode && thiscore.DmaMode)
				thiscore.Regs.STATX |= 0x80;

			thiscore.ActiveTSA = thiscore.TSA;

			if (thiscore.IRQEnable != irqe)
			{
				if (!thiscore.IRQEnable)
					Spdif.Info &= ~(4 << thiscore.Index);
			}
		}
		break;

		case REG_S_PMON:
			for (int vc = 1; vc < 16; ++vc)
				thiscore.Voices[vc].Modulated = (value >> vc) & 1;
			((u16*)&thiscore.Regs.PMON)[0] = value;
			break;

		case (REG_S_PMON + 2):
			for (int vc = 0; vc < 8; ++vc)
				thiscore.Voices[vc + 16].Modulated = (value >> vc) & 1;
			((u16*)&thiscore.Regs.PMON)[1] = value;
			break;

		case REG_S_NON:
			for (int vc = 0; vc < 16; ++vc)
				thiscore.Voices[vc].Noise = (value >> vc) & 1;
			((u16*)&thiscore.Regs.NON)[0] = value;
			break;

		case (REG_S_NON + 2):
			for (int vc = 0; vc < 8; ++vc)
				thiscore.Voices[vc + 16].Noise = (value >> vc) & 1;
			((u16*)&thiscore.Regs.NON)[1] = value;
			break;

		case REG_S_VMIXL:
			{
				const u32 result = thiscore.Regs.VMIXL;
				((u16*)&thiscore.Regs.VMIXL)[0] = value;
				if (result == thiscore.Regs.VMIXL)
					break;
				for (uint vc = 0, vx = 1; vc < 16; ++vc, vx <<= 1)
					thiscore.VoiceGates[vc].DryL = (value & vx) ? -1 : 0;
			}
			break;

		case (REG_S_VMIXL + 2):
			{
				const u32 result = thiscore.Regs.VMIXL;
				((u16*)&thiscore.Regs.VMIXL)[1] = value;
				if (result == thiscore.Regs.VMIXL)
					break;
				for (uint vc = 16, vx = 1; vc < 24; ++vc, vx <<= 1)
					thiscore.VoiceGates[vc].DryL = (value & vx) ? -1 : 0;
			}
			break;

		case REG_S_VMIXEL:
			{
				const u32 result = thiscore.Regs.VMIXEL;
				((u16*)&thiscore.Regs.VMIXEL)[0] = value;
				if (result == thiscore.Regs.VMIXEL)
					break;
				for (uint vc = 0, vx = 1; vc < 16; ++vc, vx <<= 1)
					thiscore.VoiceGates[vc].WetL = (value & vx) ? -1 : 0;
			}
			break;

		case (REG_S_VMIXEL + 2):
			{
				const u32 result = thiscore.Regs.VMIXEL;
				((u16*)&thiscore.Regs.VMIXEL)[1] = value;
				if (result == thiscore.Regs.VMIXEL)
					break;
				for (uint vc = 16, vx = 1; vc < 24; ++vc, vx <<= 1)
					thiscore.VoiceGates[vc].WetL = (value & vx) ? -1 : 0;
			}
			break;

		case REG_S_VMIXR:
			{
				const u32 result = thiscore.Regs.VMIXR;
				((u16*)&thiscore.Regs.VMIXR)[0] = value;
				if (result == thiscore.Regs.VMIXR)
					break;
				for (uint vc = 0, vx = 1; vc < 16; ++vc, vx <<= 1)
					thiscore.VoiceGates[vc].DryR = (value & vx) ? -1 : 0;
			}
			break;

		case (REG_S_VMIXR + 2):
			{
				const u32 result = thiscore.Regs.VMIXR;
				((u16*)&thiscore.Regs.VMIXR)[1] = value;
				if (result == thiscore.Regs.VMIXR)
					break;
				for (uint vc = 16, vx = 1; vc < 24; ++vc, vx <<= 1)
					thiscore.VoiceGates[vc].DryR = (value & vx) ? -1 : 0;
			}
			break;

		case REG_S_VMIXER:
			{
				const u32 result = thiscore.Regs.VMIXER;
				((u16*)&thiscore.Regs.VMIXER)[0] = value;
				if (result == thiscore.Regs.VMIXER)
					break;
				for (uint vc = 0, vx = 1; vc < 16; ++vc, vx <<= 1)
					thiscore.VoiceGates[vc].WetR = (value & vx) ? -1 : 0;
			}
			break;

		case (REG_S_VMIXER + 2):
			{
				const u32 result = thiscore.Regs.VMIXER;
				((u16*)&thiscore.Regs.VMIXER)[1] = value;
				if (result == thiscore.Regs.VMIXER)
					break;
				for (uint vc = 16, vx = 1; vc < 24; ++vc, vx <<= 1)
					thiscore.VoiceGates[vc].WetR = (value & vx) ? -1 : 0;
			}
			break;

		case REG_P_MMIX:
		{
			// Each MMIX gate is assigned either 0 or 0xffffffff depending on the status
			// of the MMIX bits.  I use -1 below as a shorthand for 0xffffffff. :)

			const int vx = value & ((core == 0) ? 0xFF0 : 0xFFF);
			thiscore.WetGate.ExtR = (vx & 0x001) ? -1 : 0;
			thiscore.WetGate.ExtL = (vx & 0x002) ? -1 : 0;
			thiscore.DryGate.ExtR = (vx & 0x004) ? -1 : 0;
			thiscore.DryGate.ExtL = (vx & 0x008) ? -1 : 0;
			thiscore.WetGate.InpR = (vx & 0x010) ? -1 : 0;
			thiscore.WetGate.InpL = (vx & 0x020) ? -1 : 0;
			thiscore.DryGate.InpR = (vx & 0x040) ? -1 : 0;
			thiscore.DryGate.InpL = (vx & 0x080) ? -1 : 0;
			thiscore.WetGate.SndR = (vx & 0x100) ? -1 : 0;
			thiscore.WetGate.SndL = (vx & 0x200) ? -1 : 0;
			thiscore.DryGate.SndR = (vx & 0x400) ? -1 : 0;
			thiscore.DryGate.SndL = (vx & 0x800) ? -1 : 0;
			thiscore.Regs.MMIX = value;
		}
		break;

		case (REG_S_KON + 2):
			// Optimization: Games like to write zero to the KeyOn reg a lot, so shortcut
			// this loop if value is zero.
			if ((((u32)value) << 16) != 0)
				StartVoices(thiscore, core, ((u32)value) << 16);
			spu2regs[omem >> 1 | core * 0x200] = value;
			break;

		case REG_S_KON:
			// Optimization: Games like to write zero to the KeyOn reg a lot, so shortcut
			// this loop if value is zero.
			if ((u32)value != 0)
				StartVoices(thiscore, core, ((u32)value));
			spu2regs[omem >> 1 | core * 0x200] = value;
			break;

		case (REG_S_KOFF + 2):
			if ((((u32)value) << 16) != 0)
				StopVoices(thiscore, core, ((u32)value) << 16);
			spu2regs[omem >> 1 | core * 0x200] = value;
			break;

		case REG_S_KOFF:
			if (((u32)value) != 0)
				StopVoices(thiscore, core, ((u32)value));
			spu2regs[omem >> 1 | core * 0x200] = value;
			break;

		case REG_S_ENDX:
			thiscore.Regs.ENDX &= 0xff0000;
			break;

		case (REG_S_ENDX + 2):
			thiscore.Regs.ENDX &= 0xffff;
			break;

		case REG_S_ADMAS:
			// hack for ps1driver which writes -1 (and never turns the adma off after psxlogo).
			// adma isn't available in psx mode either
			if (value == 32767)
			{
				psxmode = true;
				Cores[1].FxEnable = 0;
				Cores[1].EffectsStartA = 0x7FFF8; // park core1 effect area in inaccessible mem
				Cores[1].EffectsEndA = 0x7FFFF;
				for (uint v = 0; v < 24; ++v)
				{
					Cores[1].Voices[v].Volume.Left.Reg_VOL  = 0;
					Cores[1].Voices[v].Volume.Left.Counter  = 0;
					Cores[1].Voices[v].Volume.Left.Value    = 0;
					Cores[1].Voices[v].Volume.Right.Reg_VOL = 0;
					Cores[1].Voices[v].Volume.Right.Counter = 0;
					Cores[1].Voices[v].Volume.Right.Value   = 0;
					Cores[1].Voices[v].SCurrent = 28;

					Cores[1].Voices[v].ADSR.Value = 0;
					Cores[1].Voices[v].ADSR.Phase = 0;
					Cores[1].Voices[v].Pitch = 0x0;
					Cores[1].Voices[v].NextA = 0x6FFFF;
					Cores[1].Voices[v].StartA = 0x6FFFF;
					Cores[1].Voices[v].LoopStartA = 0x6FFFF;
					Cores[1].Voices[v].Modulated = 0;
				}
				return;
			}
			thiscore.AutoDMACtrl = value;
			if (!(value & 0x3) && thiscore.AdmaInProgress)
			{
				// Kill the current transfer so it doesn't continue
				thiscore.AdmaInProgress = 0;
				thiscore.InputDataLeft = 0;
				thiscore.DMAICounter = 0;
				thiscore.InputDataTransferred = 0;

				// Not accurate behaviour but shouldn't hurt for now, need to run some tests
				// to see why Prince of Persia Warrior Within buzzes when going in to the map
				// since it starts an ADMA of music, then kills ADMA and input DMA
				// without disabling ADMA read mode or clearing the buffer.
				for (int i = 0; i < 0x200; i++)
				{
					GetMemPtr(0x2000 + (thiscore.Index << 10))[i] = 0;
					GetMemPtr(0x2200 + (thiscore.Index << 10))[i] = 0;
				}
			}
			break;

		default:
		{
			const int addr = omem | ((core == 1) ? 0x400 : 0);
			*(regtable[addr >> 1]) = value;
		}
		break;
	}
}

template <int CoreIdx, int addr>
static void RegWrite_CoreExt(u16 value)
{
	V_Core& thiscore = Cores[CoreIdx];
	const int core = CoreIdx;

	switch (addr)
	{
			// Master Volume Address Write!

		case REG_P_MVOLL:
			thiscore.MasterVol.Left.Reg_VOL = value;
			if (!thiscore.MasterVol.Left.Enable)
				thiscore.MasterVol.Left.Value = (s16)(value << 1);
			break;
		case REG_P_MVOLR:
			thiscore.MasterVol.Right.Reg_VOL = value;
			if (!thiscore.MasterVol.Right.Enable)
				thiscore.MasterVol.Right.Value = (s16)(value << 1);
			break;

		case REG_P_EVOLL:
			thiscore.FxVol.Left = (s16)(value);
			break;

		case REG_P_EVOLR:
			thiscore.FxVol.Right = (s16)(value);
			break;

		case REG_P_AVOLL:
			thiscore.ExtVol.Left = (s16)(value);
			break;

		case REG_P_AVOLR:
			thiscore.ExtVol.Right = (s16)(value);
			break;

		case REG_P_BVOLL:
			thiscore.InpVol.Left = (s16)(value);
			break;

		case REG_P_BVOLR:
			thiscore.InpVol.Right = (s16)(value);
			break;

			// MVOLX has been confirmed to not be allowed to be written to, so cases have been added as a no-op.
			// Tokyo Xtreme Racer Zero triggers this code, caused left side volume to be reduced.

		case REG_P_MVOLXL:
		case REG_P_MVOLXR:
			break;

		default:
		{
			const int raddr = addr + ((core == 1) ? 0x28 : 0);
			*(regtable[raddr >> 1]) = value;
		}
		break;
	}
}


template <int addr>
static void RegWrite_SPDIF(u16 value)
{
	*(regtable[addr >> 1]) = value;
	UpdateSpdifMode();
}

template <int addr>
static void RegWrite_Raw(u16 value)
{
	*(regtable[addr >> 1]) = value;
}

static void RegWrite_Null(u16 value)
{
}

// --------------------------------------------------------------------------------------
//  Macros for tbl_reg_writes
// --------------------------------------------------------------------------------------
#define VoiceParamsSet(core, voice)                                                 \
	RegWrite_VoiceParams<core, voice, 0>, RegWrite_VoiceParams<core, voice, 1>,     \
		RegWrite_VoiceParams<core, voice, 2>, RegWrite_VoiceParams<core, voice, 3>, \
		RegWrite_VoiceParams<core, voice, 4>, RegWrite_VoiceParams<core, voice, 5>, \
		RegWrite_VoiceParams<core, voice, 6>, RegWrite_VoiceParams<core, voice, 7>

#define VoiceParamsCore(core)                                                                                   \
	VoiceParamsSet(core, 0), VoiceParamsSet(core, 1), VoiceParamsSet(core, 2), VoiceParamsSet(core, 3),         \
		VoiceParamsSet(core, 4), VoiceParamsSet(core, 5), VoiceParamsSet(core, 6), VoiceParamsSet(core, 7),     \
		VoiceParamsSet(core, 8), VoiceParamsSet(core, 9), VoiceParamsSet(core, 10), VoiceParamsSet(core, 11),   \
		VoiceParamsSet(core, 12), VoiceParamsSet(core, 13), VoiceParamsSet(core, 14), VoiceParamsSet(core, 15), \
		VoiceParamsSet(core, 16), VoiceParamsSet(core, 17), VoiceParamsSet(core, 18), VoiceParamsSet(core, 19), \
		VoiceParamsSet(core, 20), VoiceParamsSet(core, 21), VoiceParamsSet(core, 22), VoiceParamsSet(core, 23)

#define VoiceAddrSet(core, voice)                                               \
	RegWrite_VoiceAddr<core, voice, 0>, RegWrite_VoiceAddr<core, voice, 1>,     \
		RegWrite_VoiceAddr<core, voice, 2>, RegWrite_VoiceAddr<core, voice, 3>, \
		RegWrite_VoiceAddr<core, voice, 4>, RegWrite_VoiceAddr<core, voice, 5>

#define CoreParamsPair(core, omem) \
	RegWrite_Core<core, omem>, RegWrite_Core<core, ((omem) + 2)>

#define REGRAW(addr) RegWrite_Raw<addr>

// --------------------------------------------------------------------------------------
//  tbl_reg_writes  - Register Write Function Invocation LUT
// --------------------------------------------------------------------------------------

typedef void RegWriteHandler(u16 value);
RegWriteHandler* const tbl_reg_writes[0x401] =
	{
		VoiceParamsCore(0), // 0x000 -> 0x180
		CoreParamsPair(0, REG_S_PMON),
		CoreParamsPair(0, REG_S_NON),
		CoreParamsPair(0, REG_S_VMIXL),
		CoreParamsPair(0, REG_S_VMIXEL),
		CoreParamsPair(0, REG_S_VMIXR),
		CoreParamsPair(0, REG_S_VMIXER),

		RegWrite_Core<0, REG_P_MMIX>,
		RegWrite_Core<0, REG_C_ATTR>,

		CoreParamsPair(0, REG_A_IRQA),
		CoreParamsPair(0, REG_S_KON),
		CoreParamsPair(0, REG_S_KOFF),
		CoreParamsPair(0, REG_A_TSA),
		CoreParamsPair(0, REG__1AC),

		RegWrite_Core<0, REG_S_ADMAS>,
		REGRAW(0x1b2),

		REGRAW(0x1b4), REGRAW(0x1b6),
		REGRAW(0x1b8), REGRAW(0x1ba),
		REGRAW(0x1bc), REGRAW(0x1be),

		// 0x1c0!

		VoiceAddrSet(0, 0), VoiceAddrSet(0, 1), VoiceAddrSet(0, 2), VoiceAddrSet(0, 3), VoiceAddrSet(0, 4), VoiceAddrSet(0, 5),
		VoiceAddrSet(0, 6), VoiceAddrSet(0, 7), VoiceAddrSet(0, 8), VoiceAddrSet(0, 9), VoiceAddrSet(0, 10), VoiceAddrSet(0, 11),
		VoiceAddrSet(0, 12), VoiceAddrSet(0, 13), VoiceAddrSet(0, 14), VoiceAddrSet(0, 15), VoiceAddrSet(0, 16), VoiceAddrSet(0, 17),
		VoiceAddrSet(0, 18), VoiceAddrSet(0, 19), VoiceAddrSet(0, 20), VoiceAddrSet(0, 21), VoiceAddrSet(0, 22), VoiceAddrSet(0, 23),

		CoreParamsPair(0, REG_A_ESA),

		CoreParamsPair(0, R_APF1_SIZE),   //       0x02E4		// Feedback Source A
		CoreParamsPair(0, R_APF2_SIZE),   //       0x02E8		// Feedback Source B
		CoreParamsPair(0, R_SAME_L_DST),  //    0x02EC
		CoreParamsPair(0, R_SAME_R_DST),  //    0x02F0
		CoreParamsPair(0, R_COMB1_L_SRC), //     0x02F4
		CoreParamsPair(0, R_COMB1_R_SRC), //     0x02F8
		CoreParamsPair(0, R_COMB2_L_SRC), //     0x02FC
		CoreParamsPair(0, R_COMB2_R_SRC), //     0x0300
		CoreParamsPair(0, R_SAME_L_SRC),  //     0x0304
		CoreParamsPair(0, R_SAME_R_SRC),  //     0x0308
		CoreParamsPair(0, R_DIFF_L_DST),  //    0x030C
		CoreParamsPair(0, R_DIFF_R_DST),  //    0x0310
		CoreParamsPair(0, R_COMB3_L_SRC), //     0x0314
		CoreParamsPair(0, R_COMB3_R_SRC), //     0x0318
		CoreParamsPair(0, R_COMB4_L_SRC), //     0x031C
		CoreParamsPair(0, R_COMB4_R_SRC), //     0x0320
		CoreParamsPair(0, R_DIFF_L_SRC),  //     0x0324
		CoreParamsPair(0, R_DIFF_R_SRC),  //     0x0328
		CoreParamsPair(0, R_APF1_L_DST),  //    0x032C
		CoreParamsPair(0, R_APF1_R_DST),  //    0x0330
		CoreParamsPair(0, R_APF2_L_DST),  //    0x0334
		CoreParamsPair(0, R_APF2_R_DST),  //    0x0338

		RegWrite_Core<0, REG_A_EEA>, RegWrite_Null,

		CoreParamsPair(0, REG_S_ENDX), //       0x0340	// End Point passed flag
		RegWrite_Core<0, REG_P_STATX>, //      0x0344 	// Status register?

		//0x346 here
		REGRAW(0x346),
		REGRAW(0x348), REGRAW(0x34A), REGRAW(0x34C), REGRAW(0x34E),
		REGRAW(0x350), REGRAW(0x352), REGRAW(0x354), REGRAW(0x356),
		REGRAW(0x358), REGRAW(0x35A), REGRAW(0x35C), REGRAW(0x35E),
		REGRAW(0x360), REGRAW(0x362), REGRAW(0x364), REGRAW(0x366),
		REGRAW(0x368), REGRAW(0x36A), REGRAW(0x36C), REGRAW(0x36E),
		REGRAW(0x370), REGRAW(0x372), REGRAW(0x374), REGRAW(0x376),
		REGRAW(0x378), REGRAW(0x37A), REGRAW(0x37C), REGRAW(0x37E),
		REGRAW(0x380), REGRAW(0x382), REGRAW(0x384), REGRAW(0x386),
		REGRAW(0x388), REGRAW(0x38A), REGRAW(0x38C), REGRAW(0x38E),
		REGRAW(0x390), REGRAW(0x392), REGRAW(0x394), REGRAW(0x396),
		REGRAW(0x398), REGRAW(0x39A), REGRAW(0x39C), REGRAW(0x39E),
		REGRAW(0x3A0), REGRAW(0x3A2), REGRAW(0x3A4), REGRAW(0x3A6),
		REGRAW(0x3A8), REGRAW(0x3AA), REGRAW(0x3AC), REGRAW(0x3AE),
		REGRAW(0x3B0), REGRAW(0x3B2), REGRAW(0x3B4), REGRAW(0x3B6),
		REGRAW(0x3B8), REGRAW(0x3BA), REGRAW(0x3BC), REGRAW(0x3BE),
		REGRAW(0x3C0), REGRAW(0x3C2), REGRAW(0x3C4), REGRAW(0x3C6),
		REGRAW(0x3C8), REGRAW(0x3CA), REGRAW(0x3CC), REGRAW(0x3CE),
		REGRAW(0x3D0), REGRAW(0x3D2), REGRAW(0x3D4), REGRAW(0x3D6),
		REGRAW(0x3D8), REGRAW(0x3DA), REGRAW(0x3DC), REGRAW(0x3DE),
		REGRAW(0x3E0), REGRAW(0x3E2), REGRAW(0x3E4), REGRAW(0x3E6),
		REGRAW(0x3E8), REGRAW(0x3EA), REGRAW(0x3EC), REGRAW(0x3EE),
		REGRAW(0x3F0), REGRAW(0x3F2), REGRAW(0x3F4), REGRAW(0x3F6),
		REGRAW(0x3F8), REGRAW(0x3FA), REGRAW(0x3FC), REGRAW(0x3FE),

		// AND... we reached 0x400!
		// Last verse, same as the first:

		VoiceParamsCore(1), // 0x000 -> 0x180
		CoreParamsPair(1, REG_S_PMON),
		CoreParamsPair(1, REG_S_NON),
		CoreParamsPair(1, REG_S_VMIXL),
		CoreParamsPair(1, REG_S_VMIXEL),
		CoreParamsPair(1, REG_S_VMIXR),
		CoreParamsPair(1, REG_S_VMIXER),

		RegWrite_Core<1, REG_P_MMIX>,
		RegWrite_Core<1, REG_C_ATTR>,

		CoreParamsPair(1, REG_A_IRQA),
		CoreParamsPair(1, REG_S_KON),
		CoreParamsPair(1, REG_S_KOFF),
		CoreParamsPair(1, REG_A_TSA),
		CoreParamsPair(1, REG__1AC),

		RegWrite_Core<1, REG_S_ADMAS>,
		REGRAW(0x5b2),

		REGRAW(0x5b4), REGRAW(0x5b6),
		REGRAW(0x5b8), REGRAW(0x5ba),
		REGRAW(0x5bc), REGRAW(0x5be),

		// 0x1c0!

		VoiceAddrSet(1, 0), VoiceAddrSet(1, 1), VoiceAddrSet(1, 2), VoiceAddrSet(1, 3), VoiceAddrSet(1, 4), VoiceAddrSet(1, 5),
		VoiceAddrSet(1, 6), VoiceAddrSet(1, 7), VoiceAddrSet(1, 8), VoiceAddrSet(1, 9), VoiceAddrSet(1, 10), VoiceAddrSet(1, 11),
		VoiceAddrSet(1, 12), VoiceAddrSet(1, 13), VoiceAddrSet(1, 14), VoiceAddrSet(1, 15), VoiceAddrSet(1, 16), VoiceAddrSet(1, 17),
		VoiceAddrSet(1, 18), VoiceAddrSet(1, 19), VoiceAddrSet(1, 20), VoiceAddrSet(1, 21), VoiceAddrSet(1, 22), VoiceAddrSet(1, 23),

		CoreParamsPair(1, REG_A_ESA),

		CoreParamsPair(1, R_APF1_SIZE),   //       0x02E4		// Feedback Source A
		CoreParamsPair(1, R_APF2_SIZE),   //       0x02E8		// Feedback Source B
		CoreParamsPair(1, R_SAME_L_DST),  //    0x02EC
		CoreParamsPair(1, R_SAME_R_DST),  //    0x02F0
		CoreParamsPair(1, R_COMB1_L_SRC), //     0x02F4
		CoreParamsPair(1, R_COMB1_R_SRC), //     0x02F8
		CoreParamsPair(1, R_COMB2_L_SRC), //     0x02FC
		CoreParamsPair(1, R_COMB2_R_SRC), //     0x0300
		CoreParamsPair(1, R_SAME_L_SRC),  //     0x0304
		CoreParamsPair(1, R_SAME_R_SRC),  //     0x0308
		CoreParamsPair(1, R_DIFF_L_DST),  //    0x030C
		CoreParamsPair(1, R_DIFF_R_DST),  //    0x0310
		CoreParamsPair(1, R_COMB3_L_SRC), //     0x0314
		CoreParamsPair(1, R_COMB3_R_SRC), //     0x0318
		CoreParamsPair(1, R_COMB4_L_SRC), //     0x031C
		CoreParamsPair(1, R_COMB4_R_SRC), //     0x0320
		CoreParamsPair(1, R_DIFF_R_SRC),  //     0x0324
		CoreParamsPair(1, R_DIFF_L_SRC),  //     0x0328
		CoreParamsPair(1, R_APF1_L_DST),  //    0x032C
		CoreParamsPair(1, R_APF1_R_DST),  //    0x0330
		CoreParamsPair(1, R_APF2_L_DST),  //    0x0334
		CoreParamsPair(1, R_APF2_R_DST),  //    0x0338

		RegWrite_Core<1, REG_A_EEA>, RegWrite_Null,

		CoreParamsPair(1, REG_S_ENDX), //       0x0340	// End Point passed flag
		RegWrite_Core<1, REG_P_STATX>, //      0x0344 	// Status register?

		REGRAW(0x746),
		REGRAW(0x748), REGRAW(0x74A), REGRAW(0x74C), REGRAW(0x74E),
		REGRAW(0x750), REGRAW(0x752), REGRAW(0x754), REGRAW(0x756),
		REGRAW(0x758), REGRAW(0x75A), REGRAW(0x75C), REGRAW(0x75E),

		// ------ -------

		RegWrite_CoreExt<0, REG_P_MVOLL>,  //     0x0760		// Master Volume Left
		RegWrite_CoreExt<0, REG_P_MVOLR>,  //     0x0762		// Master Volume Right
		RegWrite_CoreExt<0, REG_P_EVOLL>,  //     0x0764		// Effect Volume Left
		RegWrite_CoreExt<0, REG_P_EVOLR>,  //     0x0766		// Effect Volume Right
		RegWrite_CoreExt<0, REG_P_AVOLL>,  //     0x0768		// Core External Input Volume Left  (Only Core 1)
		RegWrite_CoreExt<0, REG_P_AVOLR>,  //     0x076A		// Core External Input Volume Right (Only Core 1)
		RegWrite_CoreExt<0, REG_P_BVOLL>,  //     0x076C 		// Sound Data Volume Left
		RegWrite_CoreExt<0, REG_P_BVOLR>,  //     0x076E		// Sound Data Volume Right
		RegWrite_CoreExt<0, REG_P_MVOLXL>, //     0x0770		// Current Master Volume Left
		RegWrite_CoreExt<0, REG_P_MVOLXR>, //     0x0772		// Current Master Volume Right

		RegWrite_CoreExt<0, R_IIR_VOL>,   //     0x0774		//IIR alpha (% used)
		RegWrite_CoreExt<0, R_COMB1_VOL>, //     0x0776
		RegWrite_CoreExt<0, R_COMB2_VOL>, //     0x0778
		RegWrite_CoreExt<0, R_COMB3_VOL>, //     0x077A
		RegWrite_CoreExt<0, R_COMB4_VOL>, //     0x077C
		RegWrite_CoreExt<0, R_WALL_VOL>,  //     0x077E
		RegWrite_CoreExt<0, R_APF1_VOL>,  //     0x0780		//feedback alpha (% used)
		RegWrite_CoreExt<0, R_APF2_VOL>,  //     0x0782		//feedback
		RegWrite_CoreExt<0, R_IN_COEF_L>, //     0x0784
		RegWrite_CoreExt<0, R_IN_COEF_R>, //     0x0786

		// ------ -------

		RegWrite_CoreExt<1, REG_P_MVOLL>,  //     0x0788		// Master Volume Left
		RegWrite_CoreExt<1, REG_P_MVOLR>,  //     0x078A		// Master Volume Right
		RegWrite_CoreExt<1, REG_P_EVOLL>,  //     0x0764		// Effect Volume Left
		RegWrite_CoreExt<1, REG_P_EVOLR>,  //     0x0766		// Effect Volume Right
		RegWrite_CoreExt<1, REG_P_AVOLL>,  //     0x0768		// Core External Input Volume Left  (Only Core 1)
		RegWrite_CoreExt<1, REG_P_AVOLR>,  //     0x076A		// Core External Input Volume Right (Only Core 1)
		RegWrite_CoreExt<1, REG_P_BVOLL>,  //     0x076C		// Sound Data Volume Left
		RegWrite_CoreExt<1, REG_P_BVOLR>,  //     0x076E		// Sound Data Volume Right
		RegWrite_CoreExt<1, REG_P_MVOLXL>, //     0x0770		// Current Master Volume Left
		RegWrite_CoreExt<1, REG_P_MVOLXR>, //     0x0772		// Current Master Volume Right

		RegWrite_CoreExt<1, R_IIR_VOL>,   //     0x0774		//IIR alpha (% used)
		RegWrite_CoreExt<1, R_COMB1_VOL>, //     0x0776
		RegWrite_CoreExt<1, R_COMB2_VOL>, //     0x0778
		RegWrite_CoreExt<1, R_COMB3_VOL>, //     0x077A
		RegWrite_CoreExt<1, R_COMB4_VOL>, //     0x077C
		RegWrite_CoreExt<1, R_WALL_VOL>,  //     0x077E
		RegWrite_CoreExt<1, R_APF1_VOL>,  //     0x0780		//feedback alpha (% used)
		RegWrite_CoreExt<1, R_APF2_VOL>,  //     0x0782		//feedback
		RegWrite_CoreExt<1, R_IN_COEF_L>, //     0x0784
		RegWrite_CoreExt<1, R_IN_COEF_R>, //     0x0786

		REGRAW(0x7B0), REGRAW(0x7B2), REGRAW(0x7B4), REGRAW(0x7B6),
		REGRAW(0x7B8), REGRAW(0x7BA), REGRAW(0x7BC), REGRAW(0x7BE),

		//  SPDIF interface

		RegWrite_SPDIF<SPDIF_OUT>,     //    0x07C0		// SPDIF Out: OFF/'PCM'/Bitstream/Bypass
		RegWrite_SPDIF<SPDIF_IRQINFO>, //    0x07C2
		REGRAW(0x7C4),
		RegWrite_SPDIF<SPDIF_MODE>,  //    0x07C6
		RegWrite_SPDIF<SPDIF_MEDIA>, //    0x07C8		// SPDIF Media: 'CD'/DVD
		REGRAW(0x7CA),
		RegWrite_SPDIF<SPDIF_PROTECT>, //	 0x07CC		// SPDIF Copy Protection

		REGRAW(0x7CE),
		REGRAW(0x7D0), REGRAW(0x7D2), REGRAW(0x7D4), REGRAW(0x7D6),
		REGRAW(0x7D8), REGRAW(0x7DA), REGRAW(0x7DC), REGRAW(0x7DE),
		REGRAW(0x7E0), REGRAW(0x7E2), REGRAW(0x7E4), REGRAW(0x7E6),
		REGRAW(0x7E8), REGRAW(0x7EA), REGRAW(0x7EC), REGRAW(0x7EE),
		REGRAW(0x7F0), REGRAW(0x7F2), REGRAW(0x7F4), REGRAW(0x7F6),
		REGRAW(0x7F8), REGRAW(0x7FA), REGRAW(0x7FC), REGRAW(0x7FE),

		nullptr // should be at 0x400!  (we assert check it on startup)
};

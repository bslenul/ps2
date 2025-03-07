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

#include "Global.h"

// FIXME Not yet used so let's comment it out.
/*static const u8 sLogTable[256] = {
    0x00, 0x3C, 0x60, 0x78, 0x8C, 0x9C, 0xA8, 0xB4, 0xBE, 0xC8, 0xD0, 0xD8, 0xDE, 0xE4, 0xEA, 0xF0,
    0xF6, 0xFA, 0xFE, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x16, 0x1A, 0x1E, 0x20, 0x24, 0x26, 0x2A, 0x2C,
    0x2E, 0x32, 0x34, 0x36, 0x38, 0x3A, 0x3E, 0x40, 0x42, 0x44, 0x46, 0x48, 0x4A, 0x4C, 0x4E, 0x50,
    0x50, 0x52, 0x54, 0x56, 0x58, 0x5A, 0x5A, 0x5C, 0x5E, 0x60, 0x60, 0x62, 0x64, 0x66, 0x66, 0x68,
    0x6A, 0x6A, 0x6C, 0x6E, 0x6E, 0x70, 0x70, 0x72, 0x74, 0x74, 0x76, 0x76, 0x78, 0x7A, 0x7A, 0x7C,
    0x7C, 0x7E, 0x7E, 0x80, 0x80, 0x82, 0x82, 0x84, 0x84, 0x86, 0x86, 0x88, 0x88, 0x8A, 0x8A, 0x8C,
    0x8C, 0x8C, 0x8E, 0x8E, 0x90, 0x90, 0x92, 0x92, 0x92, 0x94, 0x94, 0x96, 0x96, 0x96, 0x98, 0x98,
    0x9A, 0x9A, 0x9A, 0x9C, 0x9C, 0x9C, 0x9E, 0x9E, 0xA0, 0xA0, 0xA0, 0xA2, 0xA2, 0xA2, 0xA4, 0xA4,
    0xA4, 0xA6, 0xA6, 0xA6, 0xA8, 0xA8, 0xA8, 0xAA, 0xAA, 0xAA, 0xAC, 0xAC, 0xAC, 0xAC, 0xAE, 0xAE,
    0xAE, 0xB0, 0xB0, 0xB0, 0xB2, 0xB2, 0xB2, 0xB2, 0xB4, 0xB4, 0xB4, 0xB6, 0xB6, 0xB6, 0xB6, 0xB8,
    0xB8, 0xB8, 0xB8, 0xBA, 0xBA, 0xBA, 0xBC, 0xBC, 0xBC, 0xBC, 0xBE, 0xBE, 0xBE, 0xBE, 0xC0, 0xC0,
    0xC0, 0xC0, 0xC2, 0xC2, 0xC2, 0xC2, 0xC2, 0xC4, 0xC4, 0xC4, 0xC4, 0xC6, 0xC6, 0xC6, 0xC6, 0xC8,
    0xC8, 0xC8, 0xC8, 0xC8, 0xCA, 0xCA, 0xCA, 0xCA, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCE, 0xCE, 0xCE,
    0xCE, 0xCE, 0xD0, 0xD0, 0xD0, 0xD0, 0xD0, 0xD2, 0xD2, 0xD2, 0xD2, 0xD2, 0xD4, 0xD4, 0xD4, 0xD4,
    0xD4, 0xD6, 0xD6, 0xD6, 0xD6, 0xD6, 0xD8, 0xD8, 0xD8, 0xD8, 0xD8, 0xD8, 0xDA, 0xDA, 0xDA, 0xDA,
    0xDA, 0xDC, 0xDC, 0xDC, 0xDC, 0xDC, 0xDC, 0xDE, 0xDE, 0xDE, 0xDE, 0xDE, 0xDE, 0xE0, 0xE0, 0xE0,
};*/

static float Gfl = 0, Gfr = 0;
static float LMax = 0, RMax = 0;

static float AccL = 0;
static float AccR = 0;

constexpr float Scale = 4294967296.0f; // tweak this value to change the overall output volume

constexpr float GainL = 0.80f * Scale;
constexpr float GainR = 0.80f * Scale;

constexpr float GainC = 0.75f * Scale;

constexpr float GainSL = 0.90f * Scale;
constexpr float GainSR = 0.90f * Scale;

constexpr float GainLFE = 0.90f * Scale;

constexpr float AddCLR = 0.20f * Scale; // Stereo expansion

extern void ResetDplIIDecoder()
{
	Gfl = 0;
	Gfr = 0;
	LMax = 0;
	RMax = 0;
	AccL = 0;
	AccR = 0;
}

void ProcessDplIISample32(const StereoOut16& src, Stereo51Out32DplII* s)
{
	const float IL = src.Left / static_cast<float>(1 << 16);
	const float IR = src.Right / static_cast<float>(1 << 16);

	// Calculate center channel and LFE
	const float C = (IL + IR) * 0.5f;
	const float SUB = C; // no need to lowpass, the speaker amplifier should take care of it

	float L = IL - C; // Effective L/R data
	float R = IR - C;

	// Peak L/R
	const float PL = std::abs(L);
	const float PR = std::abs(R);

	AccL += (PL - AccL) * 0.1f;
	AccR += (PR - AccR) * 0.1f;

	// Calculate power balance
	const float Balance = (AccR - AccL); // -1 .. 1

	// If the power levels are different, then the audio is meant for the front speakers
	const float Frontness = std::abs(Balance);
	const float Rearness = 1 - Frontness; // And the other way around

	// Equalize the power levels for L/R
	const float B = std::min(0.9f, std::max(-0.9f, Balance));

	const float VL = L / (1 - B); // if B>0, it means R>L, so increase L, else decrease L
	const float VR = R / (1 + B); // vice-versa

	// 1.73+1.22 = 2.94; 2.94 = 0.34 = 0.9996; Close enough.
	// The range for VL/VR is approximately 0..1,
	// But in the cases where VL/VR are > 0.5, Rearness is 0 so it should never overflow.
	constexpr float RearScale = 0.34f * 2;

	const float SL = (VR * 1.73f - VL * 1.22f) * RearScale * Rearness;
	const float SR = (VR * 1.22f - VL * 1.73f) * RearScale * Rearness;
	// Possible experiment: Play with stereo expension levels on rear

	// Adjust the volume of the front speakers based on what we calculated above
	L *= Frontness;
	R *= Frontness;

	const s32 CX = static_cast<s32>(C * AddCLR);

	s->Left = static_cast<s32>(L * GainL) + CX;
	s->Right = static_cast<s32>(R * GainR) + CX;
	s->Center = static_cast<s32>(C * GainC);
	s->LFE = static_cast<s32>(SUB * GainLFE);
	s->LeftBack = static_cast<s32>(SL * GainSL);
	s->RightBack = static_cast<s32>(SR * GainSR);
}

void ProcessDplIISample16(const StereoOut16& src, Stereo51Out16DplII* s)
{
	Stereo51Out32DplII ss;
	ProcessDplIISample32(src, &ss);

	s->Left = ss.Left >> 16;
	s->Right = ss.Right >> 16;
	s->Center = ss.Center >> 16;
	s->LFE = ss.LFE >> 16;
	s->LeftBack = ss.LeftBack >> 16;
	s->RightBack = ss.RightBack >> 16;
}

void ProcessDplSample32(const StereoOut16& src, Stereo51Out32Dpl* s)
{
	const float ValL = src.Left / static_cast<float>(1 << 16);
	const float ValR = src.Right / static_cast<float>(1 << 16);

	const float C = (ValL + ValR) * 0.5f; //+15.8
	const float S = (ValL - ValR) * 0.5f;

	const float L = ValL - C; //+15.8
	const float R = ValR - C;

	const float SUB = C;

	const s32 CX = static_cast<s32>(C * AddCLR); // +15.16

	s->Left = static_cast<s32>(L * GainL) + CX; // +15.16 = +31, can grow to +32 if (GainL + AddCLR)>255
	s->Right = static_cast<s32>(R * GainR) + CX;
	s->Center = static_cast<s32>(C * GainC); // +15.16 = +31
	s->LFE = static_cast<s32>(SUB * GainLFE);
	s->LeftBack = static_cast<s32>(S * GainSL);
	s->RightBack = static_cast<s32>(S * GainSR);
}

void ProcessDplSample16(const StereoOut16& src, Stereo51Out16Dpl* s)
{
	Stereo51Out32Dpl ss;
	ProcessDplSample32(src, &ss);

	s->Left = ss.Left >> 16;
	s->Right = ss.Right >> 16;
	s->Center = ss.Center >> 16;
	s->LFE = ss.LFE >> 16;
	s->LeftBack = ss.LeftBack >> 16;
	s->RightBack = ss.RightBack >> 16;
}

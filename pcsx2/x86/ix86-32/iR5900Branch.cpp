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

#include "Common.h"
#include "R5900OpcodeTables.h"
#include "x86/iR5900.h"

using namespace x86Emitter;

namespace R5900::Dynarec::OpcodeImpl
{
/*********************************************************
* Register branch logic                                  *
* Format:  OP rs, rt, offset                             *
*********************************************************/
#ifndef BRANCH_RECOMPILE

namespace Interp = R5900::Interpreter::OpcodeImpl;

REC_SYS(BEQ);
REC_SYS(BEQL);
REC_SYS(BNE);
REC_SYS(BNEL);
REC_SYS(BLTZ);
REC_SYS(BGTZ);
REC_SYS(BLEZ);
REC_SYS(BGEZ);
REC_SYS(BGTZL);
REC_SYS(BLTZL);
REC_SYS_DEL(BLTZAL, 31);
REC_SYS_DEL(BLTZALL, 31);
REC_SYS(BLEZL);
REC_SYS(BGEZL);
REC_SYS_DEL(BGEZAL, 31);
REC_SYS_DEL(BGEZALL, 31);

#else

static u32 *recSetBranchEQ(int bne, int process)
{
	// TODO(Stenzek): This is suboptimal if the registers are in XMMs.
	// If the constant register is already in a host register, we don't need the immediate...

	if (process & PROCESS_CONSTS)
	{
		_eeFlushAllDirty();

		_deleteGPRtoXMMreg(_Rt_, DELETE_REG_FLUSH_AND_FREE);
		const int regt = _checkX86reg(X86TYPE_GPR, _Rt_, MODE_READ);
		if (regt >= 0)
			xImm64Op(xCMP, xRegister64(regt), rax, g_cpuConstRegs[_Rs_].UD[0]);
		else
			xImm64Op(xCMP, ptr64[&cpuRegs.GPR.r[_Rt_].UD[0]], rax, g_cpuConstRegs[_Rs_].UD[0]);
	}
	else if (process & PROCESS_CONSTT)
	{
		_eeFlushAllDirty();

		_deleteGPRtoXMMreg(_Rs_, DELETE_REG_FLUSH_AND_FREE);
		const int regs = _checkX86reg(X86TYPE_GPR, _Rs_, MODE_READ);
		if (regs >= 0)
			xImm64Op(xCMP, xRegister64(regs), rax, g_cpuConstRegs[_Rt_].UD[0]);
		else
			xImm64Op(xCMP, ptr64[&cpuRegs.GPR.r[_Rs_].UD[0]], rax, g_cpuConstRegs[_Rt_].UD[0]);
	}
	else
	{
		// force S into register, since we need to load it, may as well cache.
		_deleteGPRtoXMMreg(_Rt_, DELETE_REG_FLUSH_AND_FREE);
		const int regs = _allocX86reg(X86TYPE_GPR, _Rs_, MODE_READ);
		const int regt = _checkX86reg(X86TYPE_GPR, _Rt_, MODE_READ);
		_eeFlushAllDirty();

		if (regt >= 0)
			xCMP(xRegister64(regs), xRegister64(regt));
		else
			xCMP(xRegister64(regs), ptr64[&cpuRegs.GPR.r[_Rt_]]);
	}

	if (bne)
		return JE32(0);
	return JNE32(0);
}

static u32 *recSetBranchL(int ltz)
{
	const int regs = _checkX86reg(X86TYPE_GPR, _Rs_, MODE_READ);
	const int regsxmm = _checkXMMreg(XMMTYPE_GPRREG, _Rs_, MODE_READ);
	_eeFlushAllDirty();

	if (regsxmm >= 0)
	{
		xMOVMSKPS(eax, xRegisterSSE(regsxmm));
		xTEST(al, 2);

		if (ltz)
			return JZ32(0);
		return JNZ32(0);
	}

	if (regs >= 0)
		xCMP(xRegister64(regs), 0);
	else
		xCMP(ptr64[&cpuRegs.GPR.r[_Rs_].UD[0]], 0);

	if (ltz)
		return JGE32(0);
	return JL32(0);
}

//// BEQ
static void recBEQ_const(void)
{
	u32 branchTo;

	if (g_cpuConstRegs[_Rs_].SD[0] == g_cpuConstRegs[_Rt_].SD[0])
		branchTo = ((s32)_Imm_ * 4) + pc;
	else
		branchTo = pc + 4;

	recompileNextInstruction(true, false);
	SetBranchImm(branchTo);
}

static void recBEQ_process(int process)
{
	u32 branchTo = ((s32)_Imm_ * 4) + pc;

	if (_Rs_ == _Rt_)
	{
		recompileNextInstruction(true, false);
		SetBranchImm(branchTo);
	}
	else
	{
		const bool swap = TrySwapDelaySlot(_Rs_, _Rt_, 0, true);
		u32 *j32Ptr = recSetBranchEQ(0, process);

		if (!swap)
		{
			SaveBranchState();
			recompileNextInstruction(true, false);
		}

		SetBranchImm(branchTo);

		x86SetJ32(j32Ptr);

		if (!swap)
		{
			// recopy the next inst
			pc -= 4;
			LoadBranchState();
			recompileNextInstruction(true, false);
		}

		SetBranchImm(pc);
	}
}

void recBEQ(void)
{
	// prefer using the host register over an immediate, it'll be smaller code.
	if (GPR_IS_CONST2(_Rs_, _Rt_))
		recBEQ_const();
	else if (GPR_IS_CONST1(_Rs_) && _checkX86reg(X86TYPE_GPR, _Rs_, MODE_READ) < 0)
		recBEQ_process(PROCESS_CONSTS);
	else if (GPR_IS_CONST1(_Rt_) && _checkX86reg(X86TYPE_GPR, _Rt_, MODE_READ) < 0)
		recBEQ_process(PROCESS_CONSTT);
	else
		recBEQ_process(0);
}

//// BNE
static void recBNE_const(void)
{
	u32 branchTo;

	if (g_cpuConstRegs[_Rs_].SD[0] != g_cpuConstRegs[_Rt_].SD[0])
		branchTo = ((s32)_Imm_ * 4) + pc;
	else
		branchTo = pc + 4;

	recompileNextInstruction(true, false);
	SetBranchImm(branchTo);
}

static void recBNE_process(int process)
{
	u32 branchTo = ((s32)_Imm_ * 4) + pc;

	if (_Rs_ == _Rt_)
	{
		recompileNextInstruction(true, false);
		SetBranchImm(pc);
		return;
	}

	const bool swap = TrySwapDelaySlot(_Rs_, _Rt_, 0, true);

	u32 *j32Ptr = recSetBranchEQ(1, process);

	if (!swap)
	{
		SaveBranchState();
		recompileNextInstruction(true, false);
	}

	SetBranchImm(branchTo);

	x86SetJ32(j32Ptr);

	if (!swap)
	{
		// recopy the next inst
		pc -= 4;
		LoadBranchState();
		recompileNextInstruction(true, false);
	}

	SetBranchImm(pc);
}

void recBNE(void)
{
	if (GPR_IS_CONST2(_Rs_, _Rt_))
		recBNE_const();
	else if (GPR_IS_CONST1(_Rs_) && _checkX86reg(X86TYPE_GPR, _Rs_, MODE_READ) < 0)
		recBNE_process(PROCESS_CONSTS);
	else if (GPR_IS_CONST1(_Rt_) && _checkX86reg(X86TYPE_GPR, _Rt_, MODE_READ) < 0)
		recBNE_process(PROCESS_CONSTT);
	else
		recBNE_process(0);
}

//// BEQL
static void recBEQL_const(void)
{
	if (g_cpuConstRegs[_Rs_].SD[0] == g_cpuConstRegs[_Rt_].SD[0])
	{
		u32 branchTo = ((s32)_Imm_ * 4) + pc;
		recompileNextInstruction(true, false);
		SetBranchImm(branchTo);
	}
	else
	{
		SetBranchImm(pc + 4);
	}
}

static void recBEQL_process(int process)
{
	u32 branchTo = ((s32)_Imm_ * 4) + pc;
	u32 *j32Ptr = recSetBranchEQ(0, process);

	SaveBranchState();
	recompileNextInstruction(true, false);
	SetBranchImm(branchTo);

	x86SetJ32(j32Ptr);

	LoadBranchState();
	SetBranchImm(pc);
}

void recBEQL(void)
{
	if (GPR_IS_CONST2(_Rs_, _Rt_))
		recBEQL_const();
	else if (GPR_IS_CONST1(_Rs_) && _checkX86reg(X86TYPE_GPR, _Rs_, MODE_READ) < 0)
		recBEQL_process(PROCESS_CONSTS);
	else if (GPR_IS_CONST1(_Rt_) && _checkX86reg(X86TYPE_GPR, _Rt_, MODE_READ) < 0)
		recBEQL_process(PROCESS_CONSTT);
	else
		recBEQL_process(0);
}

//// BNEL
static void recBNEL_const(void)
{
	if (g_cpuConstRegs[_Rs_].SD[0] != g_cpuConstRegs[_Rt_].SD[0])
	{
		u32 branchTo = ((s32)_Imm_ * 4) + pc;
		recompileNextInstruction(true, false);
		SetBranchImm(branchTo);
	}
	else
	{
		SetBranchImm(pc + 4);
	}
}

static void recBNEL_process(int process)
{
	u32 branchTo = ((s32)_Imm_ * 4) + pc;

	u32 *j32Ptr = recSetBranchEQ(0, process);

	SaveBranchState();
	SetBranchImm(pc + 4);

	x86SetJ32(j32Ptr);

	// recopy the next inst
	LoadBranchState();
	recompileNextInstruction(true, false);
	SetBranchImm(branchTo);
}

void recBNEL()
{
	if (GPR_IS_CONST2(_Rs_, _Rt_))
		recBNEL_const();
	else if (GPR_IS_CONST1(_Rs_) && _checkX86reg(X86TYPE_GPR, _Rs_, MODE_READ) < 0)
		recBNEL_process(PROCESS_CONSTS);
	else if (GPR_IS_CONST1(_Rt_) && _checkX86reg(X86TYPE_GPR, _Rt_, MODE_READ) < 0)
		recBNEL_process(PROCESS_CONSTT);
	else
		recBNEL_process(0);
}

////////////////////////////////////////////////////
void recBLTZAL(void)
{
	u32 branchTo = ((s32)_Imm_ * 4) + pc;

	_eeOnWriteReg(31, 0);
	_eeFlushAllDirty();

	_deleteEEreg(31, 0);
	xMOV64(rax, pc + 4);
	xMOV(ptr64[&cpuRegs.GPR.n.ra.UD[0]], rax);

	if (GPR_IS_CONST1(_Rs_))
	{
		if (!(g_cpuConstRegs[_Rs_].SD[0] < 0))
			branchTo = pc + 4;

		recompileNextInstruction(true, false);
		SetBranchImm(branchTo);
		return;
	}

	const bool swap = TrySwapDelaySlot(_Rs_, 0, 0, true);

	u32 *j32Ptr = recSetBranchL(1);

	if (!swap)
	{
		SaveBranchState();
		recompileNextInstruction(true, false);
	}

	SetBranchImm(branchTo);

	x86SetJ32(j32Ptr);

	if (!swap)
	{
		// recopy the next inst
		pc -= 4;
		LoadBranchState();
		recompileNextInstruction(true, false);
	}

	SetBranchImm(pc);
}

////////////////////////////////////////////////////
void recBGEZAL(void)
{
	u32 branchTo = ((s32)_Imm_ * 4) + pc;

	_eeOnWriteReg(31, 0);
	_eeFlushAllDirty();

	_deleteEEreg(31, 0);
	xMOV64(rax, pc + 4);
	xMOV(ptr64[&cpuRegs.GPR.n.ra.UD[0]], rax);

	if (GPR_IS_CONST1(_Rs_))
	{
		if (!(g_cpuConstRegs[_Rs_].SD[0] >= 0))
			branchTo = pc + 4;

		recompileNextInstruction(true, false);
		SetBranchImm(branchTo);
		return;
	}

	const bool swap = TrySwapDelaySlot(_Rs_, 0, 0, true);

	u32 *j32Ptr = recSetBranchL(0);

	if (!swap)
	{
		SaveBranchState();
		recompileNextInstruction(true, false);
	}

	SetBranchImm(branchTo);

	x86SetJ32(j32Ptr);

	if (!swap)
	{
		// recopy the next inst
		pc -= 4;
		LoadBranchState();
		recompileNextInstruction(true, false);
	}

	SetBranchImm(pc);
}

////////////////////////////////////////////////////
void recBLTZALL(void)
{
	u32 branchTo = ((s32)_Imm_ * 4) + pc;

	_eeOnWriteReg(31, 0);
	_eeFlushAllDirty();

	_deleteEEreg(31, 0);
	xMOV64(rax, pc + 4);
	xMOV(ptr64[&cpuRegs.GPR.n.ra.UD[0]], rax);

	if (GPR_IS_CONST1(_Rs_))
	{
		if (!(g_cpuConstRegs[_Rs_].SD[0] < 0))
			SetBranchImm(pc + 4);
		else
		{
			recompileNextInstruction(true, false);
			SetBranchImm(branchTo);
		}
		return;
	}

	u32 *j32Ptr = recSetBranchL(1);

	SaveBranchState();
	recompileNextInstruction(true, false);
	SetBranchImm(branchTo);

	x86SetJ32(j32Ptr);

	LoadBranchState();
	SetBranchImm(pc);
}

////////////////////////////////////////////////////
void recBGEZALL(void)
{
	u32 branchTo = ((s32)_Imm_ * 4) + pc;

	_eeOnWriteReg(31, 0);
	_eeFlushAllDirty();

	_deleteEEreg(31, 0);
	xMOV64(rax, pc + 4);
	xMOV(ptr64[&cpuRegs.GPR.n.ra.UD[0]], rax);

	if (GPR_IS_CONST1(_Rs_))
	{
		if (!(g_cpuConstRegs[_Rs_].SD[0] >= 0))
			SetBranchImm(pc + 4);
		else
		{
			recompileNextInstruction(true, false);
			SetBranchImm(branchTo);
		}
		return;
	}

	u32 *j32Ptr = recSetBranchL(0);

	SaveBranchState();
	recompileNextInstruction(true, false);
	SetBranchImm(branchTo);

	x86SetJ32(j32Ptr);

	LoadBranchState();
	SetBranchImm(pc);
}


//// BLEZ
void recBLEZ(void)
{
	u32 branchTo = ((s32)_Imm_ * 4) + pc;

	if (GPR_IS_CONST1(_Rs_))
	{
		if (!(g_cpuConstRegs[_Rs_].SD[0] <= 0))
			branchTo = pc + 4;

		recompileNextInstruction(true, false);
		SetBranchImm(branchTo);
		return;
	}

	const bool swap = TrySwapDelaySlot(_Rs_, 0, 0, true);
	const int regs = _checkX86reg(X86TYPE_GPR, _Rs_, MODE_READ);
	_eeFlushAllDirty();

	if (regs >= 0)
		xCMP(xRegister64(regs), 0);
	else
		xCMP(ptr64[&cpuRegs.GPR.r[_Rs_].UD[0]], 0);

	u32 *j32Ptr = JG32(0);

	if (!swap)
	{
		SaveBranchState();
		recompileNextInstruction(true, false);
	}

	SetBranchImm(branchTo);

	x86SetJ32(j32Ptr);

	if (!swap)
	{
		// recopy the next inst
		pc -= 4;
		LoadBranchState();
		recompileNextInstruction(true, false);
	}

	SetBranchImm(pc);
}

//// BGTZ
void recBGTZ(void)
{
	u32 branchTo = ((s32)_Imm_ * 4) + pc;

	if (GPR_IS_CONST1(_Rs_))
	{
		if (!(g_cpuConstRegs[_Rs_].SD[0] > 0))
			branchTo = pc + 4;

		recompileNextInstruction(true, false);
		SetBranchImm(branchTo);
		return;
	}

	const bool swap = TrySwapDelaySlot(_Rs_, 0, 0, true);
	const int regs = _checkX86reg(X86TYPE_GPR, _Rs_, MODE_READ);
	_eeFlushAllDirty();

	if (regs >= 0)
		xCMP(xRegister64(regs), 0);
	else
		xCMP(ptr64[&cpuRegs.GPR.r[_Rs_].UD[0]], 0);

	u32 *j32Ptr = JLE32(0);

	if (!swap)
	{
		SaveBranchState();
		recompileNextInstruction(true, false);
	}

	SetBranchImm(branchTo);

	x86SetJ32(j32Ptr);

	if (!swap)
	{
		// recopy the next inst
		pc -= 4;
		LoadBranchState();
		recompileNextInstruction(true, false);
	}

	SetBranchImm(pc);
}

////////////////////////////////////////////////////
void recBLTZ(void)
{
	u32 branchTo = ((s32)_Imm_ * 4) + pc;

	if (GPR_IS_CONST1(_Rs_))
	{
		if (!(g_cpuConstRegs[_Rs_].SD[0] < 0))
			branchTo = pc + 4;

		recompileNextInstruction(true, false);
		SetBranchImm(branchTo);
		return;
	}

	const bool swap = TrySwapDelaySlot(_Rs_, 0, 0, true);
	_eeFlushAllDirty();
	u32 *j32Ptr = recSetBranchL(1);

	if (!swap)
	{
		SaveBranchState();
		recompileNextInstruction(true, false);
	}

	SetBranchImm(branchTo);

	x86SetJ32(j32Ptr);

	if (!swap)
	{
		// recopy the next inst
		pc -= 4;
		LoadBranchState();
		recompileNextInstruction(true, false);
	}

	SetBranchImm(pc);
}

////////////////////////////////////////////////////
void recBGEZ(void)
{
	u32 branchTo = ((s32)_Imm_ * 4) + pc;

	if (GPR_IS_CONST1(_Rs_))
	{
		if (!(g_cpuConstRegs[_Rs_].SD[0] >= 0))
			branchTo = pc + 4;

		recompileNextInstruction(true, false);
		SetBranchImm(branchTo);
		return;
	}

	const bool swap = TrySwapDelaySlot(_Rs_, 0, 0, true);
	_eeFlushAllDirty();

	u32 *j32Ptr = recSetBranchL(0);

	if (!swap)
	{
		SaveBranchState();
		recompileNextInstruction(true, false);
	}

	SetBranchImm(branchTo);

	x86SetJ32(j32Ptr);

	if (!swap)
	{
		// recopy the next inst
		pc -= 4;
		LoadBranchState();
		recompileNextInstruction(true, false);
	}

	SetBranchImm(pc);
}

////////////////////////////////////////////////////
void recBLTZL(void)
{
	const u32 branchTo = ((s32)_Imm_ * 4) + pc;

	if (GPR_IS_CONST1(_Rs_))
	{
		if (!(g_cpuConstRegs[_Rs_].SD[0] < 0))
			SetBranchImm(pc + 4);
		else
		{
			recompileNextInstruction(true, false);
			SetBranchImm(branchTo);
		}
		return;
	}

	_eeFlushAllDirty();
	u32 *j32Ptr = recSetBranchL(1);

	SaveBranchState();
	recompileNextInstruction(true, false);
	SetBranchImm(branchTo);

	x86SetJ32(j32Ptr);

	LoadBranchState();
	SetBranchImm(pc);
}


////////////////////////////////////////////////////
void recBGEZL(void)
{
	const u32 branchTo = ((s32)_Imm_ * 4) + pc;

	if (GPR_IS_CONST1(_Rs_))
	{
		if (!(g_cpuConstRegs[_Rs_].SD[0] >= 0))
			SetBranchImm(pc + 4);
		else
		{
			recompileNextInstruction(true, false);
			SetBranchImm(branchTo);
		}
		return;
	}

	_eeFlushAllDirty();
	u32 *j32Ptr = recSetBranchL(0);

	SaveBranchState();
	recompileNextInstruction(true, false);
	SetBranchImm(branchTo);

	x86SetJ32(j32Ptr);

	LoadBranchState();
	SetBranchImm(pc);
}



/*********************************************************
* Register branch logic  Likely                          *
* Format:  OP rs, offset                                 *
*********************************************************/

////////////////////////////////////////////////////
void recBLEZL(void)
{
	const u32 branchTo = ((s32)_Imm_ * 4) + pc;

	if (GPR_IS_CONST1(_Rs_))
	{
		if (!(g_cpuConstRegs[_Rs_].SD[0] <= 0))
			SetBranchImm(pc + 4);
		else
		{
			recompileNextInstruction(true, false);
			SetBranchImm(branchTo);
		}
		return;
	}

	const int regs = _checkX86reg(X86TYPE_GPR, _Rs_, MODE_READ);
	_eeFlushAllDirty();

	if (regs >= 0)
		xCMP(xRegister64(regs), 0);
	else
		xCMP(ptr64[&cpuRegs.GPR.r[_Rs_].UD[0]], 0);

	u32 *j32Ptr = JG32(0);

	SaveBranchState();
	recompileNextInstruction(true, false);
	SetBranchImm(branchTo);

	x86SetJ32(j32Ptr);

	LoadBranchState();
	SetBranchImm(pc);
}

////////////////////////////////////////////////////
void recBGTZL(void)
{
	const u32 branchTo = ((s32)_Imm_ * 4) + pc;

	if (GPR_IS_CONST1(_Rs_))
	{
		if (!(g_cpuConstRegs[_Rs_].SD[0] > 0))
			SetBranchImm(pc + 4);
		else
		{
			_clearNeededXMMregs();
			recompileNextInstruction(true, false);
			SetBranchImm(branchTo);
		}
		return;
	}

	const int regs = _checkX86reg(X86TYPE_GPR, _Rs_, MODE_READ);
	_eeFlushAllDirty();

	if (regs >= 0)
		xCMP(xRegister64(regs), 0);
	else
		xCMP(ptr64[&cpuRegs.GPR.r[_Rs_].UD[0]], 0);

	u32 *j32Ptr = JLE32(0);

	SaveBranchState();
	recompileNextInstruction(true, false);
	SetBranchImm(branchTo);

	x86SetJ32(j32Ptr);

	LoadBranchState();
	SetBranchImm(pc);
}

#endif

} // namespace R5900::Dynarec::OpcodeImpl

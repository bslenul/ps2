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

enum microOpcode
{
	// Upper Instructions
	opABS, opCLIP, opOPMULA, opOPMSUB, opNOP,
	opADD,   opADDi,   opADDq,   opADDx,   opADDy,   opADDz,   opADDw,
	opADDA,  opADDAi,  opADDAq,  opADDAx,  opADDAy,  opADDAz,  opADDAw,
	opSUB,   opSUBi,   opSUBq,   opSUBx,   opSUBy,   opSUBz,   opSUBw,
	opSUBA,  opSUBAi,  opSUBAq,  opSUBAx,  opSUBAy,  opSUBAz,  opSUBAw,
	opMUL,   opMULi,   opMULq,   opMULx,   opMULy,   opMULz,   opMULw,
	opMULA,  opMULAi,  opMULAq,  opMULAx,  opMULAy,  opMULAz,  opMULAw,
	opMADD,  opMADDi,  opMADDq,  opMADDx,  opMADDy,  opMADDz,  opMADDw,
	opMADDA, opMADDAi, opMADDAq, opMADDAx, opMADDAy, opMADDAz, opMADDAw,
	opMSUB,  opMSUBi,  opMSUBq,  opMSUBx,  opMSUBy,  opMSUBz,  opMSUBw,
	opMSUBA, opMSUBAi, opMSUBAq, opMSUBAx, opMSUBAy, opMSUBAz, opMSUBAw,
	opMAX,   opMAXi,             opMAXx,   opMAXy,    opMAXz,  opMAXw,
	opMINI,  opMINIi,            opMINIx,  opMINIy,   opMINIz, opMINIw,
	opFTOI0, opFTOI4, opFTOI12, opFTOI15,
	opITOF0, opITOF4, opITOF12, opITOF15,
	// Lower Instructions
	opDIV, opSQRT, opRSQRT,
	opIADD, opIADDI, opIADDIU,
	opIAND, opIOR,
	opISUB, opISUBIU,
	opMOVE, opMFIR, opMTIR, opMR32, opMFP,
	opLQ, opLQD, opLQI,
	opSQ, opSQD, opSQI,
	opILW, opISW, opILWR, opISWR,
	opRINIT, opRGET, opRNEXT, opRXOR,
	opWAITQ, opWAITP,
	opFSAND, opFSEQ, opFSOR, opFSSET,
	opFMAND, opFMEQ, opFMOR,
	opFCAND, opFCEQ, opFCOR, opFCSET, opFCGET,
	opIBEQ, opIBGEZ, opIBGTZ, opIBLTZ, opIBLEZ, opIBNE,
	opB, opBAL, opJR, opJALR,
	opESADD, opERSADD, opELENG, opERLENG,
	opEATANxy, opEATANxz, opESUM, opERCPR,
	opESQRT, opERSQRT, opESIN, opEATAN,
	opEEXP, opXITOP, opXTOP, opXGKICK,
	opLastOpcode
};

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

#include "Pcsx2Defs.h"

#include <string>

enum ConsoleColors
{
	Color_Current = -1,

	Color_Default = 0,

	Color_Black,
	Color_Green,
	Color_Red,
	Color_Blue,
	Color_Magenta,
	Color_Orange,
	Color_Gray,

	Color_Cyan, // faint visibility, intended for logging PS2/IOP output
	Color_Yellow, // faint visibility, intended for logging PS2/IOP output
	Color_White, // faint visibility, intended for logging PS2/IOP output

	// Strong text *may* result in mis-aligned text in the console, depending on the
	// font and the platform, so use these with caution.
	Color_StrongBlack,
	Color_StrongRed, // intended for errors
	Color_StrongGreen, // intended for infrequent state information
	Color_StrongBlue, // intended for block headings
	Color_StrongMagenta,
	Color_StrongOrange, // intended for warnings
	Color_StrongGray,

	Color_StrongCyan,
	Color_StrongYellow,
	Color_StrongWhite,

	ConsoleColors_Count
};

static const ConsoleColors DefaultConsoleColor = Color_Default;


// ----------------------------------------------------------------------------------------
//  IConsoleWriter -- For printing messages to the console.
// ----------------------------------------------------------------------------------------
// General ConsoleWrite Threading Guideline:
//   PCSX2 is a threaded environment and multiple threads can write to the console asynchronously.
//   Individual calls to ConsoleWriter APIs will be written in atomic fashion, however "partial"
//   logs may end up interrupted by logs on other threads.  This is usually not a problem for
//   WriteLn, but may be undesirable for typical uses of Write.  In cases where you want to
//   print multi-line blocks of uninterrupted logs, compound the entire log into a single large
//   string and issue that to WriteLn.
//
struct IConsoleWriter
{
	// A direct console write, without tabbing or newlines.  Useful to devs who want to do quick
	// logging of various junk; but should *not* be used in production code due.
	void(* WriteRaw)(const char* fmt);

	// WriteLn implementation for internal use only.  Bypasses tabbing, prefixing, and other
	// formatting.
	void(* DoWriteLn)(const char* fmt);

	// SetColor implementation for internal use only.
	void(* DoSetColor)(ConsoleColors color);

	// Special implementation of DoWrite that's pretty much for MSVC use only.
	// All implementations should map to DoWrite, except Stdio which should map to Null.
	// (This avoids circular/recursive stdio output)
	void(* DoWriteFromStdout)(const char* fmt);

	void(* Newline)();
	void(* SetTitle)(const char* title);

	// internal value for indentation of individual lines.  Use the Indent() member to invoke.
	int _imm_indentation;

	// For internal use only.
	std::string _addIndentation(const std::string& src, int glob_indent) const;

	// ----------------------------------------------------------------------------
	// Public members; call these to print stuff to console!
	//
	// All functions always return false.  Return value is provided only so that we can easily
	// disable logs at compile time using the "0&&action" macro trick.

	const IConsoleWriter& SetIndent(int tabcount = 1) const;

	IConsoleWriter Indent(int tabcount = 1) const;

	bool FormatV(const char* fmt, va_list args) const;
	bool WriteLn(ConsoleColors color, const char* fmt, ...) const;
	bool WriteLn(const char* fmt, ...) const;
	bool Error(const char* fmt, ...) const;
	bool Warning(const char* fmt, ...) const;

	bool WriteLn(ConsoleColors color, const std::string& str) const;
	bool WriteLn(const std::string& str) const;
	bool Error(const std::string& str) const;
	bool Warning(const std::string& str) const;
};

// --------------------------------------------------------------------------------------
//  ConsoleIndentScope
// --------------------------------------------------------------------------------------
// Provides a scoped indentation of the IConsoleWriter interface for the current thread.
// Any console writes performed from this scope will be indented by the specified number
// of tab characters.
//
// Scoped Object Notes:  Like most scoped objects, this is intended to be used as a stack
// or temporary object only.  Using it in a situation where the object's lifespan out-lives
// a scope will almost certainly result in unintended /undefined side effects.
//
class ConsoleIndentScope
{
	DeclareNoncopyableObject(ConsoleIndentScope);

protected:
	int m_amount;
	bool m_IsScoped;

public:
	// Constructor: The specified number of tabs will be appended to the current indentation
	// setting.  The tabs will be unrolled when the object leaves scope or is destroyed.
	ConsoleIndentScope(int tabs = 1);
	virtual ~ConsoleIndentScope();
	void EnterScope();
	void LeaveScope();
};

extern IConsoleWriter Console;

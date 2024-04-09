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

#pragma once

#include "PAD/Host/PAD.h"
#include <tuple>

enum gamePadValues
{
	PAD_UP,       // Directional pad ↑
	PAD_RIGHT,    // Directional pad →
	PAD_DOWN,     // Directional pad ↓
	PAD_LEFT,     // Directional pad ←
	PAD_TRIANGLE, // Triangle button ▲
	PAD_CIRCLE,   // Circle button ●
	PAD_CROSS,    // Cross button ✖
	PAD_SQUARE,   // Square button ■
	PAD_SELECT,   // Select button
	PAD_START,    // Start button
	PAD_L1,       // L1 button
	PAD_L2,       // L2 button
	PAD_R1,       // R1 button
	PAD_R2,       // R2 button
	PAD_L3,       // Left joystick button (L3)
	PAD_R3,       // Right joystick button (R3)
	PAD_ANALOG,   // Analog mode toggle
	PAD_PRESSURE, // Pressure modifier
	PAD_L_UP,     // Left joystick (Up) ↑
	PAD_L_RIGHT,  // Left joystick (Right) →
	PAD_L_DOWN,   // Left joystick (Down) ↓
	PAD_L_LEFT,   // Left joystick (Left) ←
	PAD_R_UP,     // Right joystick (Up) ↑
	PAD_R_RIGHT,  // Right joystick (Right) →
	PAD_R_DOWN,   // Right joystick (Down) ↓
	PAD_R_LEFT,   // Right joystick (Left) ←
	MAX_KEYS
};

namespace PAD
{
	enum class ControllerType : u8;

	class KeyStatus
	{
	private:
		static constexpr u8 m_analog_released_val = 0x7F;

		struct PADAnalog
		{
			u8 lx, ly;
			u8 rx, ry;
			u8 invert_lx, invert_ly;
			u8 invert_rx, invert_ry;
		};

		PAD::ControllerType m_type[NUM_CONTROLLER_PORTS] = {};
		u32 m_button[NUM_CONTROLLER_PORTS];
		u8 m_button_pressure[NUM_CONTROLLER_PORTS][MAX_KEYS];
		PADAnalog m_analog[NUM_CONTROLLER_PORTS];
		float m_axis_scale[NUM_CONTROLLER_PORTS][2];
		float m_trigger_scale[NUM_CONTROLLER_PORTS][2];
		float m_vibration_scale[NUM_CONTROLLER_PORTS][2];
		float m_pressure_modifier[NUM_CONTROLLER_PORTS];
		float m_button_deadzone[NUM_CONTROLLER_PORTS];

	public:
		KeyStatus();
		void Init();

		void Set(u32 pad, u32 index, float value);

		__fi PAD::ControllerType GetType(u32 pad) { return m_type[pad]; }
		__fi void SetType(u32 pad, PAD::ControllerType type) { m_type[pad] = type; }

		__fi void SetAxisScale(u32 pad, float deadzone, float scale)
		{
			m_axis_scale[pad][0] = deadzone;
			m_axis_scale[pad][1] = scale;
		}
		__fi void SetTriggerScale(u32 pad, float deadzone, float scale)
		{
			m_trigger_scale[pad][0] = deadzone;
			m_trigger_scale[pad][1] = scale;
		}
		__fi float GetVibrationScale(u32 pad, u32 motor) const { return m_vibration_scale[pad][motor]; }
		__fi void SetVibrationScale(u32 pad, u32 motor, float scale) { m_vibration_scale[pad][motor] = scale; }
		__fi float GetPressureModifier(u32 pad) const { return m_pressure_modifier[pad]; }
		__fi void SetPressureModifier(u32 pad, float mod) { m_pressure_modifier[pad] = mod; }
		__fi void SetButtonDeadzone(u32 pad, float deadzone) { m_button_deadzone[pad] = deadzone; }
		__fi void SetAnalogInvertL(u32 pad, bool x, bool y)
		{
			m_analog[pad].invert_lx = x;
			m_analog[pad].invert_ly = y;
		}
		__fi void SetAnalogInvertR(u32 pad, bool x, bool y)
		{
			m_analog[pad].invert_rx = x;
			m_analog[pad].invert_ry = y;
		}

		u32 GetButtons(u32 pad);
		u8 GetPressure(u32 pad, u32 index);
	};
} // namespace PAD

extern PAD::KeyStatus g_key_status;

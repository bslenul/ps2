/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2021 PCSX2 Dev Team
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

#include "GS/Renderers/Common/GSDevice.h"
#include "GS/GSExtra.h"

#if !defined(NDEBUG) || defined(_DEBUG) || defined(_DEVEL)
#define ENABLE_OGL_DEBUG // Create a debug context and check opengl command status. Allow also to dump various textures/states.
#endif

// Note: GL messages are present in common code, so in all renderers.

#if defined(_DEBUG)
	#define GL_CACHE(...) g_gs_device->InsertDebugMessage(GSDevice::DebugMessageCategory::Cache, __VA_ARGS__)
#else
	#define GL_CACHE(...) (void)(0)
#endif

#define GL_REG(...) (void)(0)
#define GL_DBG(...) (void)(0)

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
#include "ThreadedFileReader.h"
#include <streams/file_stream.h>
#include <vector>

typedef struct _chd_file chd_file;

class ChdFileReader : public ThreadedFileReader
{
	DeclareNoncopyableObject(ChdFileReader);

public:
	ChdFileReader();
	virtual ~ChdFileReader() override;

	static bool CanHandle(const std::string& fileName, const std::string& displayName);

	bool Open2(std::string fileName) override;

	Chunk ChunkForOffset(u64 offset) override;
	int ReadChunk(void* dst, s64 blockID) override;

	void Close2(void) override;
	uint GetBlockCount(void) const override;

private:
	bool ParseTOC(u64* out_frame_count);

	chd_file* ChdFile = nullptr;
	u64 file_size = 0;
	u32 hunk_size = 0;
	std::vector<RFILE*> m_files;
};

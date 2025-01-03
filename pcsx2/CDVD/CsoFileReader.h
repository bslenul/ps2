/*  PCSX2 - PS2 Emulator for PCs
*  Copyright (C) 2002-2014  PCSX2 Dev Team
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

#include "common/FileSystem.h"

#include "ThreadedFileReader.h"
#include <zlib.h>

struct CsoHeader;
typedef struct z_stream_s z_stream;

static const uint CSO_CHUNKCACHE_SIZE_MB = 200;

class CsoFileReader : public ThreadedFileReader
{
	DeclareNoncopyableObject(CsoFileReader);

public:
	CsoFileReader();
	~CsoFileReader() override;

	static bool CanHandle(const std::string& fileName, const std::string& displayName);
	bool Open2(std::string fileName) override;

	Chunk ChunkForOffset(u64 offset) override;
	int ReadChunk(void *dst, s64 chunkID) override;

	void Close2(void) override;

	uint GetBlockCount(void) const override
	{
		return (m_totalSize - m_dataoffset) / m_blocksize;
	};

private:
	static bool ValidateHeader(const CsoHeader& hdr);
	bool ReadFileHeader();
	bool InitializeBuffers();
	int ReadFromFrame(u8* dest, u64 pos, int maxBytes);
	bool DecompressFrame(Bytef* dst, u32 frame, u32 readBufferSize);
	bool DecompressFrame(u32 frame, u32 readBufferSize);

	u32 m_frameSize;
	u8 m_frameShift;
	u8 m_indexShift;
	bool m_uselz4 = false; // flag to enable LZ4 decompression (ZSO files)
	u8* m_readBuffer;
	u32* m_index;
	u64 m_totalSize;
	// The actual source cso file handle.
	RFILE* m_src;
	z_stream* m_z_stream;
};

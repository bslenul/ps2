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

#include "PrecompiledHeader.h"

#include <array>
#include <cstring> /* memset */
#include <vector>

#include "common/Console.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include "common/StringUtil.h"

#include "MemoryCardFile.h"

#include "Config.h"
#include "Host.h"
#include "Memory.h"

#define MCD_SIZE 131072 /* Legacy PSX card default size = 1024 * 8 * 16 = 131072 */

#define MC2_MBSIZE 1081344 // Size of a single megabyte of card data = 1024 * 528 * 2 = 1081344

#define MC2_ERASE_SIZE 8448 /* 528 * 16 */

static bool FileMcd_Open = false;

// ECC code ported from mymc
// https://sourceforge.net/p/mymc-opl/code/ci/master/tree/ps2mc_ecc.py
// Public domain license

static u32 CalculateECC(u8* buf)
{
	const u8 parity_table[256] = {0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,1,0,0,1,0,1,1,
	0,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,0,1,1,0,1,0,0,1,1,0,0,1,0,
	1,1,0,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,0,1,1,
	0,1,0,0,1,1,0,0,1,0,1,1,0,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,0,
	1,1,0,1,0,0,1,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,
	0,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,1,0,0,1,0,
	1,1,0,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,0,1,1,0,1,0,0,1,1,0,0,
	1,0,1,1,0};

	const u8 column_parity_mask[256] = {0,7,22,17,37,34,51,52,52,51,34,37,17,22,
	7,0,67,68,85,82,102,97,112,119,119,112,97,102,82,85,68,67,82,85,68,67,119,112,
	97,102,102,97,112,119,67,68,85,82,17,22,7,0,52,51,34,37,37,34,51,52,0,7,22,17,
	97,102,119,112,68,67,82,85,85,82,67,68,112,119,102,97,34,37,52,51,7,0,17,22,
	22,17,0,7,51,52,37,34,51,52,37,34,22,17,0,7,7,0,17,22,34,37,52,51,112,119,102,
	97,85,82,67,68,68,67,82,85,97,102,119,112,112,119,102,97,85,82,67,68,68,67,82,
	85,97,102,119,112,51,52,37,34,22,17,0,7,7,0,17,22,34,37,52,51,34,37,52,51,7,0,
	17,22,22,17,0,7,51,52,37,34,97,102,119,112,68,67,82,85,85,82,67,68,112,119,102,
	97,17,22,7,0,52,51,34,37,37,34,51,52,0,7,22,17,82,85,68,67,119,112,97,102,102,
	97,112,119,67,68,85,82,67,68,85,82,102,97,112,119,119,112,97,102,82,85,68,67,
	0,7,22,17,37,34,51,52,52,51,34,37,17,22,7,0};

	u8 column_parity = 0x77;
	u8 line_parity_0 = 0x7F;
	u8 line_parity_1 = 0x7F;

	for (int i = 0; i < 128; i++)
	{
		u8 b = buf[i];
		column_parity ^= column_parity_mask[b];
		if (parity_table[b])
		{
			line_parity_0 ^= ~i;
			line_parity_1 ^= i;
		}
	}

	return column_parity | (line_parity_0 << 8) | (line_parity_1 << 16);
}

static bool ConvertNoECCtoRAW(const char* file_in, const char* file_out)
{
	u8 buffer[512];
	RFILE *fin = FileSystem::OpenRFile(file_in, "rb");
	if (!fin)
		return false;

	RFILE *fout = FileSystem::OpenRFile(file_out, "wb");
	if (!fout)
	{
		filestream_close(fin);
		return false;
	}

	const s64 size = FileSystem::RFSize64(fin);

	for (s64 i = 0; i < (size / 512); i++)
	{
		if (rfread(buffer, sizeof(buffer), 1, fin) != 1 ||
			rfwrite(buffer, sizeof(buffer), 1, fout) != 1)
		{
			filestream_close(fin);
			filestream_close(fout);
			return false;
		}

		for (int j = 0; j < 4; j++)
		{
			u32 checksum = CalculateECC(&buffer[j * 128]);
			if (rfwrite(&checksum, 3, 1, fout) != 1)
			{
				filestream_close(fin);
				filestream_close(fout);
				return false;
			}
		}

		u32 nullbytes = 0;
		if (rfwrite(&nullbytes, sizeof(nullbytes), 1, fout) != 1)
		{
			filestream_close(fin);
			filestream_close(fout);
			return false;
		}
	}

	filestream_close(fin);
	if (filestream_flush(fout) != 0)
		return false;
	filestream_close(fout);
	return true;
}

static bool ConvertRAWtoNoECC(const char* file_in, const char* file_out)
{
	u8 buffer[512];
	u8 checksum[16];
	RFILE *fin = FileSystem::OpenRFile(file_in, "rb");
	if (!fin)
		return false;

	RFILE *fout = FileSystem::OpenRFile(file_out, "wb");
	if (!fout)
		return false;

	const s64 size = FileSystem::RFSize64(fin);

	for (s64 i = 0; i < (size / 528); i++)
	{
		if (rfread(buffer, sizeof(buffer), 1, fin) != 1 ||
			rfwrite(buffer, sizeof(buffer), 1, fout) != 1 ||
			rfread(checksum, sizeof(checksum), 1, fin) != 1)
		{
			filestream_close(fin);
			filestream_close(fout);
			return false;
		}
	}

	filestream_close(fin);
	if (filestream_flush(fout) != 0)
		return false;
	filestream_close(fout);
	return true;
}

// --------------------------------------------------------------------------------------
//  FileMemoryCard
// --------------------------------------------------------------------------------------
// Provides thread-safe direct file IO mapping.
//
class FileMemoryCard
{
protected:
	RFILE* m_file[8] = {};
	std::string m_filenames[8] = {};
	std::vector<u8> m_currentdata;
	u64 m_chksum[8] = {};
	bool m_ispsx[8] = {};
	u32 m_chkaddr = 0;

public:
	FileMemoryCard();
	~FileMemoryCard();

	void Lock();
	void Unlock();

	void Open();
	void Close();

	s32 IsPresent(uint slot);
	void GetSizeInfo(uint slot, McdSizeInfo& outways);
	bool IsPSX(uint slot);
	s32 Read(uint slot, u8* dest, u32 adr, int size);
	s32 Save(uint slot, const u8* src, u32 adr, int size);
	s32 EraseBlock(uint slot, u32 adr);
	u64 GetCRC(uint slot);

protected:
	bool Seek(RFILE* f, u32 adr);
	bool Create(const char* mcdFile, uint sizeInMB);
};

uint FileMcd_GetMtapPort(uint slot)
{
	switch (slot)
	{
		case 1:
		case 5:
		case 6:
		case 7:
			return 1;
		case 0:
		case 2:
		case 3:
		case 4:
		default:
			break;
	}

	return 0;
}

// Returns the multitap slot number, range 1 to 3 (slot 0 refers to the standard
// 1st and 2nd player slots).
uint FileMcd_GetMtapSlot(uint slot)
{
	switch (slot)
	{
		case 2:
		case 3:
		case 4:
			return slot - 1;
		case 5:
		case 6:
		case 7:
			return slot - 4;
		case 0:
		case 1:
		default:
			break;
	}

	return 0; // technically unreachable.
}

bool FileMcd_IsMultitapSlot(uint slot)
{
	return (slot > 1);
}

std::string FileMcd_GetDefaultName(uint slot)
{
	if (FileMcd_IsMultitapSlot(slot))
		return StringUtil::StdStringFromFormat("Mcd-Multitap%u-Slot%02u.ps2", FileMcd_GetMtapPort(slot) + 1, FileMcd_GetMtapSlot(slot) + 1);
	return StringUtil::StdStringFromFormat("Mcd%03u.ps2", slot + 1);
}

FileMemoryCard::FileMemoryCard() = default;

FileMemoryCard::~FileMemoryCard() = default;

void FileMemoryCard::Open()
{
	for (int slot = 0; slot < 8; ++slot)
	{
		m_filenames[slot] = {};

		if (FileMcd_IsMultitapSlot(slot))
		{
			if (!EmuConfig.MultitapPort0_Enabled && (FileMcd_GetMtapPort(slot) == 0))
				continue;
			if (!EmuConfig.MultitapPort1_Enabled && (FileMcd_GetMtapPort(slot) == 1))
				continue;
		}

		std::string fname(EmuConfig.FullpathToMcd(slot));
		bool cont = false;

		if (fname.empty())
			cont = true;

		if (!EmuConfig.Mcd[slot].Enabled)
			cont = true;

		if (EmuConfig.Mcd[slot].Type != MemoryCardType::File)
			cont = true;

		if (cont)
			continue;

		if (FileSystem::GetPathFileSize(fname.c_str()) <= 0)
		{
			// FIXME : Ideally this should prompt the user for the size of the
			// memory card file they would like to create, instead of trying to
			// create one automatically.

			if (!Create(fname.c_str(), 8))
				Console.Error("Could not create a memory card: \n\n%s\n\n",
					fname.c_str());
		}

		// [TODO] : Add memcard size detection and report it to the console log.
		//   (8MB, 256Mb, formatted, unformatted, etc ...)

		if (StringUtil::EndsWith(fname, ".bin"))
		{
			std::string newname(fname + "x");
			if (!ConvertNoECCtoRAW(fname.c_str(), newname.c_str()))
			{
				FileSystem::DeleteFilePath(newname.c_str());
				continue;
			}

			// store the original filename
			m_file[slot] = FileSystem::OpenRFile(newname.c_str(), "r+b");
		}
		else
			m_file[slot] = FileSystem::OpenRFile(fname.c_str(), "r+b");

		if (m_file[slot]) // Load checksum
		{
			m_filenames[slot] = std::move(fname);
			m_ispsx[slot] = FileSystem::RFSize64(m_file[slot]) == 0x20000;
			m_chkaddr = 0x210;

			if (!m_ispsx[slot] && FileSystem::RFSeek64(m_file[slot], m_chkaddr, SEEK_SET) == 0)
			{
				const size_t read_result = rfread(&m_chksum[slot], sizeof(m_chksum[slot]), 1, m_file[slot]);
				if (read_result == 0)
					Console.Error("Error reading memcard.");
			}
		}
	}
}

void FileMemoryCard::Close()
{
	for (int slot = 0; slot < 8; ++slot)
	{
		if (!m_file[slot])
			continue;

		// Store checksum
		if (!m_ispsx[slot] && FileSystem::RFSeek64(m_file[slot], m_chkaddr, SEEK_SET) == 0)
			rfwrite(&m_chksum[slot], sizeof(m_chksum[slot]), 1, m_file[slot]);

		rfclose(m_file[slot]);
		m_file[slot] = nullptr;

		if (StringUtil::EndsWith(m_filenames[slot], ".bin"))
		{
			const std::string name_in(m_filenames[slot] + 'x');
			if (ConvertRAWtoNoECC(name_in.c_str(), m_filenames[slot].c_str()))
				FileSystem::DeleteFilePath(name_in.c_str());
		}

		m_filenames[slot] = {};
	}
}

// Returns FALSE if the seek failed (is outside the bounds of the file).
bool FileMemoryCard::Seek(RFILE* f, u32 adr)
{
	const s64 size = FileSystem::RFSize64(f);

	// If anyone knows why this filesize logic is here (it appears to be related to legacy PSX
	// cards, perhaps hacked support for some special emulator-specific memcard formats that
	// had header info?), then please replace this comment with something useful.  Thanks!  -- air

	u32 offset = 0;

	if (size == MCD_SIZE + 64)
		offset = 64;
	else if (size == MCD_SIZE + 3904)
		offset = 3904;
	return (FileSystem::RFSeek64(f, adr + offset, SEEK_SET) == 0);
}

// returns FALSE if an error occurred (either permission denied or disk full)
bool FileMemoryCard::Create(const char* mcdFile, uint sizeInMB)
{
	u8 buf[MC2_ERASE_SIZE];
	RFILE *fp = FileSystem::OpenRFile(mcdFile, "wb");
	if (!fp)
		return false;

	memset(buf, 0xff, sizeof(buf));

	for (uint i = 0; i < (MC2_MBSIZE * sizeInMB) / sizeof(buf); i++)
	{
		if (rfwrite(buf, sizeof(buf), 1, fp) != 1)
		{
			filestream_close(fp);
			return false;
		}
	}
	filestream_close(fp);
	return true;
}

s32 FileMemoryCard::IsPresent(uint slot)
{
	return m_file[slot] != nullptr;
}

void FileMemoryCard::GetSizeInfo(uint slot, McdSizeInfo& outways)
{
	outways.SectorSize = 512;             // 0x0200
	outways.EraseBlockSizeInSectors = 16; // 0x0010
	outways.Xor = 18;                     // 0x12, XOR 02 00 00 10

	if (m_file[slot])
		outways.McdSizeInSectors = static_cast<u32>(FileSystem::RFSize64(m_file[slot])) / (outways.SectorSize + outways.EraseBlockSizeInSectors);
	else
		outways.McdSizeInSectors = 0x4000;

	u8* pdata = (u8*)&outways.McdSizeInSectors;
	outways.Xor ^= pdata[0] ^ pdata[1] ^ pdata[2] ^ pdata[3];
}

bool FileMemoryCard::IsPSX(uint slot)
{
	return m_ispsx[slot];
}

s32 FileMemoryCard::Read(uint slot, u8* dest, u32 adr, int size)
{
	RFILE* mcfp = m_file[slot];
	if (!mcfp)
	{
		memset(dest, 0, size);
		return 1;
	}
	if (!Seek(mcfp, adr))
		return 0;
	return rfread(dest, size, 1, mcfp) == 1;
}

s32 FileMemoryCard::Save(uint slot, const u8* src, u32 adr, int size)
{
	RFILE* mcfp = m_file[slot];

	if (!mcfp)
		return 1;

	if (m_ispsx[slot])
	{
		if (static_cast<int>(m_currentdata.size()) < size)
			m_currentdata.resize(size);
		for (int i = 0; i < size; i++)
			m_currentdata[i] = src[i];
	}
	else
	{
		if (!Seek(mcfp, adr))
			return 0;
		if (static_cast<int>(m_currentdata.size()) < size)
			m_currentdata.resize(size);

		const size_t read_result = rfread(m_currentdata.data(), size, 1, mcfp);

		for (int i = 0; i < size; i++)
			m_currentdata[i] &= src[i];

		// Checksumness
		{
			u64* pdata = (u64*)&m_currentdata[0];
			u32 loops = size / 8;

			for (u32 i = 0; i < loops; i++)
				m_chksum[slot] ^= pdata[i];
		}
	}

	if (!Seek(mcfp, adr))
		return 0;

	if (rfwrite(m_currentdata.data(), size, 1, mcfp) == 1)
		return 1;

	return 0;
}

s32 FileMemoryCard::EraseBlock(uint slot, u32 adr)
{
	u8 buf[MC2_ERASE_SIZE];
	RFILE* mcfp = m_file[slot];
	if (!mcfp)
		return 1;

	if (!Seek(mcfp, adr))
		return 0;
	memset(buf, 0xff, sizeof(buf));
	return rfwrite(buf, sizeof(buf), 1, mcfp) == 1;
}

u64 FileMemoryCard::GetCRC(uint slot)
{
	RFILE* mcfp = m_file[slot];
	if (!mcfp)
		return 0;
	if (m_ispsx[slot])
	{
		u64 retval = 0;
		if (!Seek(mcfp, 0))
			return 0;

		const s64 mcfpsize = FileSystem::RFSize64(mcfp);
		if (mcfpsize < 0)
			return 0;

		// Process the file in 4k chunks.  Speeds things up significantly.

		u64 buffer[528 * 8]; // use 528 (sector size), ensures even divisibility

		const uint filesize = static_cast<uint>(mcfpsize) / sizeof(buffer);
		for (uint i = filesize; i; --i)
		{
			if (rfread(buffer, sizeof(buffer), 1, mcfp) != 1)
				return 0;

			for (uint t = 0; t < std::size(buffer); ++t)
				retval ^= buffer[t];
		}
		return retval;
	}
	return m_chksum[slot];
}

// --------------------------------------------------------------------------------------
//  MemoryCard Component API Bindings
// --------------------------------------------------------------------------------------
namespace Mcd
{
	FileMemoryCard impl; // class-based implementations we refer to when API is invoked
}; // namespace Mcd

uint FileMcd_ConvertToSlot(uint port, uint slot)
{
	if (slot == 0)
		return port;
	if (port == 0)
		return slot + 1; // multitap 1
	return slot + 4;     // multitap 2
}

void FileMcd_EmuOpen(void)
{
	if(FileMcd_Open)
		return;
	FileMcd_Open = true;
	// detect inserted memory card types
	for (uint slot = 0; slot < 8; ++slot)
	{
		if (EmuConfig.Mcd[slot].Filename.empty())
			EmuConfig.Mcd[slot].Type = MemoryCardType::Empty;
		else if (EmuConfig.Mcd[slot].Enabled)
			EmuConfig.Mcd[slot].Type = MemoryCardType::File;
	}

	Mcd::impl.Open();
}

void FileMcd_EmuClose(void)
{
	if(!FileMcd_Open)
		return;
	FileMcd_Open = false;
	Mcd::impl.Close();
}

s32 FileMcd_IsPresent(uint port, uint slot)
{
	const uint combinedSlot = FileMcd_ConvertToSlot(port, slot);
	if (EmuConfig.Mcd[combinedSlot].Type == MemoryCardType::File)
		return Mcd::impl.IsPresent(combinedSlot);
	return false;
}

void FileMcd_GetSizeInfo(uint port, uint slot, McdSizeInfo* outways)
{
	const uint combinedSlot = FileMcd_ConvertToSlot(port, slot);
	if (EmuConfig.Mcd[combinedSlot].Type == MemoryCardType::File)
		Mcd::impl.GetSizeInfo(combinedSlot, *outways);
}

bool FileMcd_IsPSX(uint port, uint slot)
{
	const uint combinedSlot = FileMcd_ConvertToSlot(port, slot);
	if (EmuConfig.Mcd[combinedSlot].Type == MemoryCardType::File)
		return Mcd::impl.IsPSX(combinedSlot);
	return false;
}

s32 FileMcd_Read(uint port, uint slot, u8* dest, u32 adr, int size)
{
	const uint combinedSlot = FileMcd_ConvertToSlot(port, slot);
	if (EmuConfig.Mcd[combinedSlot].Type == MemoryCardType::File)
		return Mcd::impl.Read(combinedSlot, dest, adr, size);
	return 0;
}

s32 FileMcd_Save(uint port, uint slot, const u8* src, u32 adr, int size)
{
	const uint combinedSlot = FileMcd_ConvertToSlot(port, slot);
	if (EmuConfig.Mcd[combinedSlot].Type == MemoryCardType::File)
		return Mcd::impl.Save(combinedSlot, src, adr, size);
	return 0;
}

s32 FileMcd_EraseBlock(uint port, uint slot, u32 adr)
{
	const uint combinedSlot = FileMcd_ConvertToSlot(port, slot);
	if (EmuConfig.Mcd[combinedSlot].Type == MemoryCardType::File)
		return Mcd::impl.EraseBlock(combinedSlot, adr);
	return 0;
}

u64 FileMcd_GetCRC(uint port, uint slot)
{
	const uint combinedSlot = FileMcd_ConvertToSlot(port, slot);
	if (EmuConfig.Mcd[combinedSlot].Type == MemoryCardType::File)
		return Mcd::impl.GetCRC(combinedSlot);
	return 0;
}

bool FileMcd_ReIndex(uint port, uint slot, const std::string& filter) { return false; }

// --------------------------------------------------------------------------------------
//  Library API Implementations
// --------------------------------------------------------------------------------------

static MemoryCardFileType GetMemoryCardFileTypeFromSize(s64 size)
{
	if (size == (8 * MC2_MBSIZE))
		return MemoryCardFileType::PS2_8MB;
	else if (size == (16 * MC2_MBSIZE))
		return MemoryCardFileType::PS2_16MB;
	else if (size == (32 * MC2_MBSIZE))
		return MemoryCardFileType::PS2_32MB;
	else if (size == (64 * MC2_MBSIZE))
		return MemoryCardFileType::PS2_64MB;
	else if (size == MCD_SIZE)
		return MemoryCardFileType::PS1;
	return MemoryCardFileType::Unknown;
}

static bool IsMemoryCardFormatted(const std::string& path)
{
	static const char formatted_psx[] = "MC";
	static const char formatted_string[] = "Sony PS2 Memory Card Format";
	static constexpr size_t read_length = sizeof(formatted_string) - 1;
	u8 data[read_length];
	RFILE *fp = FileSystem::OpenRFile(path.c_str(), "rb");
	if (!fp)
		return false;

	int64_t ret = rfread(data, read_length, 1, fp);
	filestream_close(fp);
	if (ret != 1)
		return false;
	return (   std::memcmp(data, formatted_string, sizeof(formatted_string) - 1) == 0
		|| std::memcmp(data, formatted_psx, sizeof(formatted_psx) - 1) == 0);
}

std::vector<AvailableMcdInfo> FileMcd_GetAvailableCards(bool include_in_use_cards)
{
	std::vector<FILESYSTEM_FIND_DATA> files;
	FileSystem::FindFiles(EmuFolders::MemoryCards.c_str(), "*",
		FILESYSTEM_FIND_FILES | FILESYSTEM_FIND_FOLDERS | FILESYSTEM_FIND_HIDDEN_FILES, &files);

	std::vector<AvailableMcdInfo> mcds;
	mcds.reserve(files.size());

	for (FILESYSTEM_FIND_DATA& fd : files)
	{
		std::string basename(Path::GetFileName(fd.FileName));
		if (!include_in_use_cards)
		{
			bool in_use = false;
			for (size_t i = 0; i < std::size(EmuConfig.Mcd); i++)
			{
				if (EmuConfig.Mcd[i].Filename == basename)
				{
					in_use = true;
					break;
				}
			}
			if (in_use)
				continue;
		}

		if (fd.Attributes & FILESYSTEM_FILE_ATTRIBUTE_DIRECTORY) { }
		else
		{
			if (fd.Size < MCD_SIZE)
				continue;

			const bool formatted = IsMemoryCardFormatted(fd.FileName);
			mcds.push_back({std::move(basename), std::move(fd.FileName), fd.ModificationTime,
				MemoryCardType::File, GetMemoryCardFileTypeFromSize(fd.Size),
				static_cast<u32>(fd.Size), formatted});
		}
	}

	return mcds;
}

std::optional<AvailableMcdInfo> FileMcd_GetCardInfo(const std::string_view& name)
{
	std::optional<AvailableMcdInfo> ret;

	std::string basename(name);
	std::string path(Path::Combine(EmuFolders::MemoryCards, basename));

	FILESYSTEM_STAT_DATA sd;
	if (!FileSystem::StatFile(path.c_str(), &sd))
		return ret;

	if (sd.Attributes & FILESYSTEM_FILE_ATTRIBUTE_DIRECTORY) { }
	else
	{
		if (sd.Size >= MCD_SIZE)
		{
			const bool formatted = IsMemoryCardFormatted(path);
			ret = {std::move(basename), std::move(path), sd.ModificationTime,
				MemoryCardType::File, GetMemoryCardFileTypeFromSize(sd.Size),
				static_cast<u32>(sd.Size), formatted};
		}
	}

	return ret;
}

bool FileMcd_CreateNewCard(const std::string_view& name, MemoryCardType type, MemoryCardFileType file_type)
{
	if (type == MemoryCardType::File)
	{
		if (file_type <= MemoryCardFileType::Unknown || file_type >= MemoryCardFileType::MaxCount)
			return false;

		static constexpr std::array<u32, static_cast<size_t>(MemoryCardFileType::MaxCount)> sizes = {{0, 8 * MC2_MBSIZE, 16 * MC2_MBSIZE, 32 * MC2_MBSIZE, 64 * MC2_MBSIZE, MCD_SIZE}};

		const bool isPSX = (type == MemoryCardType::File && file_type == MemoryCardFileType::PS1);
		const u32 size = sizes[static_cast<u32>(file_type)];
		if (!isPSX && size == 0)
			return false;

		const std::string full_path(Path::Combine(EmuFolders::MemoryCards, name));
		RFILE *fp = FileSystem::OpenRFile(full_path.c_str(), "wb");
		if (!fp)
			return false;

		if (!isPSX)
		{
			// PS2 Memory Card
			u8 buf[MC2_ERASE_SIZE];
			memset(buf, 0xff, sizeof(buf));

			const u32 count = size / sizeof(buf);
			for (uint i = 0; i < count; i++)
			{
				if (rfwrite(buf, sizeof(buf), 1, fp) != 1)
				{
					filestream_close(fp);
					return false;
				}
			}
		}
		else
		{
			// PSX Memory Card; 8192 is the size in bytes of a single block of a PSX memory card (8 KiB).
			u8 buf[8192];
			memset(buf, 0xff, sizeof(buf));

			// PSX cards consist of 16 blocks, each 8 KiB in size.
			for (uint i = 0; i < 16; i++)
			{
				if (rfwrite(buf, sizeof(buf), 1, fp) != 1)
				{
					filestream_close(fp);
					return false;
				}
			}
		}

		filestream_close(fp);
		return true;
	}

	return false;
}

bool FileMcd_RenameCard(const std::string_view& name, const std::string_view& new_name)
{
	const std::string name_path(Path::Combine(EmuFolders::MemoryCards, name));
	const std::string new_name_path(Path::Combine(EmuFolders::MemoryCards, new_name));
	FILESYSTEM_STAT_DATA sd, new_sd;
	if (!FileSystem::StatFile(name_path.c_str(), &sd) || FileSystem::StatFile(new_name_path.c_str(), &new_sd))
		return false;
	if (!FileSystem::RenamePath(name_path.c_str(), new_name_path.c_str()))
		return false;
	return true;
}

bool FileMcd_DeleteCard(const std::string_view& name)
{
	const std::string name_path(Path::Combine(EmuFolders::MemoryCards, name));

	FILESYSTEM_STAT_DATA sd;
	if (!FileSystem::StatFile(name_path.c_str(), &sd))
		return false;

	if (sd.Attributes & FILESYSTEM_FILE_ATTRIBUTE_DIRECTORY) { }
	else
	{
		if (!FileSystem::DeleteFilePath(name_path.c_str()))
			return false;
	}

	return true;
}

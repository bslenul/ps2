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

#include "PrecompiledHeader.h"
#include "AsyncFileReader.h"
#include "common/FileSystem.h"

#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>

#ifdef __linux__
#include <sys/syscall.h>
static int io_setup(unsigned nr, aio_context_t * ctxp)
{
   return syscall(__NR_io_setup, nr, ctxp);
}

static int io_destroy(aio_context_t ctx)
{
   return syscall(__NR_io_destroy, ctx);
}

static int io_submit(aio_context_t ctx, long nr, struct iocb ** cbp)
{
   return syscall(__NR_io_submit, ctx, nr, cbp);
}

static int io_cancel(aio_context_t ctx, struct iocb * iocb, struct io_event * result)
{
   return syscall(__NR_io_cancel, ctx, iocb, result);
}

static int io_getevents(aio_context_t ctx, long min_nr, long nr,
      struct io_event * events, struct timespec * timeout)
{
   return syscall(__NR_io_getevents, ctx, min_nr, nr, events, timeout);
}
static inline void _io_prep_pread(struct iocb *iocb, int fd, void *buf, size_t count, long long offset)
{
	memset(iocb, 0, sizeof(*iocb));
	iocb->aio_fildes     = fd;
	iocb->aio_lio_opcode = IO_CMD_PREAD;
	iocb->aio_reqprio    = 0;
	iocb->u.c.buf        = buf;
	iocb->u.c.nbytes     = count;
	iocb->u.c.offset     = offset;
}
#endif

FlatFileReader::FlatFileReader()
{
	m_blocksize = 2048;
	m_fd = -1;
	m_aio_context = 0;
}

FlatFileReader::~FlatFileReader(void)
{
	Close();
}

bool FlatFileReader::Open(std::string fileName)
{
	m_filename = std::move(fileName);

	int err = io_setup(64, &m_aio_context);
	if (err)
		return false;

	m_fd = FileSystem::OpenFDFile(m_filename.c_str(), O_RDONLY, 0);

	return (m_fd != -1);
}

int FlatFileReader::ReadSync(void* pBuffer, uint sector, uint count)
{
	BeginRead(pBuffer, sector, count);
	return FinishRead();
}

void FlatFileReader::BeginRead(void* pBuffer, uint sector, uint count)
{
	u64 offset;
	offset = sector * (s64)m_blocksize + m_dataoffset;

	u32 bytesToRead = count * m_blocksize;

	struct iocb iocb;
	struct iocb* iocbs = &iocb;

#ifdef __linux__
	_io_prep_pread(&iocb, m_fd, pBuffer, bytesToRead, offset);
#else
	io_prep_pread(&iocb, m_fd, pBuffer, bytesToRead, offset);
#endif
	io_submit(m_aio_context, 1, &iocbs);
}

int FlatFileReader::FinishRead(void)
{
	struct io_event event;

	int nevents = io_getevents(m_aio_context, 1, 1, &event, NULL);
	if (nevents < 1)
		return -1;

	return event.res;
}

void FlatFileReader::CancelRead(void)
{
	// Will be done when m_aio_context context is destroyed
	// Note: io_cancel exists but need the iocb structure as parameter
	// int io_cancel(aio_context_t ctx_id, struct iocb *iocb,
	//                struct io_event *result);
}

void FlatFileReader::Close(void)
{

	if (m_fd != -1)
		close(m_fd);

	io_destroy(m_aio_context);

	m_fd = -1;
	m_aio_context = 0;
}

uint FlatFileReader::GetBlockCount(void) const
{
#if defined(__HAIKU__) || defined(__APPLE__) || defined(__FreeBSD__)
	struct stat sysStatData;
	if (fstat(m_fd, &sysStatData) < 0)
		return 0;
#else
	struct stat64 sysStatData;
	if (fstat64(m_fd, &sysStatData) < 0)
		return 0;
#endif

	return (int)(sysStatData.st_size / m_blocksize);
}

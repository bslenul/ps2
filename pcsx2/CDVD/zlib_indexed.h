/* zran.c -- example of zlib/gzip stream indexing and random access

Copyright (C) 2005, 2012 Mark Adler

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
claim that you wrote the original software. If you use this software
in a product, an acknowledgment in the product documentation would be
appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.

Jean-loup Gailly        Mark Adler
jloup@gzip.org          madler@alumni.caltech.edu


The data format used by the zlib library is described by RFCs (Request for
Comments) 1950 to 1952 in the files http://tools.ietf.org/html/rfc1950
(zlib format), rfc1951 (deflate format) and rfc1952 (gzip format).
*/

/* zran.c -- example of zlib/gzip stream indexing and random access
 * Copyright (C) 2005, 2012 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h
   Version 1.1  29 Sep 2012  Mark Adler */

/* Version History:
 1.0  29 May 2005  First version
 1.1  29 Sep 2012  Fix memory reallocation error

 1.1+ 16 Apr 2014  PCSX2 adaptation
    (This is verbatim copy from zlib/examples/zran.c, with the following mods):
  - Added an explicit license clause taken from zlib.h and removed the sample main(...).
  - zlib include path and included Pcsx2Types.h for windows off_t (s64)
  - fseeko and off_t #define'ed for windows too (on windows off_t is 32b and no fseeko)
  - typedefs for struct access/point (Access/Point) and allocation type casts
  - access: added members span and uncompressed_size which are filled by build_index.
  - point and access packed for safety since they go to disk as is (but no endian-ness handling).
      But they're still aligned since each member size is multiple of 4, so no perf issues.
  - extract: added state import/export for instant sequential access regardless of index
      (Thanks to Mark Adler for suggesting the approach)
  - build_index(...) - added progress prints
  - CHUNK changed from 16k to 512k
 */

/* Illustrate the use of Z_BLOCK, inflatePrime(), and inflateSetDictionary()
   for random access of a compressed file.  A file containing a zlib or gzip
   stream is provided on the command line.  The compressed stream is decoded in
   its entirety, and an index built with access points about every SPAN bytes
   in the uncompressed output.  The compressed file is left open, and can then
   be read randomly, having to decompress on the average SPAN/2 uncompressed
   bytes before getting to the desired block of data.

   An access point can be created at the start of any deflate block, by saving
   the starting file offset and bit of that block, and the 32K bytes of
   uncompressed data that precede that block.  Also the uncompressed offset of
   that block is saved to provide a referece for locating a desired starting
   point in the uncompressed stream.  build_index() works by decompressing the
   input zlib or gzip stream a block at a time, and at the end of each block
   deciding if enough uncompressed data has gone by to justify the creation of
   a new access point.  If so, that point is saved in a data structure that
   grows as needed to accommodate the points.

   To use the index, an offset in the uncompressed data is provided, for which
   the latest accees point at or preceding that offset is located in the index.
   The input file is positioned to the specified location in the index, and if
   necessary the first few bits of the compressed data is read from the file.
   inflate is initialized with those bits and the 32K of uncompressed data, and
   the decompression then proceeds until the desired offset in the file is
   reached.  Then the decompression continues to read the desired uncompressed
   data from the file.

   Another approach would be to generate the index on demand.  In that case,
   requests for random access reads from the compressed data would try to use
   the index, but if a read far enough past the end of the index is required,
   then further index entries would be generated and added.

   There is some fair bit of overhead to starting inflation for the random
   access, mainly copying the 32K byte dictionary.  So if small pieces of the
   file are being accessed, it would make sense to implement a cache to hold
   some lookahead and avoid many calls to extract() for small lengths.

   Another way to build an index would be to use inflateCopy().  That would
   not be constrained to have access points at block boundaries, but requires
   more memory per access point, and also cannot be saved to file due to the
   use of pointers in the state.  The approach here allows for storage of the
   index in a file.
 */

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "common/FileSystem.h"

//#define SPAN (1048576L)  /* desired distance between access points */
#define WINSIZE 32768U    /* sliding window size */
#define CHUNK (64 * 1024) /* file input buffer size */

#ifdef _WIN32
#pragma pack(push, indexData, 1)
#endif

/* access point entry */
struct point
{
	s64 out;                  /* corresponding offset in uncompressed data */
	s64 in;                   /* offset in input file of first full byte */
	int bits;                      /* number of bits (1-7) from byte at in - 1, or 0 */
	unsigned char window[WINSIZE]; /* preceding 32K of uncompressed data */
}
#ifndef _WIN32
__attribute__((packed))
#endif
;

typedef struct point Point;

/* access point list */
struct access
{
	int have;           /* number of list entries filled in */
	int size;           /* number of list entries allocated (only used internally during build)*/
	struct point* list; /* allocated list */

	s32 span;                   /* once the index is built, holds the span size used to build it */
	s64 uncompressed_size; /* filled by build_index */
}
#ifndef _WIN32
__attribute__((packed))
#endif
;

typedef struct access Access;

#ifdef _WIN32
#pragma pack(pop, indexData)
#endif

/* Deallocate an index built by build_index() */
static inline void free_index(struct access* index)
{
	if (index != NULL)
	{
		free(index->list);
		free(index);
	}
}

/* Add an entry to the access point list.  If out of memory, deallocate the
   existing list and return NULL. */
static inline struct access* addpoint(struct access* index, int bits,
							  s64 in, s64 out, unsigned left, unsigned char* window)
{
	struct point* next;

	/* if list is empty, create it (start with eight points) */
	if (index == NULL)
	{
		index = (Access*)malloc(sizeof(struct access));
		if (index == NULL)
			return NULL;
		index->list = (Point*)malloc(sizeof(struct point) << 3);
		if (index->list == NULL)
		{
			free(index);
			return NULL;
		}
		index->size = 8;
		index->have = 0;
	}

	/* if list is full, make it bigger */
	else if (index->have == index->size)
	{
		index->size <<= 1;
		next = (Point*)realloc(index->list, sizeof(struct point) * index->size);
		if (next == NULL)
		{
			free_index(index);
			return NULL;
		}
		index->list = next;
	}

	/* fill in entry and increment how many we have */
	next = index->list + index->have;
	next->bits = bits;
	next->in = in;
	next->out = out;
	if (left)
		memcpy(next->window, window + WINSIZE - left, left);
	if (left < WINSIZE)
		memcpy(next->window + left, window, WINSIZE - left);
	index->have++;

	/* return list, possibly reallocated */
	return index;
}

/* Make one entire pass through the compressed stream and build an index, with
   access points about every span bytes of uncompressed output -- span is
   chosen to balance the speed of random access against the memory requirements
   of the list, about 32K bytes per access point.  Note that data after the end
   of the first zlib or gzip stream in the file is ignored.  build_index()
   returns the number of access points on success (>= 1), Z_MEM_ERROR for out
   of memory, Z_DATA_ERROR for an error in the input file, or Z_ERRNO for a
   file read error.  On success, *built points to the resulting index. */
static inline int build_index(RFILE* in, s64 span, struct access** built)
{
	int ret;
	s64 totin, totout, totPrinted; /* our own total counters to avoid 4GB limit */
	s64 last;                      /* totout value of last access point */
	struct access* index;               /* access points being generated */
	z_stream strm;
	unsigned char input[CHUNK];
	unsigned char window[WINSIZE];

	/* initialize inflate */
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	strm.avail_in = 0;
	strm.next_in = Z_NULL;
	ret = inflateInit2(&strm, 47); /* automatic zlib or gzip decoding */
	if (ret != Z_OK)
		return ret;

	/* inflate the input, maintain a sliding window, and build an index -- this
       also validates the integrity of the compressed data using the check
       information at the end of the gzip or zlib stream */
	totin = totout = last = totPrinted = 0;
	index = NULL; /* will be allocated by first addpoint() */
	strm.avail_out = 0;
	do
	{
		/* get some compressed data from input file */
		strm.avail_in = rfread(input, 1, CHUNK, in);
		if (rferror(in))
		{
			ret = Z_ERRNO;
			goto build_index_error;
		}
		if (strm.avail_in == 0)
		{
			ret = Z_DATA_ERROR;
			goto build_index_error;
		}
		strm.next_in = input;

		/* process all of that, or until end of stream */
		do
		{
			/* reset sliding window if necessary */
			if (strm.avail_out == 0)
			{
				strm.avail_out = WINSIZE;
				strm.next_out = window;
			}

			/* inflate until out of input, output, or at end of block --
               update the total input and output counters */
			totin += strm.avail_in;
			totout += strm.avail_out;
			ret = inflate(&strm, Z_BLOCK); /* return at end of block */
			totin -= strm.avail_in;
			totout -= strm.avail_out;
			if (ret == Z_NEED_DICT)
				ret = Z_DATA_ERROR;
			if (ret == Z_MEM_ERROR || ret == Z_DATA_ERROR)
				goto build_index_error;
			if (ret == Z_STREAM_END)
				break;

			/* if at end of block, consider adding an index entry (note that if
               data_type indicates an end-of-block, then all of the
               uncompressed data from that block has been delivered, and none
               of the compressed data after that block has been consumed,
               except for up to seven bits) -- the totout == 0 provides an
               entry point after the zlib or gzip header, and assures that the
               index always has at least one access point; we avoid creating an
               access point after the last block by checking bit 6 of data_type
             */
			if ((strm.data_type & 128) && !(strm.data_type & 64) &&
				(totout == 0 || totout - last > span))
			{
				index = addpoint(index, strm.data_type & 7, totin,
								 totout, strm.avail_out, window);
				if (index == NULL)
				{
					ret = Z_MEM_ERROR;
					goto build_index_error;
				}
				last = totout;
			}
		} while (strm.avail_in != 0);
		if (totin / (50 * 1024 * 1024) != totPrinted / (50 * 1024 * 1024))
			totPrinted = totin;
	} while (ret != Z_STREAM_END);

	if (index == NULL)
	{
		// Could happen if the start of the stream in Z_STREAM_END
		return 0;
	}

	/* clean up and return index (release unused entries in list) */
	(void)inflateEnd(&strm);
	index->list = (Point*)realloc(index->list, sizeof(struct point) * index->have);
	index->size = index->have;
	index->span = span;
	index->uncompressed_size = totout;
	*built = index;
	return index->have;

	/* return error */
build_index_error:
	(void)inflateEnd(&strm);
	if (index != NULL)
		free_index(index);
	return ret;
}

typedef struct zstate
{
	s64 out_offset;
	s64 in_offset;
	z_stream strm;
	int isValid;
} Zstate;

static inline s64 getInOffset(zstate* state)
{
	return state->in_offset;
}

/* Use the index to read len bytes from offset into buf, return bytes read or
   negative for error (Z_DATA_ERROR or Z_MEM_ERROR).  If data is requested past
   the end of the uncompressed data, then extract() will return a value less
   than len, indicating how much as actually read into buf.  This function
   should not return a data error unless the file was modified since the index
   was generated.  extract() may also return Z_ERRNO if there is an error on
   reading or seeking the input file. */
static inline int extract(RFILE* in, struct access* index, s64 offset,
				  unsigned char* buf, int len, zstate* state)
{
	int ret, skip;
	struct point* here;
	unsigned char input[CHUNK];
	unsigned char discard[WINSIZE];
	int isEnd = 0;

	/* proceed only if something reasonable to do */
	if (len < 0 || state == nullptr)
		return 0;

	if (state->isValid && offset != state->out_offset)
	{
		// state doesn't match offset, free allocations before strm is overwritten
		inflateEnd(&state->strm);
		state->isValid = 0;
	}
	state->out_offset = offset;

	if (state->isValid)
	{
		state->isValid = 0; // we took control over strm. revalidate when/if we give it back
		FileSystem::FSeek64(in, state->in_offset, SEEK_SET);
		state->strm.avail_in = 0;
		offset = 0;
		skip = 1;
	}
	else
	{
		/* find where in stream to start */
		here = index->list;
		ret = index->have;
		while (--ret && here[1].out <= offset)
			here++;

		/* initialize file and inflate state to start there */
		state->strm.zalloc = Z_NULL;
		state->strm.zfree = Z_NULL;
		state->strm.opaque = Z_NULL;
		state->strm.avail_in = 0;
		state->strm.next_in = Z_NULL;
		ret = inflateInit2(&state->strm, -15); /* raw inflate */
		if (ret != Z_OK)
			return ret;
		ret = FileSystem::FSeek64(in, here->in - (here->bits ? 1 : 0), SEEK_SET);
		if (ret == -1)
			goto extract_ret;
		if (here->bits)
		{
			ret = rfgetc(in);
			if (ret == -1)
			{
				ret = rferror(in) ? Z_ERRNO : Z_DATA_ERROR;
				goto extract_ret;
			}
			inflatePrime(&state->strm, here->bits, ret >> (8 - here->bits));
		}
		inflateSetDictionary(&state->strm, here->window, WINSIZE);

		/* skip uncompressed bytes until offset reached, then satisfy request */
		offset -= here->out;
		state->strm.avail_in = 0;
		skip = 1; /* while skipping to offset */
	}

	do
	{
		/* define where to put uncompressed data, and how much */
		if (offset == 0 && skip)
		{ /* at offset now */
			state->strm.avail_out = len;
			state->strm.next_out = buf;
			skip = 0; /* only do this once */
		}
		if (offset > WINSIZE)
		{ /* skip WINSIZE bytes */
			state->strm.avail_out = WINSIZE;
			state->strm.next_out = discard;
			offset -= WINSIZE;
		}
		else if (offset != 0)
		{ /* last skip */
			state->strm.avail_out = (unsigned)offset;
			state->strm.next_out = discard;
			offset = 0;
		}

		/* uncompress until avail_out filled, or end of stream */
		do
		{
			if (state->strm.avail_in == 0)
			{
				state->in_offset = FileSystem::FTell64(in);
				state->strm.avail_in = rfread(input, 1, CHUNK, in);
				if (rferror(in))
				{
					ret = Z_ERRNO;
					goto extract_ret;
				}
				if (state->strm.avail_in == 0)
				{
					ret = Z_DATA_ERROR;
					goto extract_ret;
				}
				state->strm.next_in = input;
			}
			uint prev_in = state->strm.avail_in;
			ret = inflate(&state->strm, Z_NO_FLUSH); /* normal inflate */
			state->in_offset += (prev_in - state->strm.avail_in);
			if (ret == Z_NEED_DICT)
				ret = Z_DATA_ERROR;
			if (ret == Z_MEM_ERROR || ret == Z_DATA_ERROR)
				goto extract_ret;
			if (ret == Z_STREAM_END)
				break;
		} while (state->strm.avail_out != 0);

		/* if reach end of stream, then don't keep trying to get more */
		if (ret == Z_STREAM_END)
			break;

		/* do until offset reached and requested data read, or stream ends */
	} while (skip);

	isEnd = ret == Z_STREAM_END;
	/* compute number of uncompressed bytes read after offset */
	ret = skip ? 0 : len - state->strm.avail_out;

	/* clean up and return bytes read or error */
extract_ret:
	if (ret == len && !isEnd)
	{
		state->out_offset += len;
		state->isValid = 1;
	}
	else
		inflateEnd(&state->strm);

	return ret;
}

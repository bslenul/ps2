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

#include "PrecompiledHeader.h"
#include "GSTexture12.h"
#include "GSDevice12.h"
#include "common/Assertions.h"
#include "common/Align.h"
#include "common/D3D12/Builders.h"
#include "common/D3D12/Context.h"
#include "common/D3D12/Util.h"
#include "common/StringUtil.h"
#include "D3D12MemAlloc.h"
#include "GS/GSPerfMon.h"

GSTexture12::GSTexture12(Type type, Format format, D3D12::Texture texture)
	: m_texture(std::move(texture))
{
	m_type = type;
	m_format = format;
	m_size.x = m_texture.GetWidth();
	m_size.y = m_texture.GetHeight();
	m_mipmap_levels = m_texture.GetLevels();
}

GSTexture12::~GSTexture12()
{
	GSDevice12::GetInstance()->UnbindTexture(this);
}

std::unique_ptr<GSTexture12> GSTexture12::Create(Type type, u32 width, u32 height, u32 levels, Format format,
	DXGI_FORMAT d3d_format, DXGI_FORMAT srv_format, DXGI_FORMAT rtv_format, DXGI_FORMAT dsv_format)
{
	switch (type)
	{
		case Type::Texture:
		{
			// This is a little annoying. basically, to do mipmap generation, we need to be a render target.
			// If it's a compressed texture, we won't be generating mips anyway, so this should be fine.
			const D3D12_RESOURCE_FLAGS flags = (levels > 1 && !IsCompressedFormat(format)) ?
												   D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET :
                                                   D3D12_RESOURCE_FLAG_NONE;

			D3D12::Texture texture;
			if (!texture.Create(width, height, levels, d3d_format, srv_format,
					DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, flags))
			{
				return {};
			}

			D3D12::SetObjectNameFormatted(texture.GetResource(), "%ux%u texture", width, height);
			return std::make_unique<GSTexture12>(type, format, std::move(texture));
		}

		case Type::RenderTarget:
		{
			pxAssert(levels == 1);

			// RT's tend to be larger, so we'll keep them committed for speed.
			D3D12::Texture texture;
			if (!texture.Create(width, height, levels, d3d_format, srv_format, rtv_format,
					DXGI_FORMAT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12MA::ALLOCATION_FLAG_COMMITTED))
			{
				return {};
			}

			D3D12::SetObjectNameFormatted(texture.GetResource(), "%ux%u render target", width, height);
			return std::make_unique<GSTexture12>(type, format, std::move(texture));
		}

		case Type::DepthStencil:
		{
			pxAssert(levels == 1);

			D3D12::Texture texture;
			if (!texture.Create(width, height, levels, d3d_format, srv_format,
					DXGI_FORMAT_UNKNOWN, dsv_format, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
					D3D12MA::ALLOCATION_FLAG_COMMITTED))
			{
				return {};
			}

			D3D12::SetObjectNameFormatted(texture.GetResource(), "%ux%u depth stencil", width, height);
			return std::make_unique<GSTexture12>(type, format, std::move(texture));
		}

		case Type::RWTexture:
		{
			pxAssert(levels == 1);

			D3D12::Texture texture;
			if (!texture.Create(width, height, levels, d3d_format, srv_format, DXGI_FORMAT_UNKNOWN,
					DXGI_FORMAT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12MA::ALLOCATION_FLAG_COMMITTED))
			{
				return {};
			}

			D3D12::SetObjectNameFormatted(texture.GetResource(), "%ux%u RW texture", width, height);
			return std::make_unique<GSTexture12>(type, format, std::move(texture));
		}

		default:
			return {};
	}
}

void* GSTexture12::GetNativeHandle() const { return const_cast<D3D12::Texture*>(&m_texture); }

ID3D12GraphicsCommandList* GSTexture12::GetCommandBufferForUpdate()
{
	if (m_type != Type::Texture || m_use_fence_counter == g_d3d12_context->GetCurrentFenceValue())
	{
		// Console.WriteLn("Texture update within frame, can't use do beforehand");
		GSDevice12::GetInstance()->EndRenderPass();
		return g_d3d12_context->GetCommandList();
	}

	return g_d3d12_context->GetInitCommandList();
}

ID3D12Resource* GSTexture12::AllocateUploadStagingBuffer(const void* data, u32 pitch, u32 upload_pitch, u32 height) const
{
	const u32 buffer_size = CalcUploadSize(height, upload_pitch);
	wil::com_ptr_nothrow<ID3D12Resource> resource;
	wil::com_ptr_nothrow<D3D12MA::Allocation> allocation;

	const D3D12MA::ALLOCATION_DESC allocation_desc = {D3D12MA::ALLOCATION_FLAG_NONE, D3D12_HEAP_TYPE_UPLOAD};
	const D3D12_RESOURCE_DESC resource_desc = {
		D3D12_RESOURCE_DIMENSION_BUFFER, 0, buffer_size, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
		D3D12_RESOURCE_FLAG_NONE};
	HRESULT hr = g_d3d12_context->GetAllocator()->CreateResource(&allocation_desc, &resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr, allocation.put(), IID_PPV_ARGS(resource.put()));
	if (FAILED(hr))
	{
		Console.WriteLn("(AllocateUploadStagingBuffer) CreateCommittedResource() failed with %08X", hr);
		return nullptr;
	}

	void* map_ptr;
	hr = resource->Map(0, nullptr, &map_ptr);
	if (FAILED(hr))
	{
		Console.WriteLn("(AllocateUploadStagingBuffer) Map() failed with %08X", hr);
		return nullptr;
	}

	CopyTextureDataForUpload(map_ptr, data, pitch, upload_pitch, height);

	const D3D12_RANGE write_range = {0, buffer_size};
	resource->Unmap(0, &write_range);

	// Immediately queue it for freeing after the command buffer finishes, since it's only needed for the copy.
	// This adds the reference needed to keep the buffer alive.
	g_d3d12_context->DeferResourceDestruction(allocation.get(), resource.get());
	return resource.get();
}

void GSTexture12::CopyTextureDataForUpload(void* dst, const void* src, u32 pitch, u32 upload_pitch, u32 height) const
{
	const u32 block_size = GetCompressedBlockSize();
	const u32 count = (height + (block_size - 1)) / block_size;
	StringUtil::StrideMemCpy(dst, upload_pitch, src, pitch, std::min(upload_pitch, pitch), count);
}

bool GSTexture12::Update(const GSVector4i& r, const void* data, int pitch, int layer)
{
	if (layer >= m_mipmap_levels)
		return false;

	// Footprint and box must be block aligned for compressed textures.
	const u32 block_size = GetCompressedBlockSize();
	const u32 width = Common::AlignUpPow2(r.width(), block_size);
	const u32 height = Common::AlignUpPow2(r.height(), block_size);
	const u32 upload_pitch = Common::AlignUpPow2<u32>(pitch, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
	const u32 required_size = CalcUploadSize(r.height(), upload_pitch);

	D3D12_TEXTURE_COPY_LOCATION srcloc;
	srcloc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	srcloc.PlacedFootprint.Footprint.Width = width;
	srcloc.PlacedFootprint.Footprint.Height = height;
	srcloc.PlacedFootprint.Footprint.Depth = 1;
	srcloc.PlacedFootprint.Footprint.Format = m_texture.GetFormat();
	srcloc.PlacedFootprint.Footprint.RowPitch = upload_pitch;

	// If the texture is larger than half our streaming buffer size, use a separate buffer.
	// Otherwise allocation will either fail, or require lots of cmdbuffer submissions.
	if (required_size > (g_d3d12_context->GetTextureStreamBuffer().GetSize() / 2))
	{
		srcloc.pResource = AllocateUploadStagingBuffer(data, pitch, upload_pitch, height);
		if (!srcloc.pResource)
			return false;

		srcloc.PlacedFootprint.Offset = 0;
	}
	else
	{
		D3D12::StreamBuffer& sbuffer = g_d3d12_context->GetTextureStreamBuffer();
		if (!sbuffer.ReserveMemory(required_size, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT))
		{
			GSDevice12::GetInstance()->ExecuteCommandList(
				false, "While waiting for %u bytes in texture upload buffer", required_size);
			if (!sbuffer.ReserveMemory(required_size, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT))
			{
				Console.Error("Failed to reserve texture upload memory (%u bytes).", required_size);
				return false;
			}
		}

		srcloc.pResource = sbuffer.GetBuffer();
		srcloc.PlacedFootprint.Offset = sbuffer.GetCurrentOffset();
		CopyTextureDataForUpload(sbuffer.GetCurrentHostPointer(), data, pitch, upload_pitch, height);
		sbuffer.CommitMemory(required_size);
	}

	ID3D12GraphicsCommandList* cmdlist = GetCommandBufferForUpdate();

	// first time the texture is used? don't leave it undefined
	if (m_texture.GetState() == D3D12_RESOURCE_STATE_COMMON)
		m_texture.TransitionToState(cmdlist, D3D12_RESOURCE_STATE_COPY_DEST);
	else if (m_texture.GetState() != D3D12_RESOURCE_STATE_COPY_DEST)
		m_texture.TransitionSubresourceToState(cmdlist, layer, m_texture.GetState(), D3D12_RESOURCE_STATE_COPY_DEST);

	// if we're an rt and have been cleared, and the full rect isn't being uploaded, do the clear
	if (m_type == Type::RenderTarget)
	{
		if (!r.eq(GSVector4i(0, 0, m_size.x, m_size.y)))
			CommitClear(cmdlist);
		else
			m_state = State::Dirty;
	}

	D3D12_TEXTURE_COPY_LOCATION dstloc;
	dstloc.pResource = m_texture.GetResource();
	dstloc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dstloc.SubresourceIndex = layer;

	const D3D12_BOX srcbox{0u, 0u, 0u, width, height, 1u};
	cmdlist->CopyTextureRegion(&dstloc, Common::AlignDownPow2((u32)r.x, block_size),
		Common::AlignDownPow2((u32)r.y, block_size), 0, &srcloc, &srcbox);

	if (m_texture.GetState() != D3D12_RESOURCE_STATE_COPY_DEST)
		m_texture.TransitionSubresourceToState(cmdlist, layer, D3D12_RESOURCE_STATE_COPY_DEST, m_texture.GetState());

	if (m_type == Type::Texture)
		m_needs_mipmaps_generated |= (layer == 0);

	return true;
}

bool GSTexture12::Map(GSMap& m, const GSVector4i* r, int layer)
{
	if (layer >= m_mipmap_levels || IsCompressedFormat())
		return false;

	// map for writing
	m_map_area = r ? *r : GSVector4i(0, 0, m_texture.GetWidth(), m_texture.GetHeight());
	m_map_level = layer;

	m.pitch = Common::AlignUpPow2(m_map_area.width() * D3D12::GetTexelSize(m_texture.GetFormat()), D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);

	// see note in Update() for the reason why.
	const u32 required_size = CalcUploadSize(m_map_area.height(), m.pitch);
	D3D12::StreamBuffer& buffer = g_d3d12_context->GetTextureStreamBuffer();
	if (required_size >= (buffer.GetSize() / 2))
		return false;

	if (!buffer.ReserveMemory(required_size, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT))
	{
		GSDevice12::GetInstance()->ExecuteCommandList(
			false, "While waiting for %u bytes in texture upload buffer", required_size);
		if (!buffer.ReserveMemory(required_size, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT))
			Console.Error("Failed to reserve texture upload memory");
	}

	m.bits = static_cast<u8*>(buffer.GetCurrentHostPointer());
	return true;
}

void GSTexture12::Unmap()
{
	// this can't handle blocks/compressed formats at the moment.
	pxAssert(m_map_level < m_texture.GetLevels() && !IsCompressedFormat());

	// TODO: non-tightly-packed formats
	const u32 width = static_cast<u32>(m_map_area.width());
	const u32 height = static_cast<u32>(m_map_area.height());
	const u32 pitch = Common::AlignUpPow2(m_map_area.width() * D3D12::GetTexelSize(m_texture.GetFormat()), D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
	const u32 required_size = CalcUploadSize(height, pitch);
	D3D12::StreamBuffer& buffer = g_d3d12_context->GetTextureStreamBuffer();
	const u32 buffer_offset = buffer.GetCurrentOffset();
	buffer.CommitMemory(required_size);

	ID3D12GraphicsCommandList* cmdlist = GetCommandBufferForUpdate();

	// first time the texture is used? don't leave it undefined
	if (m_texture.GetState() == D3D12_RESOURCE_STATE_COMMON)
		m_texture.TransitionToState(cmdlist, D3D12_RESOURCE_STATE_COPY_DEST);
	else if (m_texture.GetState() != D3D12_RESOURCE_STATE_COPY_DEST)
		m_texture.TransitionSubresourceToState(cmdlist, m_map_level, m_texture.GetState(), D3D12_RESOURCE_STATE_COPY_DEST);

	// if we're an rt and have been cleared, and the full rect isn't being uploaded, do the clear
	if (m_type == Type::RenderTarget)
	{
		if (!m_map_area.eq(GSVector4i(0, 0, m_size.x, m_size.y)))
			CommitClear(cmdlist);
		else
			m_state = State::Dirty;
	}

	D3D12_TEXTURE_COPY_LOCATION srcloc;
	srcloc.pResource = buffer.GetBuffer();
	srcloc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	srcloc.PlacedFootprint.Offset = buffer_offset;
	srcloc.PlacedFootprint.Footprint.Width = width;
	srcloc.PlacedFootprint.Footprint.Height = height;
	srcloc.PlacedFootprint.Footprint.Depth = 1;
	srcloc.PlacedFootprint.Footprint.Format = m_texture.GetFormat();
	srcloc.PlacedFootprint.Footprint.RowPitch = pitch;

	D3D12_TEXTURE_COPY_LOCATION dstloc;
	dstloc.pResource = m_texture.GetResource();
	dstloc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dstloc.SubresourceIndex = m_map_level;

	const D3D12_BOX srcbox{0u, 0u, 0u, width, height, 1};
	cmdlist->CopyTextureRegion(&dstloc, m_map_area.x, m_map_area.y, 0, &srcloc, &srcbox);

	if (m_texture.GetState() != D3D12_RESOURCE_STATE_COPY_DEST)
		m_texture.TransitionSubresourceToState(cmdlist, m_map_level, D3D12_RESOURCE_STATE_COPY_DEST, m_texture.GetState());

	if (m_type == Type::Texture)
		m_needs_mipmaps_generated |= (m_map_level == 0);
}

void GSTexture12::GenerateMipmap()
{
	pxAssert(!IsCompressedFormat(m_format));

	for (int dst_level = 1; dst_level < m_mipmap_levels; dst_level++)
	{
		const int src_level = dst_level - 1;
		const int src_width = std::max<int>(m_size.x >> src_level, 1);
		const int src_height = std::max<int>(m_size.y >> src_level, 1);
		const int dst_width = std::max<int>(m_size.x >> dst_level, 1);
		const int dst_height = std::max<int>(m_size.y >> dst_level, 1);

		GSDevice12::GetInstance()->RenderTextureMipmap(m_texture,
			dst_level, dst_width, dst_height, src_level, src_width, src_height);
	}

	SetUsedThisCommandBuffer();
}

void GSTexture12::Swap(GSTexture* tex)
{
	GSTexture::Swap(tex);
	std::swap(m_texture, static_cast<GSTexture12*>(tex)->m_texture);
	std::swap(m_use_fence_counter, static_cast<GSTexture12*>(tex)->m_use_fence_counter);
	std::swap(m_clear_value, static_cast<GSTexture12*>(tex)->m_clear_value);
	std::swap(m_map_area, static_cast<GSTexture12*>(tex)->m_map_area);
	std::swap(m_map_level, static_cast<GSTexture12*>(tex)->m_map_level);
}

void GSTexture12::TransitionToState(D3D12_RESOURCE_STATES state)
{
	m_texture.TransitionToState(g_d3d12_context->GetCommandList(), state);
}

void GSTexture12::CommitClear()
{
	if (m_state != GSTexture::State::Cleared)
		return;

	GSDevice12::GetInstance()->EndRenderPass();

	CommitClear(g_d3d12_context->GetCommandList());
}

void GSTexture12::CommitClear(ID3D12GraphicsCommandList* cmdlist)
{
	if (IsDepthStencil())
	{
		m_texture.TransitionToState(cmdlist, D3D12_RESOURCE_STATE_DEPTH_WRITE);
		cmdlist->ClearDepthStencilView(m_texture.GetWriteDescriptor(), D3D12_CLEAR_FLAG_DEPTH, m_clear_value.depth, 0, 0, nullptr);
	}
	else
	{
		m_texture.TransitionToState(cmdlist, D3D12_RESOURCE_STATE_RENDER_TARGET);
		cmdlist->ClearRenderTargetView(m_texture.GetWriteDescriptor(), m_clear_value.color, 0, nullptr);
	}

	SetState(GSTexture::State::Dirty);
}

GSDownloadTexture12::GSDownloadTexture12(u32 width, u32 height, GSTexture::Format format)
	: GSDownloadTexture(width, height, format)
{
}

GSDownloadTexture12::~GSDownloadTexture12()
{
	if (IsMapped())
		GSDownloadTexture12::Unmap();

	if (m_buffer)
		g_d3d12_context->DeferResourceDestruction(m_allocation.get(), m_buffer.get());
}

std::unique_ptr<GSDownloadTexture12> GSDownloadTexture12::Create(u32 width, u32 height, GSTexture::Format format)
{
	const u32 buffer_size = GetBufferSize(width, height, format, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);

	D3D12MA::ALLOCATION_DESC allocation_desc = {};
	allocation_desc.HeapType = D3D12_HEAP_TYPE_READBACK;

	const D3D12_RESOURCE_DESC resource_desc = {D3D12_RESOURCE_DIMENSION_BUFFER, 0, buffer_size, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0},
		D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE};

	wil::com_ptr_nothrow<D3D12MA::Allocation> allocation;
	wil::com_ptr_nothrow<ID3D12Resource> buffer;

	HRESULT hr = g_d3d12_context->GetAllocator()->CreateResource(
		&allocation_desc, &resource_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, allocation.put(), IID_PPV_ARGS(buffer.put()));
	if (FAILED(hr))
	{
		Console.Error("(GSDownloadTexture12::Create) CreateResource() failed with HRESULT %08X", hr);
		return {};
	}

	std::unique_ptr<GSDownloadTexture12> tex(new GSDownloadTexture12(width, height, format));
	tex->m_allocation = std::move(allocation);
	tex->m_buffer = std::move(buffer);
	tex->m_buffer_size = buffer_size;
	return tex;
}

void GSDownloadTexture12::CopyFromTexture(
	const GSVector4i& drc, GSTexture* stex, const GSVector4i& src, u32 src_level, bool use_transfer_pitch)
{
	GSTexture12* const tex12 = static_cast<GSTexture12*>(stex);

	pxAssert(tex12->GetFormat() == m_format);
	pxAssert(drc.width() == src.width() && drc.height() == src.height());
	pxAssert(src.z <= tex12->GetWidth() && src.w <= tex12->GetHeight());
	pxAssert(static_cast<u32>(drc.z) <= m_width && static_cast<u32>(drc.w) <= m_height);
	pxAssert(src_level < static_cast<u32>(tex12->GetMipmapLevels()));
	pxAssert((drc.left == 0 && drc.top == 0) || !use_transfer_pitch);

	u32 copy_offset, copy_size, copy_rows;
	m_current_pitch = GetTransferPitch(use_transfer_pitch ? static_cast<u32>(drc.width()) : m_width, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
	GetTransferSize(drc, &copy_offset, &copy_size, &copy_rows);

	GSDevice12::GetInstance()->EndRenderPass();
	tex12->CommitClear();

	if (IsMapped())
		Unmap();

	ID3D12GraphicsCommandList* cmdlist = g_d3d12_context->GetCommandList();

	D3D12_TEXTURE_COPY_LOCATION srcloc;
	srcloc.pResource = tex12->GetResource();
	srcloc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	srcloc.SubresourceIndex = src_level;

	D3D12_TEXTURE_COPY_LOCATION dstloc;
	dstloc.pResource = m_buffer.get();
	dstloc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	dstloc.PlacedFootprint.Offset = copy_offset;
	dstloc.PlacedFootprint.Footprint.Format = tex12->GetNativeFormat();
	dstloc.PlacedFootprint.Footprint.Width = drc.width();
	dstloc.PlacedFootprint.Footprint.Height = drc.height();
	dstloc.PlacedFootprint.Footprint.Depth = 1;
	dstloc.PlacedFootprint.Footprint.RowPitch = m_current_pitch;

	const D3D12_RESOURCE_STATES old_layout = tex12->GetResourceState();
	if (old_layout != D3D12_RESOURCE_STATE_COPY_SOURCE)
		tex12->GetTexture().TransitionSubresourceToState(cmdlist, src_level, old_layout, D3D12_RESOURCE_STATE_COPY_SOURCE);

	// TODO: Rules for depth buffers here?
	const D3D12_BOX srcbox{
		static_cast<UINT>(src.left), static_cast<UINT>(src.top), 0u, static_cast<UINT>(src.right), static_cast<UINT>(src.bottom), 1u};
	cmdlist->CopyTextureRegion(&dstloc, 0, 0, 0, &srcloc, &srcbox);

	if (old_layout != D3D12_RESOURCE_STATE_COPY_SOURCE)
		tex12->GetTexture().TransitionSubresourceToState(cmdlist, src_level, D3D12_RESOURCE_STATE_COPY_SOURCE, old_layout);

	m_copy_fence_value = g_d3d12_context->GetCurrentFenceValue();
	m_needs_flush = true;
}

bool GSDownloadTexture12::Map(const GSVector4i& read_rc)
{
	if (IsMapped())
		return true;

	// Never populated?
	if (!m_current_pitch)
		return false;

	u32 copy_offset, copy_size, copy_rows;
	GetTransferSize(read_rc, &copy_offset, &copy_size, &copy_rows);

	const D3D12_RANGE read_range{copy_offset, copy_offset + copy_size};
	const HRESULT hr = m_buffer->Map(0, &read_range, reinterpret_cast<void**>(const_cast<u8**>(&m_map_pointer)));
	if (FAILED(hr))
	{
		Console.Error("(GSDownloadTexture12::Map) Map() failed with HRESULT %08X", hr);
		return false;
	}

	return true;
}

void GSDownloadTexture12::Unmap()
{
	if (!IsMapped())
		return;

	const D3D12_RANGE write_range = {};
	m_buffer->Unmap(0, &write_range);
	m_map_pointer = nullptr;
}

void GSDownloadTexture12::Flush()
{
	if (!m_needs_flush)
		return;

	m_needs_flush = false;

	if (g_d3d12_context->GetCompletedFenceValue() >= m_copy_fence_value)
		return;

	// Need to execute command buffer.
	if (g_d3d12_context->GetCurrentFenceValue() == m_copy_fence_value)
		GSDevice12::GetInstance()->ExecuteCommandListForReadback();
	else
		g_d3d12_context->WaitForFence(m_copy_fence_value, GSConfig.HWSpinGPUForReadbacks);
}

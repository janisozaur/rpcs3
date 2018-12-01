﻿#include "stdafx.h"
#include "Emu/Memory/vm.h"
#include "Emu/System.h"
#include "Emu/Cell/PPUModule.h"
#include "Emu/RSX/GSRender.h"
#include "Emu/IdManager.h"
#include "Emu/Cell/ErrorCodes.h"
#include "sys_rsx.h"
#include "sys_event.h"


LOG_CHANNEL(sys_rsx);

extern u64 get_timebased_time();

u64 rsxTimeStamp()
{
	return get_timebased_time();
}

s32 sys_rsx_device_open()
{
	sys_rsx.todo("sys_rsx_device_open()");

	return CELL_OK;
}

s32 sys_rsx_device_close()
{
	sys_rsx.todo("sys_rsx_device_close()");

	return CELL_OK;
}

/*
 * lv2 SysCall 668 (0x29C): sys_rsx_memory_allocate
 * @param mem_handle (OUT): Context / ID, which is used by sys_rsx_memory_free to free allocated memory.
 * @param mem_addr (OUT): Returns the local memory base address, usually 0xC0000000.
 * @param size (IN): Local memory size. E.g. 0x0F900000 (249 MB).
 * @param flags (IN): E.g. Immediate value passed in cellGcmSys is 8.
 * @param a5 (IN): E.g. Immediate value passed in cellGcmSys is 0x00300000 (3 MB?).
 * @param a6 (IN): E.g. Immediate value passed in cellGcmSys is 16.
 * @param a7 (IN): E.g. Immediate value passed in cellGcmSys is 8.
 */
s32 sys_rsx_memory_allocate(vm::ptr<u32> mem_handle, vm::ptr<u64> mem_addr, u32 size, u64 flags, u64 a5, u64 a6, u64 a7)
{
	sys_rsx.warning("sys_rsx_memory_allocate(mem_handle=*0x%x, mem_addr=*0x%x, size=0x%x, flags=0x%llx, a5=0x%llx, a6=0x%llx, a7=0x%llx)", mem_handle, mem_addr, size, flags, a5, a6, a7);

	*mem_handle = 0x5a5a5a5b;
	*mem_addr = vm::falloc(0xC0000000, size, vm::video);

	return CELL_OK;
}

/*
 * lv2 SysCall 669 (0x29D): sys_rsx_memory_free
 * @param mem_handle (OUT): Context / ID, for allocated local memory generated by sys_rsx_memory_allocate
 */
s32 sys_rsx_memory_free(u32 mem_handle)
{
	sys_rsx.todo("sys_rsx_memory_free(mem_handle=0x%x)", mem_handle);

	return CELL_OK;
}

/*
 * lv2 SysCall 670 (0x29E): sys_rsx_context_allocate
 * @param context_id (OUT): RSX context, E.g. 0x55555555 (in vsh.self)
 * @param lpar_dma_control (OUT): Control register area. E.g. 0x60100000 (in vsh.self)
 * @param lpar_driver_info (OUT): RSX data like frequencies, sizes, version... E.g. 0x60200000 (in vsh.self)
 * @param lpar_reports (OUT): Report data area. E.g. 0x60300000 (in vsh.self)
 * @param mem_ctx (IN): mem_ctx given by sys_rsx_memory_allocate
 * @param system_mode (IN):
 */
s32 sys_rsx_context_allocate(vm::ptr<u32> context_id, vm::ptr<u64> lpar_dma_control, vm::ptr<u64> lpar_driver_info, vm::ptr<u64> lpar_reports, u64 mem_ctx, u64 system_mode)
{
	sys_rsx.warning("sys_rsx_context_allocate(context_id=*0x%x, lpar_dma_control=*0x%x, lpar_driver_info=*0x%x, lpar_reports=*0x%x, mem_ctx=0x%llx, system_mode=0x%llx)",
		context_id, lpar_dma_control, lpar_driver_info, lpar_reports, mem_ctx, system_mode);

	auto m_sysrsx = fxm::get<SysRsxConfig>();

	if (!m_sysrsx) // TODO: check if called twice
		return CELL_EINVAL;

	*context_id = 0x55555555;

	*lpar_dma_control = m_sysrsx->rsx_context_addr + 0x100000;
	*lpar_driver_info = m_sysrsx->rsx_context_addr + 0x200000;
	*lpar_reports = m_sysrsx->rsx_context_addr + 0x300000;

	auto &reports = vm::_ref<RsxReports>(*lpar_reports);
	std::memset(&reports, 0, sizeof(RsxReports));

	for (int i = 0; i < 64; ++i)
		reports.notify[i].timestamp = (u64)-1;

	for (int i = 0; i < 256; ++i) {
		reports.semaphore[i].val = 0x1337C0D3;
		reports.semaphore[i].pad = 0x1337BABE;
		reports.semaphore[i].timestamp = (u64)-1; // technically different but should be fine
	}

	for (int i = 0; i < 2048; ++i)
		reports.report[i].timestamp = (u64)-1;

	auto &driverInfo = vm::_ref<RsxDriverInfo>(*lpar_driver_info);

	std::memset(&driverInfo, 0, sizeof(RsxDriverInfo));

	driverInfo.version_driver = 0x211;
	driverInfo.version_gpu = 0x5c;
	driverInfo.memory_size = 0xFE00000;
	driverInfo.nvcore_frequency = 500000000; // 0x1DCD6500
	driverInfo.memory_frequency = 650000000; // 0x26BE3680
	driverInfo.reportsNotifyOffset = 0x1000;
	driverInfo.reportsOffset = 0;
	driverInfo.reportsReportOffset = 0x1400;
	driverInfo.systemModeFlags = system_mode;
	driverInfo.hardware_channel = 1; // * i think* this 1 for games, 0 for vsh

	m_sysrsx->driverInfo = *lpar_driver_info;

	auto &dmaControl = vm::_ref<RsxDmaControl>(*lpar_dma_control);
	dmaControl.get = 0;
	dmaControl.put = 0;
	dmaControl.ref = 0; // Set later to -1 by cellGcmSys

	memset(&RSXIOMem, 0xFF, sizeof(RSXIOMem));

	if (false/*system_mode == CELL_GCM_SYSTEM_MODE_IOMAP_512MB*/)
		rsx::get_current_renderer()->main_mem_size = 0x20000000; //512MB
	else
		rsx::get_current_renderer()->main_mem_size = 0x10000000; //256MB

	vm::var<sys_event_queue_attribute_t, vm::page_allocator<>> attr;
	attr->protocol = SYS_SYNC_PRIORITY;
	attr->type = SYS_PPU_QUEUE;
	attr->name_u64 = 0;

	sys_event_port_create(vm::get_addr(&driverInfo.handler_queue), SYS_EVENT_PORT_LOCAL, 0);
	m_sysrsx->rsx_event_port = driverInfo.handler_queue;
	sys_event_queue_create(vm::get_addr(&driverInfo.handler_queue), attr, 0, 0x20);
	sys_event_port_connect_local(m_sysrsx->rsx_event_port, driverInfo.handler_queue);

	const auto render = rsx::get_current_renderer();
	render->display_buffers_count = 0;
	render->current_display_buffer = 0;
	render->main_mem_addr = 0;
	render->label_addr = *lpar_reports;
	render->ctxt_addr = m_sysrsx->rsx_context_addr;
	render->init(0, 0, *lpar_dma_control, 0xC0000000);

	return CELL_OK;
}

/*
 * lv2 SysCall 671 (0x29F): sys_rsx_context_free
 * @param context_id (IN): RSX context generated by sys_rsx_context_allocate to free the context.
 */
s32 sys_rsx_context_free(u32 context_id)
{
	sys_rsx.todo("sys_rsx_context_free(context_id=0x%x)", context_id);

	return CELL_OK;
}

/*
 * lv2 SysCall 672 (0x2A0): sys_rsx_context_iomap
 * @param context_id (IN): RSX context, E.g. 0x55555555 (in vsh.self)
 * @param io (IN): IO offset mapping area. E.g. 0x00600000
 * @param ea (IN): Start address of mapping area. E.g. 0x20400000
 * @param size (IN): Size of mapping area in bytes. E.g. 0x00200000
 * @param flags (IN):
 */
s32 sys_rsx_context_iomap(u32 context_id, u32 io, u32 ea, u32 size, u64 flags)
{
	sys_rsx.warning("sys_rsx_context_iomap(context_id=0x%x, io=0x%x, ea=0x%x, size=0x%x, flags=0x%llx)", context_id, io, ea, size, flags);

	if (!size || io & 0xFFFFF || ea & 0xFFFFF || size & 0xFFFFF ||
		rsx::get_current_renderer()->main_mem_size < io + size)
	{
		return CELL_EINVAL;
	}

	io >>= 20, ea >>= 20, size >>= 20;

	for (u32 i = 0; i < size; i++)
	{
		RSXIOMem.io[ea + i] = io + i;
		RSXIOMem.ea[io + i] = ea + i;
	}

	return CELL_OK;
}

/*
 * lv2 SysCall 673 (0x2A1): sys_rsx_context_iounmap
 * @param context_id (IN): RSX context, E.g. 0x55555555 (in vsh.self)
 * @param io (IN): IO address. E.g. 0x00600000 (Start page 6)
 * @param size (IN): Size to unmap in byte. E.g. 0x00200000
 */
s32 sys_rsx_context_iounmap(u32 context_id, u32 io, u32 size)
{
	sys_rsx.warning("sys_rsx_context_iounmap(context_id=0x%x, io=0x%x, size=0x%x)", context_id, io, size);

	if (!size || rsx::get_current_renderer()->main_mem_size < io + size)
	{
		return CELL_EINVAL;
	}

	const u32 end = (io >>= 20) + (size >>= 20);
	for (u32 ea = RSXIOMem.ea[io]; io < end;)
	{
		RSXIOMem.io[ea++] = 0xFFFF;
		RSXIOMem.ea[io++] = 0xFFFF;
	}

	return CELL_OK;
}

/*
 * lv2 SysCall 674 (0x2A2): sys_rsx_context_attribute
 * @param context_id (IN): RSX context, e.g. 0x55555555
 * @param package_id (IN):
 * @param a3 (IN):
 * @param a4 (IN):
 * @param a5 (IN):
 * @param a6 (IN):
 */
s32 sys_rsx_context_attribute(s32 context_id, u32 package_id, u64 a3, u64 a4, u64 a5, u64 a6)
{
	// Flip/queue/reset flip/flip event/user command/vblank as trace to help with log spam
	if (package_id == 0x102 || package_id == 0x103 || package_id == 0x10a || package_id == 0xFEC || package_id == 0xFED || package_id == 0xFEF)
		sys_rsx.trace("sys_rsx_context_attribute(context_id=0x%x, package_id=0x%x, a3=0x%llx, a4=0x%llx, a5=0x%llx, a6=0x%llx)", context_id, package_id, a3, a4, a5, a6);
	else
		sys_rsx.warning("sys_rsx_context_attribute(context_id=0x%x, package_id=0x%x, a3=0x%llx, a4=0x%llx, a5=0x%llx, a6=0x%llx)", context_id, package_id, a3, a4, a5, a6);

	// todo: these event ports probly 'shouldnt' be here as i think its supposed to be interrupts that are sent from rsx somewhere in lv1

	const auto render = rsx::get_current_renderer();

	//hle protection
	if (render->isHLE)
		return 0;

	auto m_sysrsx = fxm::get<SysRsxConfig>();

	if (!m_sysrsx)
	{
		sys_rsx.error("sys_rsx_context_attribute called before sys_rsx_context_allocate: context_id=0x%x, package_id=0x%x, a3=0x%llx, a4=0x%llx, a5=0x%llx, a6=0x%llx)", context_id, package_id, a3, a4, a5, a6);
		return CELL_EINVAL;
	}

	auto &driverInfo = vm::_ref<RsxDriverInfo>(m_sysrsx->driverInfo);
	switch (package_id)
	{
	case 0x001: // FIFO
		render->pause();
		render->ctrl->get = a3;
		render->ctrl->put = a4;
		render->restore_point = a3;
		render->unpause();
		break;

	case 0x100: // Display mode set
		break;
	case 0x101: // Display sync set, cellGcmSetFlipMode
		// a4 == 2 is vsync, a4 == 1 is hsync
		render->requested_vsync.store(a4 == 2);
		break;

	case 0x102: // Display flip
	{
		u32 flip_idx = -1u;

		// high bit signifys grabbing a queued buffer
		// otherwise it contains a display buffer offset
		if ((a4 & 0x80000000) != 0)
		{
			// last half byte gives buffer, 0xf seems to trigger just last queued
			u8 idx_check = a4 & 0xf;
			if (idx_check > 7)
				flip_idx = driverInfo.head[a3].lastQueuedBufferId;
			else
				flip_idx = idx_check;

			// fyi -- u32 hardware_channel = (a4 >> 8) & 0xFF;

			// sanity check, the head should have a 'queued' buffer on it, and it should have been previously 'queued'
			u32 sanity_check = 0x40000000 & (1 << flip_idx);
			if ((driverInfo.head[a3].flipFlags & sanity_check) != sanity_check)
				LOG_ERROR(RSX, "Display Flip Queued: Flipping non previously queued buffer 0x%x", a4);
		}
		else
		{
			for (u32 i = 0; i < render->display_buffers_count; ++i)
			{
				if (render->display_buffers[i].offset == a4)
				{
					flip_idx = i;
					break;
				}
			}
			if (flip_idx == -1u)
			{
				LOG_ERROR(RSX, "Display Flip: Couldn't find display buffer offset, flipping 0. Offset: 0x%x", a4);
				flip_idx = 0;
			}
		}

		render->request_emu_flip(flip_idx);
	}
	break;

	case 0x103: // Display Queue
		driverInfo.head[a3].lastQueuedBufferId = a4;
		driverInfo.head[a3].flipFlags |= 0x40000000 | (1 << a4);
		if (a3 == 0)
			sys_event_port_send(m_sysrsx->rsx_event_port, 0, (1 << 5), 0);
		if (a3 == 1)
			sys_event_port_send(m_sysrsx->rsx_event_port, 0, (1 << 6), 0);
		break;

	case 0x104: // Display buffer
	{
		u8 id = a3 & 0xFF;
		u32 width = (a4 >> 32) & 0xFFFFFFFF;
		u32 height = a4 & 0xFFFFFFFF;
		u32 pitch = (a5 >> 32) & 0xFFFFFFFF;
		u32 offset = a5 & 0xFFFFFFFF;
		if (id > 7)
			return -17;
		render->display_buffers[id].width = width;
		render->display_buffers[id].height = height;
		render->display_buffers[id].pitch = pitch;
		render->display_buffers[id].offset = offset;

		render->display_buffers_count = std::max((u32)id + 1, render->display_buffers_count);
	}
	break;

	case 0x105: // destroy buffer?
		break;

	case 0x106: // ? (Used by cellGcmInitPerfMon)
		break;

	case 0x108: // cellGcmSetSecondVFrequency
		// a4 == 3, CELL_GCM_DISPLAY_FREQUENCY_59_94HZ
		// a4 == 2, CELL_GCM_DISPLAY_FREQUENCY_SCANOUT
		// a4 == 4, CELL_GCM_DISPLAY_FREQUENCY_DISABLE
		// Note: Scanout/59_94 is ignored currently as we report refresh rate of 59_94hz as it is, so the difference doesnt matter
		render->enable_second_vhandler.store(a4 != 4);
		break;

	case 0x10a: // ? Involved in managing flip status through cellGcmResetFlipStatus
	{
		if (a3 > 7)
			return -17;

		u32 flipStatus = driverInfo.head[a3].flipFlags;
		flipStatus = (flipStatus & a4) | a5;
		driverInfo.head[a3].flipFlags = flipStatus;
	}
	break;

	case 0x10D: // Called by cellGcmInitCursor
		break;

	case 0x300: // Tiles
	{
		//a4 high bits = ret.tile = (location + 1) | (bank << 4) | ((offset / 0x10000) << 16) | (location << 31);
		//a4 low bits = ret.limit = ((offset + size - 1) / 0x10000) << 16 | (location << 31);
		//a5 high bits = ret.pitch = (pitch / 0x100) << 8;
		//a5 low bits = ret.format = base | ((base + ((size - 1) / 0x10000)) << 13) | (comp << 26) | (1 << 30);

		auto& tile = render->tiles[a3];

		// When tile is going to be unbinded, we can use it as a hint that the address will no longer be used as a surface and can be removed/invalidated
		// Todo: There may be more checks such as format/size/width can could be done
		if (tile.binded && a5 == 0)
			render->notify_tile_unbound(a3);

		tile.location = ((a4 >> 32) & 0xF) - 1;
		tile.offset = ((((a4 >> 32) & 0x7FFFFFFF) >> 16) * 0x10000);
		tile.size = ((((a4 & 0x7FFFFFFF) >> 16) + 1) * 0x10000) - tile.offset;
		tile.pitch = (((a5 >> 32) & 0xFFFFFFFF) >> 8) * 0x100;
		tile.comp = ((a5 & 0xFFFFFFFF) >> 26) & 0xF;
		tile.base = (a5 & 0xFFFFFFFF) & 0x7FF;
		tile.bank = (((a4 >> 32) & 0xFFFFFFFF) >> 4) & 0xF;
		tile.binded = a5 != 0;
	}
	break;

	case 0x301: // Depth-buffer (Z-cull)
	{
		//a4 high = region = (1 << 0) | (zFormat << 4) | (aaFormat << 8);
		//a4 low = size = ((width >> 6) << 22) | ((height >> 6) << 6);
		//a5 high = start = cullStart&(~0xFFF);
		//a5 low = offset = offset;
		//a6 high = status0 = (zcullDir << 1) | (zcullFormat << 2) | ((sFunc & 0xF) << 12) | (sRef << 16) | (sMask << 24);
		//a6 low = status1 = (0x2000 << 0) | (0x20 << 16);

		auto &zcull = render->zculls[a3];
		zcull.zFormat = ((a4 >> 32) >> 4) & 0xF;
		zcull.aaFormat = ((a4 >> 32) >> 8) & 0xF;
		zcull.width = ((a4 & 0xFFFFFFFF) >> 22) << 6;
		zcull.height = (((a4 & 0xFFFFFFFF) >> 6) & 0xFF) << 6;
		zcull.cullStart = (a5 >> 32);
		zcull.offset = (a5 & 0xFFFFFFFF);
		zcull.zcullDir = ((a6 >> 32) >> 1) & 0x1;
		zcull.zcullFormat = ((a6 >> 32) >> 2) & 0x3FF;
		zcull.sFunc = ((a6 >> 32) >> 12) & 0xF;
		zcull.sRef = ((a6 >> 32) >> 16) & 0xFF;
		zcull.sMask = ((a6 >> 32) >> 24) & 0xFF;
		zcull.binded = (a6 & 0xFFFFFFFF) != 0;
	}
	break;

	case 0x302: // something with zcull
		break;

	case 0x600: // Framebuffer setup
		break;

	case 0x601: // Framebuffer blit
		break;

	case 0x602: // Framebuffer blit sync
		break;

	case 0x603: // Framebuffer close
		break;

	case 0xFEC: // hack: flip event notification
		// we only ever use head 1 for now
		driverInfo.head[1].flipFlags |= 0x80000000;
		driverInfo.head[1].lastFlipTime = rsxTimeStamp(); // should rsxthread set this?
		driverInfo.head[1].flipBufferId = (u32)a3;

		// seems gcmSysWaitLabel uses this offset, so lets set it to 0 every flip
		vm::_ref<u32>(render->label_addr + 0x10) = 0;

		//if (a3 == 0)
		//	sys_event_port_send(m_sysrsx->rsx_event_port, 0, (1 << 3), 0);
		//if (a3 == 1)
		sys_event_port_send(m_sysrsx->rsx_event_port, 0, (1 << 4), 0);
		break;

	case 0xFED: // hack: vblank command
		// todo: this is wrong and should be 'second' vblank handler and freq, but since currently everything is reported as being 59.94, this should be fine
		driverInfo.head[a3].vBlankCount++;
		driverInfo.head[a3].lastSecondVTime = rsxTimeStamp();
		sys_event_port_send(m_sysrsx->rsx_event_port, 0, (1 << 1), 0);

		if (render->enable_second_vhandler)
			sys_event_port_send(m_sysrsx->rsx_event_port, 0, (1 << 11), 0); // second vhandler

		break;

	case 0xFEF: // hack: user command
		// 'custom' invalid package id for now
		// as i think we need custom lv1 interrupts to handle this accurately
		// this also should probly be set by rsxthread
		driverInfo.userCmdParam = a4;
		sys_event_port_send(m_sysrsx->rsx_event_port, 0, (1 << 7), 0);
		break;

	default:
		return CELL_EINVAL;
	}

	return CELL_OK;
}

/*
 * lv2 SysCall 675 (0x2A3): sys_rsx_device_map
 * @param a1 (OUT): rsx device map address : 0x40000000, 0x50000000.. 0xB0000000
 * @param a2 (OUT): Unused?
 * @param dev_id (IN): An immediate value and always 8. (cellGcmInitPerfMon uses 11, 10, 9, 7, 12 successively).
 */
s32 sys_rsx_device_map(vm::ptr<u64> dev_addr, vm::ptr<u64> a2, u32 dev_id)
{
	sys_rsx.warning("sys_rsx_device_map(dev_addr=*0x%x, a2=*0x%x, dev_id=0x%x)", dev_addr, a2, dev_id);

	if (dev_id != 8) {
		// TODO: lv1 related
		fmt::throw_exception("sys_rsx_device_map: Invalid dev_id %d", dev_id);
	}

	// a2 seems to not be referenced in cellGcmSys, tests show this arg is ignored
	//*a2 = 0;

	auto m_sysrsx = fxm::make<SysRsxConfig>();

	if (!m_sysrsx)
	{
		return CELL_EINVAL; // sys_rsx_device_map called twice
	}

	if (const auto area = vm::find_map(0x10000000, 0x10000000, 0x403))
	{
		vm::falloc(area->addr, 0x400000);
		m_sysrsx->rsx_context_addr = *dev_addr = area->addr;
		return CELL_OK;
	}

	return CELL_ENOMEM;
}

/*
 * lv2 SysCall 676 (0x2A4): sys_rsx_device_unmap
 * @param dev_id (IN): An immediate value and always 8.
 */
s32 sys_rsx_device_unmap(u32 dev_id)
{
	sys_rsx.todo("sys_rsx_device_unmap(dev_id=0x%x)", dev_id);

	return CELL_OK;
}

/*
 * lv2 SysCall 677 (0x2A5): sys_rsx_attribute
 */
s32 sys_rsx_attribute(u32 packageId, u32 a2, u32 a3, u32 a4, u32 a5)
{
	sys_rsx.warning("sys_rsx_attribute(packageId=0x%x, a2=0x%x, a3=0x%x, a4=0x%x, a5=0x%x)", packageId, a2, a3, a4, a5);

	return CELL_OK;
}
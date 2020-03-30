// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
// ******************************************************************
// *
// *  This file is part of Cxbx-Reloaded.
// *
// *  Cxbx-Reloaded is free software; you can redistribute it
// *  and/or modify it under the terms of the GNU General Public
// *  License as published by the Free Software Foundation; either
// *  version 2 of the license, or (at your option) any later version.
// *
// *  This program is distributed in the hope that it will be useful,
// *  but WITHOUT ANY WARRANTY; without even the implied warranty of
// *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// *  GNU General Public License for more details.
// *
// *  You should have recieved a copy of the GNU General Public License
// *  along with this program; see the file COPYING.
// *  If not, write to the Free Software Foundation, Inc.,
// *  59 Temple Place - Suite 330, Bostom, MA 02111-1307, USA.
// *
// *  (c) 2017-2019 Patrick van Logchem <pvanlogchem@gmail.com>
// *  (c) 2019 ergo720
// *
// *  All rights reserved
// *
// ******************************************************************
#pragma once

#include <cstdint> // For uint32_t

#define KB(x) ((x) *    1024 ) // = 0x00000400
#define MB(x) ((x) * KB(1024)) // = 0x00100000

#define USER_ADDRESS1_BASE                  0x00010000
#define USER_ADDRESS1_SIZE                  (MB(64) - KB(64)) // = 0x03FF0000
#define USER_ADDRESS1_END                   (USER_ADDRESS1_BASE + USER_ADDRESS1_SIZE - 1) // 0x03FFFFFF

#define USER_ADDRESS2_BASE                  0x04000000
#define USER_ADDRESS2_SIZE                  (MB(64)) // = 0x04000000
#define USER_ADDRESS2_END                   (USER_ADDRESS2_BASE + USER_ADDRESS2_SIZE - 1) // 0x07FFFFFF

// Base addresses of various components
#define KSEG0_BASE                          0x80000000

#define PHYSICAL_MAP_BASE                   0x80000000
#define PHYSICAL_MAP_SIZE                   (MB(256)) // = 0x10000000
#define PHYSICAL_MAP_END                    (PHYSICAL_MAP_BASE + PHYSICAL_MAP_SIZE - 1) // 0x8FFFFFFF

#define PHYSICAL_MAP1_BASE                  0x80000000
#define PHYSICAL_MAP1_SIZE                  (MB(64)) // = 0x04000000
#define PHYSICAL_MAP1_END                   (PHYSICAL_MAP1_BASE + PHYSICAL_MAP1_SIZE - 1) // 0x83FFFFFF

#define PHYSICAL_MAP2_BASE                  0x84000000
#define PHYSICAL_MAP2_SIZE                  (MB(64)) // = 0x04000000
#define PHYSICAL_MAP2_END                   (PHYSICAL_MAP2_BASE + PHYSICAL_MAP2_SIZE - 1) // 0x8FFFFFFF

#define CONTIGUOUS_MEMORY_BASE              KSEG0_BASE // = 0x80000000
#define XBOX_CONTIGUOUS_MEMORY_SIZE         (MB(64))
#define CHIHIRO_CONTIGUOUS_MEMORY_SIZE      (MB(128))

#define DEVKIT_MEMORY_BASE                  0xB0000000
#define DEVKIT_MEMORY_SIZE                  (MB(256)) // = 0x10000000
#define DEVKIT_MEMORY_END                   (DEVKIT_MEMORY_BASE + DEVKIT_MEMORY_SIZE - 1) // 0xBFFFFFFF

#define PAGE_TABLES_BASE                    0xC0000000
#define PAGE_TABLES_SIZE                    (MB(4)) // = 0x00400000
#define PAGE_TABLES_END                     (PAGE_TABLES_BASE + PAGE_TABLES_SIZE - 1) // 0xC03FFFFF

#define SYSTEM_MEMORY_BASE                  0xD0000000
#define SYSTEM_MEMORY_SIZE                  (MB(512)) // = 0x20000000
#define SYSTEM_MEMORY_END                   (SYSTEM_MEMORY_BASE + SYSTEM_MEMORY_SIZE - 1) // 0xEFFFFFFF

#define TILED_MEMORY_BASE                   0xF0000000
#define TILED_MEMORY_SIZE                   (MB(64)) // = 0x04000000
#define TILED_MEMORY_END                    (TILED_MEMORY_BASE + TILED_MEMORY_SIZE - 1) // 0xF3FFFFFF

#define XBOX_WRITE_COMBINED_BASE            0xF0000000 // WC (The WC memory is another name of the tiled memory)
#define XBOX_WRITE_COMBINED_SIZE            (MB(128)) // = 0x08000000
#define XBOX_WRITE_COMBINED_END             (XBOX_WRITE_COMBINED_BASE + XBOX_WRITE_COMBINED_SIZE - 1) // 0xF7FFFFFF

#define XBOX_UNCACHED_BASE                  0xF8000000 // UC
#define XBOX_UNCACHED_SIZE                  (MB(128 - 4)) // = 0x07C00000
#define XBOX_UNCACHED_END                   (XBOX_UNCACHED_BASE + XBOX_UNCACHED_SIZE - 1) // - 0xFFBFFFFF

#define NV2A_DEVICE1_BASE                   0xFD000000
#define NV2A_DEVICE1_SIZE                   (MB(7)) // = 0x00700000
#define NV2A_DEVICE1_END                    (NV2A_DEVICE1_BASE + NV2A_DEVICE1_SIZE - 1) // 0xFD6FFFFF

#define NV2A_PRAMIN_BASE                    0xFD700000
#define NV2A_PRAMIN_SIZE1                   (MB(1)) // = 0x00100000 // TODO: Might be best to merge PCIDevice.h's address ranges into this header file.
#define NV2A_PRAMIN_END                     (NV2A_PRAMIN_BASE + NV2A_PRAMIN_SIZE1 - 1) // 0xFD7FFFFF

#define NV2A_DEVICE2_BASE                   0xFD800000
#define NV2A_DEVICE2_SIZE                   (MB(8)) // = 0x00800000
#define NV2A_DEVICE2_END                    (NV2A_DEVICE2_BASE + NV2A_DEVICE2_SIZE - 1) // 0xFDFFFFFF

#define APU_DEVICE_BASE                     0xFE800000
#define APU_DEVICE_SIZE                     (KB(512)) // = 0x00080000
#define APU_DEVICE_END                      (APU_DEVICE_BASE + APU_DEVICE_SIZE - 1) // 0xFE87FFFF

#define AC97_DEVICE_BASE                    0xFEC00000
#define AC97_DEVICE_SIZE                    (KB(4)) // = 0x0001000
#define AC97_DEVICE_END                     (AC97_DEVICE_BASE + AC97_DEVICE_SIZE - 1) // 0xFEC00FFF

#define USB_DEVICE_BASE                     0xFED00000
#define USB_DEVICE_SIZE                     (KB(4)) // = 0x0001000
#define USB_DEVICE_END                      (USB_DEVICE_BASE + USB_DEVICE_SIZE - 1) // 0xFED00FFF

#define NVNET_DEVICE_BASE                   0xFEF00000
#define NVNET_DEVICE_SIZE                   (KB(1)) // = 0x00000400
#define NVNET_DEVICE_END                    (NVNET_DEVICE_BASE + NVNET_DEVICE_SIZE - 1) // 0xFEF003FF

#define XBOX_FLASH_ROM_BASE                 0xFFF00000
#define XBOX_FLASH_ROM_SIZE                 (MB(1)) // = 0x00100000
#define XBOX_FLASH_ROM_END                  (XBOX_FLASH_ROM_BASE + XBOX_FLASH_ROM_SIZE - 1) // - 0xFFFFFFF

#define FLASH_DEVICEN_SIZE                  (MB(4)) // = 0x00400000
#define FLASH_DEVICE1_BASE                  0xFF000000
#define FLASH_DEVICE1_END                   (FLASH_DEVICE1_BASE + FLASH_DEVICEN_SIZE - 1) // 0xFF3FFFFF

#define FLASH_DEVICE2_BASE                  0xFF400000
#define FLASH_DEVICE2_END                   (FLASH_DEVICE2_BASE + FLASH_DEVICEN_SIZE - 1) // 0xFF7FFFFF

#define FLASH_DEVICE3_BASE                  0xFF800000
#define FLASH_DEVICE3_END                   (FLASH_DEVICE3_BASE + FLASH_DEVICEN_SIZE - 1) // 0xFFBFFFFF

#define FLASH_DEVICE4_BASE                  0xFFC00000
#define FLASH_DEVICE4_END                   (FLASH_DEVICE4_BASE + FLASH_DEVICEN_SIZE - 1) // 0xFFFFFFFF

// Miscellaneous base addresses
#define XBE_IMAGE_BASE                      0x00010000
#define PAGE_DIRECTORY_BASE                 0xC0300000
#define NV2A_INIT_VECTOR                    0xFF000008

// Define virtual base and alternate virtual base of kernel
#define XBOX_KERNEL_BASE                    (PHYSICAL_MAP_BASE + XBE_IMAGE_BASE)
#define KERNEL_PHYSICAL_ADDRESS             XBE_IMAGE_BASE // = 0x10000
#define KERNEL_SIZE                         sizeof(DUMMY_KERNEL)
#define KERNEL_STACK_SIZE                   12288 // 0x03000, needed by PsCreateSystemThreadEx, however the current implementation doesn't use it

// Miscellaneous memory variables
// Xbox pages are (1 << 12) = 0x00001000 = 4096 bytes in size. Large pages are 4 MiB instead
// NOTE: PAGE_SIZE is also defined in xfile.h (oxdk) and linux_wrapper.h (oxdk)
#define PAGE_SHIFT                          12 // 2^12 = 4 KiB
#define PAGE_SIZE                           (1 << PAGE_SHIFT)
#define PAGE_MASK                           (PAGE_SIZE - 1)

#define LARGE_PAGE_SHIFT                    22 // 2^22 = 4 MiB
#define LARGE_PAGE_SIZE                     (1 << LARGE_PAGE_SHIFT) // = 0x00400000 = 4 MiB
#define LARGE_PAGE_MASK                     (LARGE_PAGE_SIZE - 1)

#define BYTES_IN_PHYSICAL_MAP               (MB(256)) // this refers to the system RAM physical window 0x80000000 - 0x8FFFFFFF
#define MAXIMUM_ZERO_BITS                   21 // for XbAllocateVirtualMemory
#define MAX_VIRTUAL_ADDRESS                 0xFFFFFFFF

#define LOWEST_USER_ADDRESS                 XBE_IMAGE_BASE // = 0x00010000
#define HIGHEST_USER_ADDRESS                0x7FFEFFFF
#define HIGHEST_VMA_ADDRESS                 (HIGHEST_USER_ADDRESS - X64KB) // for NtAllocateVirtualMemory
#define USER_MEMORY_SIZE                    (HIGHEST_USER_ADDRESS - LOWEST_USER_ADDRESS + 1) // 0x7FFE0000 = 2 GiB - 128 KiB

// Memory size per system
#define XBOX_MEMORY_SIZE                    (MB(64))
#define CHIHIRO_MEMORY_SIZE                 (MB(128))

// Windows' address space allocation granularity;
// See https://blogs.msdn.microsoft.com/oldnewthing/20031008-00/?p=42223
const int BLOCK_SIZE = KB(64);

extern const struct _XboxAddressRanges {
	uint32_t Start;
#ifdef DEBUG
	uint32_t End; // TODO : Add validation that this End corresponds to specified Size
#endif
	uint32_t Size;
	unsigned long InitialMemoryProtection; // Memory page protection, for use by VirtualAlloc
	// Shortend symbol aliasses for memory page protection
	#define PROT_UNH 0 // UNHANDLED
	#define PROT_RW  PAGE_READWRITE
	#define PROT_XRW PAGE_EXECUTE_READWRITE
	#define PROT_NAC PAGE_NOACCESS

	unsigned int RangeFlags;
	// Range flags (used for system selection and optional marker)
	#define MAY_FAIL       (1 << 0) // Optional (may fail address range reservation)
	#define SYSTEM_XBOX    (1 << 1)
	#define SYSTEM_DEVKIT  (1 << 2)
	#define SYSTEM_CHIHIRO (1 << 3)
	// Short-hand for sets of system configurations
	#define SYSTEM_ALL     (SYSTEM_XBOX | SYSTEM_DEVKIT | SYSTEM_CHIHIRO)
	#define SYSTEM_128MB   (              SYSTEM_DEVKIT | SYSTEM_CHIHIRO)
#ifdef DEBUG
	const char *Comment;
#endif
} XboxAddressRanges[];

extern const size_t XboxAddressRanges_size;

#define BLOCK_REGION_DEVKIT_INDEX_BEGIN 0
#define BLOCK_REGION_DEVKIT_INDEX_END   4096
#define BLOCK_REGION_SYSTEM_INDEX_BEGIN 4096
#define BLOCK_REGION_SYSTEM_INDEX_END   12288

extern bool AddressRangeMatchesFlags(const int index, const unsigned int flags);
extern bool IsOptionalAddressRange(const int index);
extern int AddressRangeGetSystemFlags(const int index);

extern bool VerifyWow64();

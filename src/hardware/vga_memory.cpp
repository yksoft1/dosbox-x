/*
 *  Copyright (C) 2002-2015  The DOSBox Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */


#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dosbox.h"
#include "mem.h"
#include "vga.h"
#include "paging.h"
#include "pic.h"
#include "inout.h"
#include "setup.h"
#include "cpu.h"
#include "pc98_cg.h"
#include "pc98_gdc.h"

#ifndef C_VGARAM_CHECKED
#define C_VGARAM_CHECKED 1
#endif

#if C_VGARAM_CHECKED
// Checked linear offset
#define CHECKED(v) ((v)&(vga.vmemwrap-1))
// Checked planar offset (latched access)
#define CHECKED2(v) ((v)&((vga.vmemwrap>>2)-1))
#else
#define CHECKED(v) (v)
#define CHECKED2(v) (v)
#endif

#define CHECKED3(v) ((v)&(vga.vmemwrap-1))
#define CHECKED4(v) ((v)&((vga.vmemwrap>>2)-1))

#define TANDY_VIDBASE(_X_)  &MemBase[ 0x80000 + (_X_)]

/* how much delay to add to VGA memory I/O in nanoseconds */
int vga_memio_delay_ns = 1000;

void VGAMEM_USEC_read_delay() {
	if (vga_memio_delay_ns > 0) {
		Bits delaycyc = (CPU_CycleMax * vga_memio_delay_ns) / 1000000;
//		if(GCC_UNLIKELY(CPU_Cycles < 3*delaycyc)) delaycyc = 0; //Else port acces will set cycles to 0. which might trigger problem with games which read 16 bit values
		CPU_Cycles -= delaycyc;
		CPU_IODelayRemoved += delaycyc;
	}
}

void VGAMEM_USEC_write_delay() {
	if (vga_memio_delay_ns > 0) {
		Bits delaycyc = (CPU_CycleMax * vga_memio_delay_ns * 3) / (1000000 * 4);
//		if(GCC_UNLIKELY(CPU_Cycles < 3*delaycyc)) delaycyc = 0; //Else port acces will set cycles to 0. which might trigger problem with games which read 16 bit values
		CPU_Cycles -= delaycyc;
		CPU_IODelayRemoved += delaycyc;
	}
}

template <class Size>
static INLINE void hostWrite(HostPt off, Bitu val) {
	if ( sizeof( Size ) == 1)
		host_writeb( off, (Bit8u)val );
	else if ( sizeof( Size ) == 2)
		host_writew( off, (Bit16u)val );
	else if ( sizeof( Size ) == 4)
		host_writed( off, (Bit32u)val );
}

template <class Size>
static INLINE Bitu  hostRead(HostPt off ) {
	if ( sizeof( Size ) == 1)
		return host_readb( off );
	else if ( sizeof( Size ) == 2)
		return host_readw( off );
	else if ( sizeof( Size ) == 4)
		return host_readd( off );
	return 0;
}


void VGA_MapMMIO(void);
//Nice one from DosEmu
INLINE static Bit32u RasterOp(Bit32u input,Bit32u mask) {
	switch (vga.config.raster_op) {
	case 0x00:	/* None */
		return (input & mask) | (vga.latch.d & ~mask);
	case 0x01:	/* AND */
		return (input | ~mask) & vga.latch.d;
	case 0x02:	/* OR */
		return (input & mask) | vga.latch.d;
	case 0x03:	/* XOR */
		return (input & mask) ^ vga.latch.d;
	};
	return 0;
}

INLINE static Bit32u ModeOperation(Bit8u val) {
	Bit32u full;
	switch (vga.config.write_mode) {
	case 0x00:
		// Write Mode 0: In this mode, the host data is first rotated as per the Rotate Count field, then the Enable Set/Reset mechanism selects data from this or the Set/Reset field. Then the selected Logical Operation is performed on the resulting data and the data in the latch register. Then the Bit Mask field is used to select which bits come from the resulting data and which come from the latch register. Finally, only the bit planes enabled by the Memory Plane Write Enable field are written to memory. 
		val=((val >> vga.config.data_rotate) | (val << (8-vga.config.data_rotate)));
		full=ExpandTable[val];
		full=(full & vga.config.full_not_enable_set_reset) | vga.config.full_enable_and_set_reset; 
		full=RasterOp(full,vga.config.full_bit_mask);
		break;
	case 0x01:
		// Write Mode 1: In this mode, data is transferred directly from the 32 bit latch register to display memory, affected only by the Memory Plane Write Enable field. The host data is not used in this mode. 
		full=vga.latch.d;
		break;
	case 0x02:
		//Write Mode 2: In this mode, the bits 3-0 of the host data are replicated across all 8 bits of their respective planes. Then the selected Logical Operation is performed on the resulting data and the data in the latch register. Then the Bit Mask field is used to select which bits come from the resulting data and which come from the latch register. Finally, only the bit planes enabled by the Memory Plane Write Enable field are written to memory. 
		full=RasterOp(FillTable[val&0xF],vga.config.full_bit_mask);
		break;
	case 0x03:
		// Write Mode 3: In this mode, the data in the Set/Reset field is used as if the Enable Set/Reset field were set to 1111b. Then the host data is first rotated as per the Rotate Count field, then logical ANDed with the value of the Bit Mask field. The resulting value is used on the data obtained from the Set/Reset field in the same way that the Bit Mask field would ordinarily be used. to select which bits come from the expansion of the Set/Reset field and which come from the latch register. Finally, only the bit planes enabled by the Memory Plane Write Enable field are written to memory.
		val=((val >> vga.config.data_rotate) | (val << (8-vga.config.data_rotate)));
		full=RasterOp(vga.config.full_set_reset,ExpandTable[val] & vga.config.full_bit_mask);
		break;
	default:
		LOG(LOG_VGAMISC,LOG_NORMAL)("VGA:Unsupported write mode %d",vga.config.write_mode);
		full=0;
		break;
	}
	return full;
}

/* Gonna assume that whoever maps vga memory, maps it on 32/64kb boundary */

#define VGA_PAGES		(128/4)
#define VGA_PAGE_A0		(0xA0000/4096)
#define VGA_PAGE_B0		(0xB0000/4096)
#define VGA_PAGE_B8		(0xB8000/4096)

static struct {
	Bitu base, mask;
} vgapages;
	
class VGA_UnchainedRead_Handler : public PageHandler {
public:
	VGA_UnchainedRead_Handler(Bitu flags) : PageHandler(flags) {}
	Bitu readHandler(PhysPt start) {
		PhysPt memstart = start;
		unsigned char bplane;

		if (vga.gfx.miscellaneous&2) /* Odd/Even mode */
			memstart &= ~1u;

		vga.latch.d=((Bit32u*)vga.mem.linear)[memstart];
		switch (vga.config.read_mode) {
			case 0:
				bplane = vga.config.read_map_select;
				/* NTS: We check the sequencer AND the GC to know whether we mask the bitplane line this,
				 *      even though in TEXT mode we only check the sequencer. Without this extra check,
				 *      Windows 95 and Windows 3.1 will exhibit glitches in the standard VGA 640x480x16
				 *      planar mode */
				if (!(vga.seq.memory_mode&4) && (vga.gfx.miscellaneous&2)) /* FIXME: How exactly do SVGA cards determine this? */
					bplane = (bplane & ~1u) + (start & 1u); /* FIXME: Is this what VGA cards do? It makes sense to me */
				return (vga.latch.b[bplane]);
			case 1:
				VGA_Latch templatch;
				templatch.d=(vga.latch.d & FillTable[vga.config.color_dont_care]) ^ FillTable[vga.config.color_compare & vga.config.color_dont_care];
				return (Bit8u)~(templatch.b[0] | templatch.b[1] | templatch.b[2] | templatch.b[3]);
		}
		return 0;
	}
public:
	Bitu readb(PhysPt addr) {
		VGAMEM_USEC_read_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_read_full;
		addr = CHECKED2(addr);
		return readHandler(addr);
	}
	Bitu readw(PhysPt addr) {
		VGAMEM_USEC_read_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_read_full;
		addr = CHECKED2(addr);
		Bitu ret = (readHandler(addr+0) << 0);
		ret     |= (readHandler(addr+1) << 8);
		return  ret;
	}
	Bitu readd(PhysPt addr) {
		VGAMEM_USEC_read_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_read_full;
		addr = CHECKED2(addr);
		Bitu ret = (readHandler(addr+0) << 0);
		ret     |= (readHandler(addr+1) << 8);
		ret     |= (readHandler(addr+2) << 16);
		ret     |= (readHandler(addr+3) << 24);
		return ret;
	}
};

class VGA_ChainedEGA_Handler : public PageHandler {
public:
	Bitu readHandler(PhysPt addr) {
		return vga.mem.linear[addr];
	}
	void writeHandler(PhysPt start, Bit8u val) {
		/* FIXME: "Chained EGA" how does that work?? */
		ModeOperation(val);
		/* Update video memory and the pixel buffer */
		vga.mem.linear[start] = val;
	}
public:	
	VGA_ChainedEGA_Handler() : PageHandler(PFLAG_NOCODE) {}
	void writeb(PhysPt addr,Bitu val) {
		VGAMEM_USEC_write_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_write_full;
		addr = CHECKED(addr);
		writeHandler(addr+0,(Bit8u)(val >> 0));
	}
	void writew(PhysPt addr,Bitu val) {
		VGAMEM_USEC_write_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_write_full;
		addr = CHECKED(addr);
		writeHandler(addr+0,(Bit8u)(val >> 0));
		writeHandler(addr+1,(Bit8u)(val >> 8));
	}
	void writed(PhysPt addr,Bitu val) {
		VGAMEM_USEC_write_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_write_full;
		addr = CHECKED(addr);
		writeHandler(addr+0,(Bit8u)(val >> 0));
		writeHandler(addr+1,(Bit8u)(val >> 8));
		writeHandler(addr+2,(Bit8u)(val >> 16));
		writeHandler(addr+3,(Bit8u)(val >> 24));
	}
	Bitu readb(PhysPt addr) {
		VGAMEM_USEC_read_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_read_full;
		addr = CHECKED(addr);
		return readHandler(addr);
	}
	Bitu readw(PhysPt addr) {
		VGAMEM_USEC_read_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_read_full;
		addr = CHECKED(addr);
		Bitu ret = (readHandler(addr+0) << 0);
		ret     |= (readHandler(addr+1) << 8);
		return ret;
	}
	Bitu readd(PhysPt addr) {
		VGAMEM_USEC_read_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_read_full;
		addr = CHECKED(addr);
		Bitu ret = (readHandler(addr+0) << 0);
		ret     |= (readHandler(addr+1) << 8);
		ret     |= (readHandler(addr+2) << 16);
		ret     |= (readHandler(addr+3) << 24);
		return ret;
	}
};

class VGA_UnchainedEGA_Handler : public VGA_UnchainedRead_Handler {
public:
	VGA_UnchainedEGA_Handler(Bitu flags) : VGA_UnchainedRead_Handler(flags) {}
	template< bool wrapping>
	void writeHandler(PhysPt start, Bit8u val) {
		Bit32u data=ModeOperation(val);
		/* Update video memory and the pixel buffer */
		VGA_Latch pixels;
		pixels.d=((Bit32u*)vga.mem.linear)[start];
		pixels.d&=vga.config.full_not_map_mask;
		pixels.d|=(data & vga.config.full_map_mask);
		((Bit32u*)vga.mem.linear)[start]=pixels.d;
	}
public:	
	VGA_UnchainedEGA_Handler() : VGA_UnchainedRead_Handler(PFLAG_NOCODE) {}
	void writeb(PhysPt addr,Bitu val) {
		VGAMEM_USEC_write_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_write_full;
		addr = CHECKED2(addr);
		writeHandler<true>(addr+0,(Bit8u)(val >> 0));
	}
	void writew(PhysPt addr,Bitu val) {
		VGAMEM_USEC_write_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_write_full;
		addr = CHECKED2(addr);
		writeHandler<true>(addr+0,(Bit8u)(val >> 0));
		writeHandler<true>(addr+1,(Bit8u)(val >> 8));
	}
	void writed(PhysPt addr,Bitu val) {
		VGAMEM_USEC_write_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_write_full;
		addr = CHECKED2(addr);
		writeHandler<true>(addr+0,(Bit8u)(val >> 0));
		writeHandler<true>(addr+1,(Bit8u)(val >> 8));
		writeHandler<true>(addr+2,(Bit8u)(val >> 16));
		writeHandler<true>(addr+3,(Bit8u)(val >> 24));
	}
};

// Slow accurate emulation.
// This version takes the Graphics Controller bitmask and ROPs into account.
// This is needed for demos that use the bitmask to do color combination or bitplane "page flipping" tricks.
// This code will kick in if running in a chained VGA mode and the graphics controller bitmask register is
// changed to anything other than 0xFF.
//
// Impact Studios "Legend"
//  - The rotating objects, rendered as dots, needs this hack because it uses a combination of masking off
//    bitplanes using the VGA DAC pel mask and drawing on the hidden bitplane using the Graphics Controller
//    bitmask. It also relies on loading the VGA latches with zeros as a form of "overdraw". Without this
//    version the effect will instead become a glowing ball of flickering yellow/red.
class VGA_ChainedVGA_Slow_Handler : public PageHandler {
public:
	VGA_ChainedVGA_Slow_Handler() : PageHandler(PFLAG_NOCODE) {}
	static INLINE Bitu readHandler8(PhysPt addr ) {
		vga.latch.d=((Bit32u*)vga.mem.linear)[addr&~3u];
		return vga.latch.b[addr&3];
	}
	static INLINE void writeHandler8(PhysPt addr, Bitu val) {
		VGA_Latch pixels;

		/* byte-sized template specialization with masking */
		pixels.d = ModeOperation(val);
		/* Update video memory and the pixel buffer */
		hostWrite<Bit8u>( &vga.mem.linear[((addr&~3u)<<2u)+(addr&3u)], pixels.b[addr&3u] );
	}
	Bitu readb(PhysPt addr ) {
		VGAMEM_USEC_read_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_read_full;
		addr = CHECKED(addr);
		return readHandler8( addr );
	}
	Bitu readw(PhysPt addr ) {
		VGAMEM_USEC_read_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_read_full;
		addr = CHECKED(addr);
		Bitu ret = (readHandler8( addr+0 ) << 0 );
		ret     |= (readHandler8( addr+1 ) << 8 );
		return ret;
	}
	Bitu readd(PhysPt addr ) {
		VGAMEM_USEC_read_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_read_full;
		addr = CHECKED(addr);
		Bitu ret = (readHandler8( addr+0 ) << 0 );
		ret     |= (readHandler8( addr+1 ) << 8 );
		ret     |= (readHandler8( addr+2 ) << 16 );
		ret     |= (readHandler8( addr+3 ) << 24 );
		return ret;
	}
	void writeb(PhysPt addr, Bitu val ) {
		VGAMEM_USEC_write_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_write_full;
		addr = CHECKED(addr);
		writeHandler8( addr, val );
	}
	void writew(PhysPt addr,Bitu val) {
		VGAMEM_USEC_write_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_write_full;
		addr = CHECKED(addr);
		writeHandler8( addr+0, val >> 0 );
		writeHandler8( addr+1, val >> 8 );
	}
	void writed(PhysPt addr,Bitu val) {
		VGAMEM_USEC_write_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_write_full;
		addr = CHECKED(addr);
		writeHandler8( addr+0, val >> 0 );
		writeHandler8( addr+1, val >> 8 );
		writeHandler8( addr+2, val >> 16 );
		writeHandler8( addr+3, val >> 24 );
	}
};

//Slighly unusual version, will directly write 8,16,32 bits values
class VGA_ChainedVGA_Handler : public PageHandler {
public:
	VGA_ChainedVGA_Handler() : PageHandler(PFLAG_NOCODE) {}
	template <class Size>
	static INLINE Bitu readHandler(PhysPt addr ) {
		return hostRead<Size>( &vga.mem.linear[((addr&0xFFFC)<<2)+(addr&3)] );
	}
	template <class Size>
	static INLINE void writeHandler(PhysPt addr, Bitu val) {
		// No need to check for compatible chains here, this one is only enabled if that bit is set
		hostWrite<Size>( &vga.mem.linear[((addr&0xFFFC)<<2)+(addr&3)], val );
	}
	Bitu readb(PhysPt addr ) {
		VGAMEM_USEC_read_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_read_full;
		addr = CHECKED(addr);
		return readHandler<Bit8u>( addr );
	}
	Bitu readw(PhysPt addr ) {
		VGAMEM_USEC_read_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_read_full;
		addr = CHECKED(addr);
		if (GCC_UNLIKELY(addr & 1)) {
			Bitu ret = (readHandler<Bit8u>( addr+0 ) << 0 );
			ret     |= (readHandler<Bit8u>( addr+1 ) << 8 );
			return ret;
		} else
			return readHandler<Bit16u>( addr );
	}
	Bitu readd(PhysPt addr ) {
		VGAMEM_USEC_read_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_read_full;
		addr = CHECKED(addr);
		if (GCC_UNLIKELY(addr & 3)) {
			Bitu ret = (readHandler<Bit8u>( addr+0 ) << 0 );
			ret     |= (readHandler<Bit8u>( addr+1 ) << 8 );
			ret     |= (readHandler<Bit8u>( addr+2 ) << 16 );
			ret     |= (readHandler<Bit8u>( addr+3 ) << 24 );
			return ret;
		} else
			return readHandler<Bit32u>( addr );
	}
	void writeb(PhysPt addr, Bitu val ) {
		VGAMEM_USEC_write_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_write_full;
		addr = CHECKED(addr);
		writeHandler<Bit8u>( addr, val );
	}
	void writew(PhysPt addr,Bitu val) {
		VGAMEM_USEC_write_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_write_full;
		addr = CHECKED(addr);
		if (GCC_UNLIKELY(addr & 1)) {
			writeHandler<Bit8u>( addr+0, val >> 0 );
			writeHandler<Bit8u>( addr+1, val >> 8 );
		} else {
			writeHandler<Bit16u>( addr, val );
		}
	}
	void writed(PhysPt addr,Bitu val) {
		VGAMEM_USEC_write_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_write_full;
		addr = CHECKED(addr);
		if (GCC_UNLIKELY(addr & 3)) {
			writeHandler<Bit8u>( addr+0, val >> 0 );
			writeHandler<Bit8u>( addr+1, val >> 8 );
			writeHandler<Bit8u>( addr+2, val >> 16 );
			writeHandler<Bit8u>( addr+3, val >> 24 );
		} else {
			writeHandler<Bit32u>( addr, val );
		}
	}
};

// alternate version for ET4000 emulation.
// ET4000 cards implement 256-color chain-4 differently than most cards.
class VGA_ET4000_ChainedVGA_Handler : public PageHandler {
public:
	VGA_ET4000_ChainedVGA_Handler() : PageHandler(PFLAG_NOCODE) {}
	template <class Size>
	static INLINE Bitu readHandler(PhysPt addr ) {
		return hostRead<Size>( &vga.mem.linear[addr] );
	}
	template <class Size>
	static INLINE void writeHandler(PhysPt addr, Bitu val) {
		// No need to check for compatible chains here, this one is only enabled if that bit is set
		hostWrite<Size>( &vga.mem.linear[addr], val );
	}
	Bitu readb(PhysPt addr ) {
		VGAMEM_USEC_read_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_read_full;
		addr = CHECKED(addr);
		return readHandler<Bit8u>( addr );
	}
	Bitu readw(PhysPt addr ) {
		VGAMEM_USEC_read_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_read_full;
		addr = CHECKED(addr);
		if (GCC_UNLIKELY(addr & 1)) {
			Bitu ret = (readHandler<Bit8u>( addr+0 ) << 0 );
			ret     |= (readHandler<Bit8u>( addr+1 ) << 8 );
			return ret;
		} else
			return readHandler<Bit16u>( addr );
	}
	Bitu readd(PhysPt addr ) {
		VGAMEM_USEC_read_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_read_full;
		addr = CHECKED(addr);
		if (GCC_UNLIKELY(addr & 3)) {
			Bitu ret = (readHandler<Bit8u>( addr+0 ) << 0 );
			ret     |= (readHandler<Bit8u>( addr+1 ) << 8 );
			ret     |= (readHandler<Bit8u>( addr+2 ) << 16 );
			ret     |= (readHandler<Bit8u>( addr+3 ) << 24 );
			return ret;
		} else
			return readHandler<Bit32u>( addr );
	}
	void writeb(PhysPt addr, Bitu val ) {
		VGAMEM_USEC_write_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_write_full;
		addr = CHECKED(addr);
		writeHandler<Bit8u>( addr, val );
	}
	void writew(PhysPt addr,Bitu val) {
		VGAMEM_USEC_write_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_write_full;
		addr = CHECKED(addr);
		if (GCC_UNLIKELY(addr & 1)) {
			writeHandler<Bit8u>( addr+0, val >> 0 );
			writeHandler<Bit8u>( addr+1, val >> 8 );
		} else {
			writeHandler<Bit16u>( addr, val );
		}
	}
	void writed(PhysPt addr,Bitu val) {
		VGAMEM_USEC_write_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_write_full;
		addr = CHECKED(addr);
		if (GCC_UNLIKELY(addr & 3)) {
			writeHandler<Bit8u>( addr+0, val >> 0 );
			writeHandler<Bit8u>( addr+1, val >> 8 );
			writeHandler<Bit8u>( addr+2, val >> 16 );
			writeHandler<Bit8u>( addr+3, val >> 24 );
		} else {
			writeHandler<Bit32u>( addr, val );
		}
	}
};

class VGA_ET4000_ChainedVGA_Slow_Handler : public PageHandler {
public:
	VGA_ET4000_ChainedVGA_Slow_Handler() : PageHandler(PFLAG_NOCODE) {}
	static INLINE Bitu readHandler8(PhysPt addr ) {
		vga.latch.d=((Bit32u*)vga.mem.linear)[addr>>2];
		return vga.latch.b[addr&3];
	}
	static INLINE void writeHandler8(PhysPt addr, Bitu val) {
		VGA_Latch pixels;

		/* byte-sized template specialization with masking */
		pixels.d = ModeOperation(val);
		/* Update video memory and the pixel buffer */
		hostWrite<Bit8u>( &vga.mem.linear[addr], pixels.b[addr&3] );
	}
	Bitu readb(PhysPt addr ) {
		VGAMEM_USEC_read_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_read_full;
		addr = CHECKED(addr);
		return readHandler8( addr );
	}
	Bitu readw(PhysPt addr ) {
		VGAMEM_USEC_read_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_read_full;
		addr = CHECKED(addr);
		Bitu ret = (readHandler8( addr+0 ) << 0 );
		ret     |= (readHandler8( addr+1 ) << 8 );
		return ret;
	}
	Bitu readd(PhysPt addr ) {
		VGAMEM_USEC_read_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_read_full;
		addr = CHECKED(addr);
		Bitu ret = (readHandler8( addr+0 ) << 0 );
		ret     |= (readHandler8( addr+1 ) << 8 );
		ret     |= (readHandler8( addr+2 ) << 16 );
		ret     |= (readHandler8( addr+3 ) << 24 );
		return ret;
	}
	void writeb(PhysPt addr, Bitu val ) {
		VGAMEM_USEC_write_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_write_full;
		addr = CHECKED(addr);
		writeHandler8( addr, val );
	}
	void writew(PhysPt addr,Bitu val) {
		VGAMEM_USEC_write_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_write_full;
		addr = CHECKED(addr);
		writeHandler8( addr+0, val >> 0 );
		writeHandler8( addr+1, val >> 8 );
	}
	void writed(PhysPt addr,Bitu val) {
		VGAMEM_USEC_write_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_write_full;
		addr = CHECKED(addr);
		writeHandler8( addr+0, val >> 0 );
		writeHandler8( addr+1, val >> 8 );
		writeHandler8( addr+2, val >> 16 );
		writeHandler8( addr+3, val >> 24 );
	}
};

class VGA_UnchainedVGA_Handler : public VGA_UnchainedRead_Handler {
public:
	void writeHandler( PhysPt addr, Bit8u val ) {
		PhysPt memaddr = addr;
		Bit32u data=ModeOperation(val);
		VGA_Latch pixels;

		if (vga.gfx.miscellaneous&2) /* Odd/Even mode masks off A0 */
			memaddr &= ~1u;

		pixels.d=((Bit32u*)vga.mem.linear)[memaddr];

		/* Odd/even emulation, emulation fix for Windows 95's boot screen */
		if (!(vga.seq.memory_mode&4)) {
			/* You're probably wondering what the hell odd/even mode has to do with Windows 95's boot
			 * screen, right? Well, hopefully you won't puke when you read the following...
			 * 
			 * When Windows 95 starts up and shows it's boot logo, it calls INT 10h to set mode 0x13.
			 * But it calls INT 10h with AX=0x93 which means set mode 0x13 and don't clear VRAM. Then,
			 * it uses mode X to write the logo to the BOTTOM half of VGA RAM, at 0x8000 to be exact,
			 * and of course, reprograms the CRTC offset register to make that visible.
			 * THEN, it reprograms the registers to map VRAM at 0xB800, disable Chain 4, re-enable
			 * odd/even mode, and then allows both DOS and the BIOS to write to the top half of VRAM
			 * as if still running in 80x25 alphanumeric text mode. It even sets the video mode byte
			 * at 0x40:0x49 to 0x03 to continue the illusion!
			 *
			 * When Windows 95 is ready to restore text mode, it just switches back (this time, calling
			 * the saved INT 10h pointer directly) again without clearing VRAM.
			 *
			 * So if you wonder why I would spend time implementing odd/even emulation for VGA unchained
			 * mode... that's why. You can thank Microsoft for that. */
			if (addr & 1) {
				if (vga.seq.map_mask & 0x2) /* bitplane 1: attribute RAM */
					pixels.b[1] = data >> 8;
				if (vga.seq.map_mask & 0x8) /* bitplane 3: unused RAM */
					pixels.b[3] = data >> 24;
			}
			else {
				if (vga.seq.map_mask & 0x1) /* bitplane 0: character RAM */
					pixels.b[0] = data;
				if (vga.seq.map_mask & 0x4) { /* bitplane 2: font RAM */
					pixels.b[2] = data >> 16;
					vga.draw.font[memaddr] = data >> 16;
				}
			}
		}
		else {
			pixels.d&=vga.config.full_not_map_mask;
			pixels.d|=(data & vga.config.full_map_mask);
		}

		((Bit32u*)vga.mem.linear)[memaddr]=pixels.d;
	}
public:
	VGA_UnchainedVGA_Handler() : VGA_UnchainedRead_Handler(PFLAG_NOCODE) {}
	void writeb(PhysPt addr,Bitu val) {
		VGAMEM_USEC_write_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_write_full;
		addr = CHECKED2(addr);
		writeHandler(addr+0,(Bit8u)(val >> 0));
	}
	void writew(PhysPt addr,Bitu val) {
		VGAMEM_USEC_write_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_write_full;
		addr = CHECKED2(addr);
		writeHandler(addr+0,(Bit8u)(val >> 0));
		writeHandler(addr+1,(Bit8u)(val >> 8));
	}
	void writed(PhysPt addr,Bitu val) {
		VGAMEM_USEC_write_delay();
		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		addr += vga.svga.bank_write_full;
		addr = CHECKED2(addr);
		writeHandler(addr+0,(Bit8u)(val >> 0));
		writeHandler(addr+1,(Bit8u)(val >> 8));
		writeHandler(addr+2,(Bit8u)(val >> 16));
		writeHandler(addr+3,(Bit8u)(val >> 24));
	}
};

#include <stdio.h>

class VGA_CGATEXT_PageHandler : public PageHandler {
public:
	VGA_CGATEXT_PageHandler() {
		flags=PFLAG_NOCODE;
	}
	Bitu readb(PhysPt addr) {
		addr = PAGING_GetPhysicalAddress(addr) & 0x3FFF;
		VGAMEM_USEC_read_delay();
		return vga.tandy.mem_base[addr];
	}
	void writeb(PhysPt addr,Bitu val){
		VGAMEM_USEC_write_delay();

		if (enableCGASnow) {
			/* NTS: We can't use PIC_FullIndex() exclusively because it's not precise enough
			 *      with respect to when DOSBox CPU emulation is writing. We have to use other
			 *      variables like CPU_Cycles to gain additional precision */
			double timeInFrame = PIC_FullIndex()-vga.draw.delay.framestart;
			double timeInLine = fmod(timeInFrame,vga.draw.delay.htotal);

			/* we're in active area. which column should the snow show up on? */
			Bit32u x = (Bit32u)((timeInLine * 80) / vga.draw.delay.hblkstart);
			if ((unsigned)x < 80) vga.draw.cga_snow[x] = val;
		}

		addr = PAGING_GetPhysicalAddress(addr) & 0x3FFF;
		vga.tandy.mem_base[addr] = val;
	}
};

extern uint8_t pc98_egc_srcmask[2]; /* host given (Neko: egc.srcmask) */
extern uint8_t pc98_egc_maskef[2]; /* effective (Neko: egc.mask2) */
extern uint8_t pc98_egc_mask[2]; /* host given (Neko: egc.mask) */
extern uint8_t pc98_egc_access;
extern uint8_t pc98_egc_fgc;
extern uint8_t pc98_egc_foreground_color;
extern uint8_t pc98_egc_background_color;
extern uint8_t pc98_egc_lead_plane;
extern uint8_t pc98_egc_compare_lead;
extern uint8_t pc98_egc_lightsource;
extern uint8_t pc98_egc_shiftinput;
extern uint8_t pc98_egc_regload;
extern uint8_t pc98_egc_rop;

extern bool pc98_egc_shift_descend;
extern uint8_t pc98_egc_shift_destbit;
extern uint8_t pc98_egc_shift_srcbit;
extern uint16_t pc98_egc_shift_length;

/* I don't think we necessarily need the full 4096 bit buffer
 * Neko Project II uses to render things, though that is
 * probably faster to execute. It makes it hard to make sense
 * of the code though. */
struct pc98_egc_shifter {
    pc98_egc_shifter() : decrement(false), remain(0x10), srcbit(0), dstbit(0) { }

    void reinit(void) { /* from global vars set by guest */
        decrement = pc98_egc_shift_descend;
        remain = pc98_egc_shift_length + 1; /* the register is length - 1 apparently */
        dstbit = pc98_egc_shift_destbit;
        srcbit = pc98_egc_shift_srcbit;
        bufi = bufo = decrement ? (sizeof(buffer) + 3 - (4*4)) : 0;

        if ((srcbit&7) < (dstbit&7)) {
            shft8bitr = (dstbit&7) - (srcbit&7);
            shft8bitl = 8 - shft8bitr;
        }
        else if ((srcbit&7) > (dstbit&7)) {
            shft8bitl = (srcbit&7) - (dstbit&7);
            shft8bitr = 8 - shft8bitl;
        }
        else {
            shft8bitr = 0;
            shft8bitl = 0;
        }

        shft8load = 0;
        o_srcbit = srcbit & 7;
        o_dstbit = dstbit & 7;
    }

    bool                decrement;
    uint16_t            remain;
    uint16_t            srcbit;
    uint16_t            dstbit;
    uint16_t            o_srcbit;
    uint16_t            o_dstbit;

    uint8_t             buffer[512]; /* 4096/8 = 512 */
    uint16_t            bufi,bufo;

    uint8_t             shft8load;
    uint8_t             shft8bitr;
    uint8_t             shft8bitl;

    template <class AWT> inline void bi(const uint16_t ofs,const AWT val) {
        size_t ip = (bufi + ofs) & (sizeof(buffer) - 1);

        for (size_t i=0;i < sizeof(AWT);) {
            buffer[ip] = (uint8_t)(val >> ((AWT)(i * 8U)));
            if ((++ip) == sizeof(buffer)) ip = 0;
            i++;
        }
    }

    template <class AWT> inline void bi_adv(void) {
        bufi += pc98_egc_shift_descend ? (sizeof(buffer) - sizeof(AWT)) : sizeof(AWT);
        bufi &= (sizeof(buffer) - 1);
    }

    template <class AWT> inline AWT bo(const uint16_t ofs) {
        size_t op = (bufo + ofs) & (sizeof(buffer) - 1);
        AWT ret = 0;

        for (size_t i=0;i < sizeof(AWT);) {
            ret += ((AWT)buffer[op]) << ((AWT)(i * 8U));
            if ((++op) == sizeof(buffer)) op = 0;
            i++;
        }

        return ret;
    }

    template <class AWT> inline void bo_adv(void) {
        bufo += pc98_egc_shift_descend ? (sizeof(buffer) - sizeof(AWT)) : sizeof(AWT);
        bufo &= (sizeof(buffer) - 1);
    }

    template <class AWT> inline void input(const AWT a,const AWT b,const AWT c,const AWT d,uint8_t odd) {
        bi<AWT>((pc98_egc_shift_descend ? (sizeof(buffer) + 1 - sizeof(AWT)) : 0) + 0,a);
        bi<AWT>((pc98_egc_shift_descend ? (sizeof(buffer) + 1 - sizeof(AWT)) : 0) + 4,b);
        bi<AWT>((pc98_egc_shift_descend ? (sizeof(buffer) + 1 - sizeof(AWT)) : 0) + 8,c);
        bi<AWT>((pc98_egc_shift_descend ? (sizeof(buffer) + 1 - sizeof(AWT)) : 0) + 12,d);

        if (shft8load <= 16) {
            bi_adv<AWT>();

            if (sizeof(AWT) == 2) {
                if (srcbit >= 8) bo_adv<uint8_t>();
                shft8load += (16 - srcbit);
                srcbit = 0;
            }
            else {
                if (srcbit >= 8)
                    srcbit -= 8;
                else {
                    shft8load += (8 - srcbit);
                    srcbit = 0;
                }
            }
        }

        *((AWT*)(pc98_egc_srcmask+odd)) = (AWT)(~0ull);
    }

    inline uint8_t dstbit_mask(void) {
        uint8_t mb;

        /* assume remain > 0 */
        if (remain >= 8)
            mb = 0xFF;
        else if (!pc98_egc_shift_descend)
            mb = 0xFF << (uint8_t)(8 - remain); /* 0x80 0xC0 0xE0 0xF0 ... */
        else
            mb = 0xFF >> (uint8_t)(8 - remain); /* 0x01 0x03 0x07 0x0F ... */

        /* assume dstbit < 8 */
        if (!pc98_egc_shift_descend)
            return mb >> (uint8_t)dstbit; /* 0xFF 0x7F 0x3F 0x1F ... */
        else
            return mb << (uint8_t)dstbit; /* 0xFF 0xFE 0xFC 0xF8 ... */
    }

    template <class AWT> inline void output(AWT &a,AWT &b,AWT &c,AWT &d,uint8_t odd,bool recursive=false) {
        if (sizeof(AWT) == 2) {
            if (shft8load < (16 - dstbit)) {
                *((AWT*)(pc98_egc_srcmask+odd)) = 0;
                return;
            }
            shft8load -= (16 - dstbit);

            /* assume odd == false and output is to even byte offset */
            if (pc98_egc_shift_descend) {
                output<uint8_t>(((uint8_t*)(&a))[1],((uint8_t*)(&b))[1],((uint8_t*)(&c))[1],((uint8_t*)(&d))[1],1,true);
                if (remain != 0) output<uint8_t>(((uint8_t*)(&a))[0],((uint8_t*)(&b))[0],((uint8_t*)(&c))[0],((uint8_t*)(&d))[0],0,true);
                else pc98_egc_srcmask[0] = 0;
            }
            else {
                output<uint8_t>(((uint8_t*)(&a))[0],((uint8_t*)(&b))[0],((uint8_t*)(&c))[0],((uint8_t*)(&d))[0],0,true);
                if (remain != 0) output<uint8_t>(((uint8_t*)(&a))[1],((uint8_t*)(&b))[1],((uint8_t*)(&c))[1],((uint8_t*)(&d))[1],1,true);
                else pc98_egc_srcmask[1] = 0;
            }

            if (remain == 0)
                reinit();

            return;
        }

        if (!recursive) {
            if (shft8load < (8 - dstbit)) {
                *((AWT*)(pc98_egc_srcmask+odd)) = 0;
                return;
            }
            shft8load -= (8 - dstbit);
        }

        if (dstbit >= 8) {
            dstbit -= 8;
            *((AWT*)(pc98_egc_srcmask+odd)) = 0;
            return;
        }

        *((AWT*)(pc98_egc_srcmask+odd)) = dstbit_mask();

        if (dstbit > 0) {
            const uint8_t bc = 8 - dstbit;

            if (remain >= bc)
                remain -= bc;
            else
                remain = 0;
        }
        else {
            if (remain >= 8)
                remain -= 8;
            else
                remain = 0;
        }

        if (o_srcbit < o_dstbit) {
            if (dstbit != 0) {
                if (pc98_egc_shift_descend) {
                    a = bo<AWT>( 0) << shft8bitr;
                    b = bo<AWT>( 4) << shft8bitr;
                    c = bo<AWT>( 8) << shft8bitr;
                    d = bo<AWT>(12) << shft8bitr;
                }
                else {
                    a = bo<AWT>( 0) >> shft8bitr;
                    b = bo<AWT>( 4) >> shft8bitr;
                    c = bo<AWT>( 8) >> shft8bitr;
                    d = bo<AWT>(12) >> shft8bitr;
                }

                dstbit = 0;
            }
            else {
                if (pc98_egc_shift_descend) {
                    bo_adv<AWT>();
                    a = (bo<AWT>( 0+1) >> shft8bitl) | (bo<AWT>( 0) << shft8bitr);
                    b = (bo<AWT>( 4+1) >> shft8bitl) | (bo<AWT>( 4) << shft8bitr);
                    c = (bo<AWT>( 8+1) >> shft8bitl) | (bo<AWT>( 8) << shft8bitr);
                    d = (bo<AWT>(12+1) >> shft8bitl) | (bo<AWT>(12) << shft8bitr);
                }
                else {
                    a = (bo<AWT>( 0) << shft8bitl) | (bo<AWT>( 0+1) >> shft8bitr);
                    b = (bo<AWT>( 4) << shft8bitl) | (bo<AWT>( 4+1) >> shft8bitr);
                    c = (bo<AWT>( 8) << shft8bitl) | (bo<AWT>( 8+1) >> shft8bitr);
                    d = (bo<AWT>(12) << shft8bitl) | (bo<AWT>(12+1) >> shft8bitr);
                    bo_adv<AWT>();
                }
            }
        }
        else if (o_srcbit > o_dstbit) {
            dstbit = 0;

            if (pc98_egc_shift_descend) {
                bo_adv<AWT>();
                a = (bo<AWT>( 0+1) >> shft8bitl) | (bo<AWT>( 0) << shft8bitr);
                b = (bo<AWT>( 4+1) >> shft8bitl) | (bo<AWT>( 4) << shft8bitr);
                c = (bo<AWT>( 8+1) >> shft8bitl) | (bo<AWT>( 8) << shft8bitr);
                d = (bo<AWT>(12+1) >> shft8bitl) | (bo<AWT>(12) << shft8bitr);
            }
            else {
                a = (bo<AWT>( 0) << shft8bitl) | (bo<AWT>( 0+1) >> shft8bitr);
                b = (bo<AWT>( 4) << shft8bitl) | (bo<AWT>( 4+1) >> shft8bitr);
                c = (bo<AWT>( 8) << shft8bitl) | (bo<AWT>( 8+1) >> shft8bitr);
                d = (bo<AWT>(12) << shft8bitl) | (bo<AWT>(12+1) >> shft8bitr);
                bo_adv<AWT>();
            }
        }
        else {
            dstbit = 0;

            a = bo<AWT>( 0);
            b = bo<AWT>( 4);
            c = bo<AWT>( 8);
            d = bo<AWT>(12);
            bo_adv<AWT>();
        }

        if (!recursive && remain == 0)
            reinit();
    }
};

egc_quad pc98_egc_src;
egc_quad pc98_egc_bgcm;
egc_quad pc98_egc_fgcm;
egc_quad pc98_egc_data;
egc_quad pc98_egc_last_vram;

pc98_egc_shifter pc98_egc_shift;

typedef egc_quad & (*PC98_OPEFN)(uint8_t ope, const PhysPt ad);

void pc98_egc_shift_reinit() {
    pc98_egc_shift.reinit();
}

static egc_quad &ope_xx(uint8_t ope, const PhysPt ad) {
    (void)ad;//UNUSED
    LOG_MSG("EGC ROP 0x%2x not impl",ope);
    return pc98_egc_last_vram;
}

static egc_quad &ope_np(uint8_t ope, const PhysPt vramoff) {
	egc_quad dst;

	dst[0].w = *((uint16_t*)(vga.mem.linear+vramoff+0x08000));
	dst[1].w = *((uint16_t*)(vga.mem.linear+vramoff+0x10000));
	dst[2].w = *((uint16_t*)(vga.mem.linear+vramoff+0x18000));
	dst[3].w = *((uint16_t*)(vga.mem.linear+vramoff+0x20000));

	pc98_egc_data[0].w = 0;
	pc98_egc_data[1].w = 0;
	pc98_egc_data[2].w = 0;
	pc98_egc_data[3].w = 0;

	if (ope & 0x80) {
        pc98_egc_data[0].w |= (pc98_egc_src[0].w & dst[0].w);
        pc98_egc_data[1].w |= (pc98_egc_src[1].w & dst[1].w);
        pc98_egc_data[2].w |= (pc98_egc_src[2].w & dst[2].w);
        pc98_egc_data[3].w |= (pc98_egc_src[3].w & dst[3].w);
    }
	if (ope & 0x20) {
        pc98_egc_data[0].w |= (pc98_egc_src[0].w & (~dst[0].w));
        pc98_egc_data[1].w |= (pc98_egc_src[1].w & (~dst[1].w));
        pc98_egc_data[2].w |= (pc98_egc_src[2].w & (~dst[2].w));
        pc98_egc_data[3].w |= (pc98_egc_src[3].w & (~dst[3].w));
	}
	if (ope & 0x08) {
        pc98_egc_data[0].w |= ((~pc98_egc_src[0].w) & dst[0].w);
        pc98_egc_data[1].w |= ((~pc98_egc_src[1].w) & dst[1].w);
        pc98_egc_data[2].w |= ((~pc98_egc_src[2].w) & dst[2].w);
        pc98_egc_data[3].w |= ((~pc98_egc_src[3].w) & dst[3].w);
	}
	if (ope & 0x02) {
        pc98_egc_data[0].w |= ((~pc98_egc_src[0].w) & (~dst[0].w));
        pc98_egc_data[1].w |= ((~pc98_egc_src[1].w) & (~dst[1].w));
        pc98_egc_data[2].w |= ((~pc98_egc_src[2].w) & (~dst[2].w));
        pc98_egc_data[3].w |= ((~pc98_egc_src[3].w) & (~dst[3].w));
	}

	(void)ope;
	(void)vramoff;
	return pc98_egc_data;
}

static egc_quad &ope_c0(uint8_t ope, const PhysPt vramoff) {
	egc_quad dst;

    /* assume: ad is word aligned */

	dst[0].w = *((uint16_t*)(vga.mem.linear+vramoff+0x08000));
	dst[1].w = *((uint16_t*)(vga.mem.linear+vramoff+0x10000));
	dst[2].w = *((uint16_t*)(vga.mem.linear+vramoff+0x18000));
	dst[3].w = *((uint16_t*)(vga.mem.linear+vramoff+0x20000));

	pc98_egc_data[0].w = pc98_egc_src[0].w & dst[0].w;
	pc98_egc_data[1].w = pc98_egc_src[1].w & dst[1].w;
	pc98_egc_data[2].w = pc98_egc_src[2].w & dst[2].w;
	pc98_egc_data[3].w = pc98_egc_src[3].w & dst[3].w;

	(void)ope;
	(void)vramoff;
	return pc98_egc_data;
}

static egc_quad &ope_f0(uint8_t ope, const PhysPt vramoff) {
	(void)ope;
	(void)vramoff;
	return pc98_egc_src;
}

static egc_quad &ope_fc(uint8_t ope, const PhysPt vramoff) {
	egc_quad dst;

    /* assume: ad is word aligned */

	dst[0].w = *((uint16_t*)(vga.mem.linear+vramoff+0x08000));
	dst[1].w = *((uint16_t*)(vga.mem.linear+vramoff+0x10000));
	dst[2].w = *((uint16_t*)(vga.mem.linear+vramoff+0x18000));
	dst[3].w = *((uint16_t*)(vga.mem.linear+vramoff+0x20000));

	pc98_egc_data[0].w  =    pc98_egc_src[0].w;
	pc98_egc_data[0].w |= ((~pc98_egc_src[0].w) & dst[0].w);
	pc98_egc_data[1].w  =    pc98_egc_src[1].w;
	pc98_egc_data[1].w |= ((~pc98_egc_src[1].w) & dst[1].w);
	pc98_egc_data[2].w  =    pc98_egc_src[2].w;
	pc98_egc_data[2].w |= ((~pc98_egc_src[2].w) & dst[2].w);
	pc98_egc_data[3].w  =    pc98_egc_src[3].w;
	pc98_egc_data[3].w |= ((~pc98_egc_src[3].w) & dst[3].w);

	(void)ope;
	(void)vramoff;
	return pc98_egc_data;
}

static egc_quad &ope_gg(uint8_t ope, const PhysPt vramoff) {
    egc_quad pat,dst;

	switch(pc98_egc_fgc) {
		case 1:
			pat[0].w = pc98_egc_bgcm[0].w;
			pat[1].w = pc98_egc_bgcm[1].w;
			pat[2].w = pc98_egc_bgcm[2].w;
			pat[3].w = pc98_egc_bgcm[3].w;
			break;

		case 2:
			pat[0].w = pc98_egc_fgcm[0].w;
			pat[1].w = pc98_egc_fgcm[1].w;
			pat[2].w = pc98_egc_fgcm[2].w;
			pat[3].w = pc98_egc_fgcm[3].w;
			break;

		default:
			if (pc98_egc_regload & 1) {
				pat[0].w = pc98_egc_src[0].w;
				pat[1].w = pc98_egc_src[1].w;
				pat[2].w = pc98_egc_src[2].w;
				pat[3].w = pc98_egc_src[3].w;
			}
			else {
				pat[0].w = pc98_gdc_tiles[0].w;
				pat[1].w = pc98_gdc_tiles[1].w;
				pat[2].w = pc98_gdc_tiles[2].w;
				pat[3].w = pc98_gdc_tiles[3].w;
			}
			break;
	}

	dst[0].w = *((uint16_t*)(vga.mem.linear+vramoff+0x08000));
	dst[1].w = *((uint16_t*)(vga.mem.linear+vramoff+0x10000));
	dst[2].w = *((uint16_t*)(vga.mem.linear+vramoff+0x18000));
	dst[3].w = *((uint16_t*)(vga.mem.linear+vramoff+0x20000));

	pc98_egc_data[0].w = 0;
	pc98_egc_data[1].w = 0;
	pc98_egc_data[2].w = 0;
	pc98_egc_data[3].w = 0;

	if (ope & 0x80) {
		pc98_egc_data[0].w |=  ( pat[0].w  &   pc98_egc_src[0].w &    dst[0].w);
		pc98_egc_data[1].w |=  ( pat[1].w  &   pc98_egc_src[1].w &    dst[1].w);
		pc98_egc_data[2].w |=  ( pat[2].w  &   pc98_egc_src[2].w &    dst[2].w);
		pc98_egc_data[3].w |=  ( pat[3].w  &   pc98_egc_src[3].w &    dst[3].w);
	}
	if (ope & 0x40) {
		pc98_egc_data[0].w |= ((~pat[0].w) &   pc98_egc_src[0].w &    dst[0].w);
		pc98_egc_data[1].w |= ((~pat[1].w) &   pc98_egc_src[1].w &    dst[1].w);
		pc98_egc_data[2].w |= ((~pat[2].w) &   pc98_egc_src[2].w &    dst[2].w);
		pc98_egc_data[3].w |= ((~pat[3].w) &   pc98_egc_src[3].w &    dst[3].w);
	}
	if (ope & 0x20) {
		pc98_egc_data[0].w |= (  pat[0].w  &   pc98_egc_src[0].w &  (~dst[0].w));
		pc98_egc_data[1].w |= (  pat[1].w  &   pc98_egc_src[1].w &  (~dst[1].w));
		pc98_egc_data[2].w |= (  pat[2].w  &   pc98_egc_src[2].w &  (~dst[2].w));
		pc98_egc_data[3].w |= (  pat[3].w  &   pc98_egc_src[3].w &  (~dst[3].w));
	}
	if (ope & 0x10) {
		pc98_egc_data[0].w |= ((~pat[0].w) &   pc98_egc_src[0].w &  (~dst[0].w));
		pc98_egc_data[1].w |= ((~pat[1].w) &   pc98_egc_src[1].w &  (~dst[1].w));
		pc98_egc_data[2].w |= ((~pat[2].w) &   pc98_egc_src[2].w &  (~dst[2].w));
		pc98_egc_data[3].w |= ((~pat[3].w) &   pc98_egc_src[3].w &  (~dst[3].w));
	}
	if (ope & 0x08) {
		pc98_egc_data[0].w |= (  pat[0].w  & (~pc98_egc_src[0].w) &   dst[0].w);
		pc98_egc_data[1].w |= (  pat[1].w  & (~pc98_egc_src[1].w) &   dst[1].w);
		pc98_egc_data[2].w |= (  pat[2].w  & (~pc98_egc_src[2].w) &   dst[2].w);
		pc98_egc_data[3].w |= (  pat[3].w  & (~pc98_egc_src[3].w) &   dst[3].w);
	}
	if (ope & 0x04) {
		pc98_egc_data[0].w |= ((~pat[0].w) & (~pc98_egc_src[0].w) &   dst[0].w);
		pc98_egc_data[1].w |= ((~pat[1].w) & (~pc98_egc_src[1].w) &   dst[1].w);
		pc98_egc_data[2].w |= ((~pat[2].w) & (~pc98_egc_src[2].w) &   dst[2].w);
		pc98_egc_data[3].w |= ((~pat[3].w) & (~pc98_egc_src[3].w) &   dst[3].w);
	}
	if (ope & 0x02) {
		pc98_egc_data[0].w |= (  pat[0].w  & (~pc98_egc_src[0].w) & (~dst[0].w));
		pc98_egc_data[1].w |= (  pat[1].w  & (~pc98_egc_src[1].w) & (~dst[1].w));
		pc98_egc_data[2].w |= (  pat[2].w  & (~pc98_egc_src[2].w) & (~dst[2].w));
		pc98_egc_data[3].w |= (  pat[3].w  & (~pc98_egc_src[3].w) & (~dst[3].w));
	}
	if (ope & 0x01) {
		pc98_egc_data[0].w |= ((~pat[0].w) & (~pc98_egc_src[0].w) & (~dst[0].w));
		pc98_egc_data[1].w |= ((~pat[1].w) & (~pc98_egc_src[1].w) & (~dst[1].w));
		pc98_egc_data[2].w |= ((~pat[2].w) & (~pc98_egc_src[2].w) & (~dst[2].w));
		pc98_egc_data[3].w |= ((~pat[3].w) & (~pc98_egc_src[3].w) & (~dst[3].w));
	}

	return pc98_egc_data;
}

static const PC98_OPEFN pc98_egc_opfn[256] = {
			ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx,
			ope_xx, ope_xx, ope_xx, ope_xx, ope_np, ope_xx, ope_xx, ope_xx,
			ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx,
			ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx,
			ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx,
			ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx,
			ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx,
			ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx,
			ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx,
			ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx,
			ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx,
			ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx,
			ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx,
			ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx,
			ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx,
			ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx,
			ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx,
			ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx,
			ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx,
			ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx,
			ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx,
			ope_xx, ope_xx, ope_xx, ope_xx, ope_gg, ope_xx, ope_xx, ope_xx,
			ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx,
			ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx,
			ope_c0, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx,
			ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx,
			ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx,
			ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx,
			ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx,
			ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx,
			ope_f0, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx, ope_xx,
			ope_xx, ope_xx, ope_xx, ope_xx, ope_fc, ope_xx, ope_xx, ope_xx};

template <class AWT> static egc_quad &egc_ope(const PhysPt vramoff, const AWT val) {
    *((uint16_t*)pc98_egc_maskef) = *((uint16_t*)pc98_egc_mask);

    /* 4A4h
     * bits [12:11] = light source
     *    11 = invalid
     *    10 = write the contents of the palette register
     *    01 = write the result of the raster operation
     *    00 = write CPU data
     *
     * 4A2h
     * bits [14:13] = foreground, background color
     *    11 = invalid
     *    10 = foreground color
     *    01 = background color
     *    00 = pattern register
     */
    switch (pc98_egc_lightsource) {
        case 1: /* 0x0800 */
            if (pc98_egc_shiftinput) {
                pc98_egc_shift.input<AWT>(
                    val,
                    val,
                    val,
                    val,
                    vramoff&1);

                pc98_egc_shift.output<AWT>(
                    *((AWT*)(pc98_egc_src[0].b+(vramoff&1))),
                    *((AWT*)(pc98_egc_src[1].b+(vramoff&1))),
                    *((AWT*)(pc98_egc_src[2].b+(vramoff&1))),
                    *((AWT*)(pc98_egc_src[3].b+(vramoff&1))),
                    vramoff&1);
            }

            *((uint16_t*)pc98_egc_maskef) &= *((uint16_t*)pc98_egc_srcmask);
            return pc98_egc_opfn[pc98_egc_rop](pc98_egc_rop, vramoff & (~1U));
        case 2: /* 0x1000 */
            if (pc98_egc_fgc == 1)
                return pc98_egc_bgcm;
            else if (pc98_egc_fgc == 2)
                return pc98_egc_fgcm;

            if (pc98_egc_shiftinput) {
                pc98_egc_shift.input<AWT>(
                    val,
                    val,
                    val,
                    val,
                    vramoff&1);

                pc98_egc_shift.output<AWT>(
                    *((AWT*)(pc98_egc_src[0].b+(vramoff&1))),
                    *((AWT*)(pc98_egc_src[1].b+(vramoff&1))),
                    *((AWT*)(pc98_egc_src[2].b+(vramoff&1))),
                    *((AWT*)(pc98_egc_src[3].b+(vramoff&1))),
                    vramoff&1);
            }
 
            *((uint16_t*)pc98_egc_maskef) &= *((uint16_t*)pc98_egc_srcmask);
            return pc98_egc_src;
        default: {
            uint16_t tmp = (uint16_t)val;

            if (sizeof(AWT) < 2) {
                tmp &= 0xFFU;
                tmp |= tmp << 8U;
            }

            pc98_egc_data[0].w = tmp;
            pc98_egc_data[1].w = tmp;
            pc98_egc_data[2].w = tmp;
            pc98_egc_data[3].w = tmp;
            } break;
    };

    return pc98_egc_data;
}

unsigned char pc98_mem_msw_m[8] = {0};

void pc98_msw3_set_ramsize(const unsigned char b) {
    pc98_mem_msw_m[2/*MSW3*/] = b;
}

unsigned char pc98_mem_msw(unsigned char which) {
    return pc98_mem_msw_m[which&7];
}

/* The NEC display is documented to have:
 *
 * A0000-A3FFF      T-RAM (text) (8KB WORDs)
 *   A0000-A1FFF      Characters (4KB WORDs)
 *   A2000-A3FFF      Attributes (4KB WORDs). For each 16-bit WORD only the lower 8 bits are read/writeable.
 *   A4000-A5FFF      Unknown ?? (4KB WORDs)
 *   A6000-A7FFF      Not present (4KB WORDs)
 * A8000-BFFFF      G-RAM (graphics) (96KB)
 *
 * T-RAM character display RAM is 16-bits per character.
 * ASCII text has upper 8 bits zero.
 * SHIFT-JIS doublewide characters use the upper byte for non-ASCII. */

class VGA_PC98_PageHandler : public PageHandler {
public:
	VGA_PC98_PageHandler() : PageHandler(PFLAG_NOCODE) {}
    template <class AWT> static inline void check_align(const PhysPt addr) {
        /* DEBUG: address must be aligned to datatype.
         *        Code that calls us must enforce that or subdivide
         *        to a small datatype that can follow this rule. */
        PhysPt chk = (1UL << (sizeof(AWT) - 1)) - 1;
        /* uint8_t:  chk = 0
         * uint16_t: chk = 1
         * TODO: Do you suppose later generation PC-9821's supported DWORD size bitplane transfers?
         *       Or did NEC just give up on anything past 16-bit and focus on the SVGA side of things? */
        assert((addr&chk) == 0);
    }

    template <class AWT> static inline AWT mode8_r(const unsigned int plane,const PhysPt vramoff) {
        AWT r,b;

        b = *((AWT*)(vga.mem.linear + vramoff));
        r = b ^ *((AWT*)pc98_gdc_tiles[plane].b);

        return r;
    }

    template <class AWT> static inline void mode8_w(const unsigned int plane,const PhysPt vramoff) {
        /* Neko Project II code suggests that the first byte is repeated. */
        if (sizeof(AWT) > 1)
            pc98_gdc_tiles[plane].b[1] = pc98_gdc_tiles[plane].b[0];

        *((AWT*)(vga.mem.linear + vramoff)) = *((AWT*)pc98_gdc_tiles[plane].b);
    }

    template <class AWT> static inline void modeC_w(const unsigned int plane,const PhysPt vramoff,const AWT mask,const AWT val) {
        AWT t;

        /* Neko Project II code suggests that the first byte is repeated. */
        if (sizeof(AWT) > 1)
            pc98_gdc_tiles[plane].b[1] = pc98_gdc_tiles[plane].b[0];

        t  = *((AWT*)(vga.mem.linear + vramoff)) & mask;
        t |= val & *((AWT*)pc98_gdc_tiles[plane].b);
        *((AWT*)(vga.mem.linear + vramoff)) = t;
    }

    template <class AWT> static inline AWT modeEGC_r(const PhysPt vramoff,const PhysPt fulloff) {
        /* assume: vramoff is even IF AWT is 16-bit wide */
        *((AWT*)(pc98_egc_last_vram[0].b+(vramoff&1))) = *((AWT*)(vga.mem.linear+vramoff+0x08000));
        *((AWT*)(pc98_egc_last_vram[1].b+(vramoff&1))) = *((AWT*)(vga.mem.linear+vramoff+0x10000));
        *((AWT*)(pc98_egc_last_vram[2].b+(vramoff&1))) = *((AWT*)(vga.mem.linear+vramoff+0x18000));
        *((AWT*)(pc98_egc_last_vram[3].b+(vramoff&1))) = *((AWT*)(vga.mem.linear+vramoff+0x20000));

        /* bits [10:10] = read source
         *    1 = shifter input is CPU write data
         *    0 = shifter input is VRAM data */
        /* Neko Project II: if ((egc.ope & 0x0400) == 0) ... */
        if (!pc98_egc_shiftinput) {
            pc98_egc_shift.input<AWT>(
                *((AWT*)(pc98_egc_last_vram[0].b+(vramoff&1))),
                *((AWT*)(pc98_egc_last_vram[1].b+(vramoff&1))),
                *((AWT*)(pc98_egc_last_vram[2].b+(vramoff&1))),
                *((AWT*)(pc98_egc_last_vram[3].b+(vramoff&1))),
                vramoff&1);

            pc98_egc_shift.output<AWT>(
                *((AWT*)(pc98_egc_src[0].b+(vramoff&1))),
                *((AWT*)(pc98_egc_src[1].b+(vramoff&1))),
                *((AWT*)(pc98_egc_src[2].b+(vramoff&1))),
                *((AWT*)(pc98_egc_src[3].b+(vramoff&1))),
                vramoff&1);
        }

        /* 0x4A4:
         * ...
         * bits [9:8] = register load (pc98_egc_regload[1:0])
         *    11 = invalid
         *    10 = load VRAM data before writing on VRAM write
         *    01 = load VRAM data into pattern/tile register on VRAM read
         *    00 = Do not change pattern/tile register
         * ...
         *
         * pc98_egc_regload = (val >> 8) & 3;
         */
        /* Neko Project II: if ((egc.ope & 0x0300) == 0x0100) ... */
        if (pc98_egc_regload & 1) { /* load VRAM data into pattern/tile... (or INVALID) */
            *((AWT*)(pc98_gdc_tiles[0].b+(vramoff&1))) = *((AWT*)(pc98_egc_last_vram[0].b+(vramoff&1)));
            *((AWT*)(pc98_gdc_tiles[1].b+(vramoff&1))) = *((AWT*)(pc98_egc_last_vram[1].b+(vramoff&1)));
            *((AWT*)(pc98_gdc_tiles[2].b+(vramoff&1))) = *((AWT*)(pc98_egc_last_vram[2].b+(vramoff&1)));
            *((AWT*)(pc98_gdc_tiles[3].b+(vramoff&1))) = *((AWT*)(pc98_egc_last_vram[3].b+(vramoff&1)));
        }

        /* 0x4A4:
         * bits [13:13] = 0=compare lead plane  1=don't
         *
         * bits [10:10] = read source
         *    1 = shifter input is CPU write data
         *    0 = shifter input is VRAM data */
        if (pc98_egc_compare_lead) {
            if (!pc98_egc_shiftinput)
                return *((AWT*)(pc98_egc_src[pc98_egc_lead_plane&3].b));
            else
                return *((AWT*)(vga.mem.linear+vramoff+0x08000+((pc98_egc_lead_plane&3)*0x8000)));
        }

        return *((AWT*)(vga.mem.linear+fulloff));
    }

    template <class AWT> static inline void modeEGC_w(const PhysPt vramoff,const PhysPt fulloff,const AWT val) {
        (void)fulloff;//UNUSED

        /* assume: vramoff is even IF AWT is 16-bit wide */

        /* 0x4A4:
         * ...
         * bits [9:8] = register load (pc98_egc_regload[1:0])
         *    11 = invalid
         *    10 = load VRAM data before writing on VRAM write
         *    01 = load VRAM data into pattern/tile register on VRAM read
         *    00 = Do not change pattern/tile register
         * ...
         * pc98_egc_regload = (val >> 8) & 3;
         */
        /* Neko Project II: if ((egc.ope & 0x0300) == 0x0200) ... */
        if (pc98_egc_regload & 2) { /* load VRAM data before writing on VRAM write (or INVALID) */
            *((AWT*)(pc98_gdc_tiles[0].b+(vramoff&1))) = *((AWT*)(vga.mem.linear+vramoff+0x08000));
            *((AWT*)(pc98_gdc_tiles[1].b+(vramoff&1))) = *((AWT*)(vga.mem.linear+vramoff+0x10000));
            *((AWT*)(pc98_gdc_tiles[2].b+(vramoff&1))) = *((AWT*)(vga.mem.linear+vramoff+0x18000));
            *((AWT*)(pc98_gdc_tiles[3].b+(vramoff&1))) = *((AWT*)(vga.mem.linear+vramoff+0x20000));
        }

        egc_quad &ropdata = egc_ope<AWT>(vramoff, val);

        const AWT accmask = *((AWT*)(pc98_egc_maskef+(vramoff&1)));

        if (accmask != 0) {
            if (!(pc98_egc_access & 1)) {
                *((AWT*)(vga.mem.linear+vramoff+0x08000)) &= ~accmask;
                *((AWT*)(vga.mem.linear+vramoff+0x08000)) |=  accmask & *((AWT*)(ropdata[0].b+(vramoff&1)));
            }
            if (!(pc98_egc_access & 2)) {
                *((AWT*)(vga.mem.linear+vramoff+0x10000)) &= ~accmask;
                *((AWT*)(vga.mem.linear+vramoff+0x10000)) |=  accmask & *((AWT*)(ropdata[1].b+(vramoff&1)));
            }
            if (!(pc98_egc_access & 4)) {
                *((AWT*)(vga.mem.linear+vramoff+0x18000)) &= ~accmask;
                *((AWT*)(vga.mem.linear+vramoff+0x18000)) |=  accmask & *((AWT*)(ropdata[2].b+(vramoff&1)));
            }
            if (!(pc98_egc_access & 8)) {
                *((AWT*)(vga.mem.linear+vramoff+0x20000)) &= ~accmask;
                *((AWT*)(vga.mem.linear+vramoff+0x20000)) |=  accmask & *((AWT*)(ropdata[3].b+(vramoff&1)));
            }
        }
    }

    template <class AWT> AWT readc(PhysPt addr) {
        unsigned int vop_offset = 0;

		addr = PAGING_GetPhysicalAddress(addr);

        check_align<AWT>(addr);

        if ((addr & (~0x1F)) == 0xA3FE0) {
            /* 
             * 0xA3FE2      MSW1
             * 0xA3FE6      MSW2
             * 0xA3FEA      MSW3
             * 0xA3FEE      MSW4
             * 0xA3FF2      MSW5
             * 0xA3FF6      MSW6
             * 0xA3FFA      MSW7
             * 0xA3FFE      MSW8
             */
            return pc98_mem_msw((addr >> 2) & 7);
        }

        if (addr >= 0xE0000) /* the 4th bitplane (EGC 16-color mode) */
            addr = (addr & 0x7FFF) + 0x20000;
        else
            addr &= 0x1FFFF;

        switch (addr>>13) {
            case 0:     /* A0000-A1FFF Character RAM */
                return *((AWT*)(vga.mem.linear+addr));
            case 1:     /* A2000-A3FFF Attribute RAM */
                if (addr & 1) return (AWT)(~0ull); /* ignore odd bytes */
                return *((AWT*)(vga.mem.linear+addr)) | 0xFF00; /* odd bytes 0xFF */
            case 2:     /* A4000-A5FFF Unknown ?? */
                return *((AWT*)(vga.mem.linear+addr));
            case 3:     /* A6000-A7FFF Not present */
                return (AWT)(~0ull);
            default:    /* A8000-BFFFF G-RAM */
                vop_offset = (pc98_gdc_vramop & (1 << VOPBIT_ACCESS)) ? 0x20000 : 0;
                break;
        };

        switch (pc98_gdc_vramop & 0xF) {
            case 0x00:
            case 0x01:
            case 0x02:
            case 0x03:
            case 0x04:
            case 0x05:
            case 0x06:
            case 0x07:
                return *((AWT*)(vga.mem.linear+addr+vop_offset));
            case 0x08: /* TCR/TDW */
            case 0x09:
                {
                    AWT r = 0;

                    /* this reads multiple bitplanes at once */
                    addr &= 0x7FFF;

                    if (!(pc98_gdc_modereg & 1)) // blue channel
                        r |= mode8_r<AWT>(/*plane*/0,addr + 0x8000 + vop_offset);

                    if (!(pc98_gdc_modereg & 2)) // red channel
                        r |= mode8_r<AWT>(/*plane*/1,addr + 0x10000 + vop_offset);

                    if (!(pc98_gdc_modereg & 4)) // green channel
                        r |= mode8_r<AWT>(/*plane*/2,addr + 0x18000 + vop_offset);

                    if (!(pc98_gdc_modereg & 8)) // extended channel
                        r |= mode8_r<AWT>(/*plane*/3,addr + 0x20000 + vop_offset);

                    /* NTS: Apparently returning this value correctly really matters to the
                     *      sprite engine in "Edge", else visual errors occur. */
                    return ~r;
                }
            case 0x0C:
            case 0x0D:
                return *((AWT*)(vga.mem.linear+addr+vop_offset));
            case 0x0A: /* EGC read */
            case 0x0B:
            case 0x0E:
            case 0x0F:
                /* this reads multiple bitplanes at once */
                return modeEGC_r<AWT>((addr&0x7FFF) + vop_offset,addr + vop_offset);
            default: /* should not happen */
                LOG_MSG("PC-98 VRAM read warning: Unsupported opmode 0x%X",pc98_gdc_vramop);
                return *((AWT*)(vga.mem.linear+addr+vop_offset));
        };

		return (AWT)(~0ull);
	}

	template <class AWT> void writec(PhysPt addr,AWT val){
        unsigned int vop_offset = 0;

		addr = PAGING_GetPhysicalAddress(addr);

        check_align<AWT>(addr);

        if ((addr & (~0x1F)) == 0xA3FE0)
            return;

        if (addr >= 0xE0000) /* the 4th bitplane (EGC 16-color mode) */
            addr = (addr & 0x7FFF) + 0x20000;
        else
            addr &= 0x1FFFF;

        switch (addr>>13) {
            case 0:     /* A0000-A1FFF Character RAM */
                *((AWT*)(vga.mem.linear+addr)) = val;
                return;
            case 1:     /* A2000-A3FFF Attribute RAM */
                if (addr & 1) return; /* ignore odd bytes */
                *((AWT*)(vga.mem.linear+addr)) = val | 0xFF00;
                return;
            case 2:     /* A4000-A5FFF Unknown ?? */
                *((AWT*)(vga.mem.linear+addr)) = val;
                return;
            case 3:     /* A6000-A7FFF Not present */
                return;
            default:    /* A8000-BFFFF G-RAM */
                vop_offset = (pc98_gdc_vramop & (1 << VOPBIT_ACCESS)) ? 0x20000 : 0;
                break;
        };

        switch (pc98_gdc_vramop & 0xF) {
            case 0x00:
            case 0x01:
            case 0x02:
            case 0x03:
            case 0x04:
            case 0x05:
            case 0x06:
            case 0x07:
                *((AWT*)(vga.mem.linear+addr+vop_offset)) = val;
                break;
            case 0x08:  /* TCR/TDW write tile data, no masking */
            case 0x09:
                {
                    /* this writes to multiple bitplanes at once.
                     * notice that the value written has no meaning, only the tile data and memory address. */
                    addr &= 0x7FFF;

                    if (!(pc98_gdc_modereg & 1)) // blue channel
                        mode8_w<AWT>(0/*plane*/,addr + 0x8000 + vop_offset);

                    if (!(pc98_gdc_modereg & 2)) // red channel
                        mode8_w<AWT>(1/*plane*/,addr + 0x10000 + vop_offset);

                    if (!(pc98_gdc_modereg & 4)) // green channel
                        mode8_w<AWT>(2/*plane*/,addr + 0x18000 + vop_offset);

                    if (!(pc98_gdc_modereg & 8)) // extended channel
                        mode8_w<AWT>(3/*plane*/,addr + 0x20000 + vop_offset);
                }
                break;
            case 0x0C:  /* read/modify/write from tile with masking */
            case 0x0D:  /* a lot of PC-98 games seem to rely on this for sprite rendering */
                {
                    const AWT mask = ~val;

                    /* this writes to multiple bitplanes at once */
                    addr &= 0x7FFF;

                    if (!(pc98_gdc_modereg & 1)) // blue channel
                        modeC_w<AWT>(0/*plane*/,addr + 0x8000 + vop_offset,mask,val);

                    if (!(pc98_gdc_modereg & 2)) // red channel
                        modeC_w<AWT>(1/*plane*/,addr + 0x10000 + vop_offset,mask,val);

                    if (!(pc98_gdc_modereg & 4)) // green channel
                        modeC_w<AWT>(2/*plane*/,addr + 0x18000 + vop_offset,mask,val);

                    if (!(pc98_gdc_modereg & 8)) // extended channel
                        modeC_w<AWT>(3/*plane*/,addr + 0x20000 + vop_offset,mask,val);
                }
                break;
            case 0x0A: /* EGC write */
            case 0x0B:
            case 0x0E:
            case 0x0F:
                /* this reads multiple bitplanes at once */
                modeEGC_w<AWT>((addr&0x7FFF) + vop_offset,addr + vop_offset,val);
                break;
            default: /* Should no longer happen */
                LOG_MSG("PC-98 VRAM write warning: Unsupported opmode 0x%X",pc98_gdc_vramop);
                *((AWT*)(vga.mem.linear+addr+vop_offset)) = val;
                break;
        };
	}

    /* byte-wise */
	Bitu readb(PhysPt addr) {
        return readc<uint8_t>(addr);
    }
	void writeb(PhysPt addr,Bitu val) {
        writec<uint8_t>(addr,(uint8_t)val);
    }

    /* word-wise.
     * in the style of the 8086, non-word-aligned I/O is split into byte I/O */
	Bitu readw(PhysPt addr) {
        if (!(addr & 1)) /* if WORD aligned */
            return readc<uint16_t>(addr);
        else {
            return   (unsigned int)readc<uint8_t>(addr+0U) +
                    ((unsigned int)readc<uint8_t>(addr+1U) << 8u);
        }
    }
	void writew(PhysPt addr,Bitu val) {
        if (!(addr & 1)) /* if WORD aligned */
            writec<uint16_t>(addr,(uint16_t)val);
        else {
            writec<uint8_t>(addr+0,(uint8_t)val);
            writec<uint8_t>(addr+1,(uint8_t)(val >> 8U));
        }
    }
};

class VGA_TEXT_PageHandler : public PageHandler {
public:
	VGA_TEXT_PageHandler() : PageHandler(PFLAG_NOCODE) {}
	Bitu readb(PhysPt addr) {
		unsigned char bplane;

		VGAMEM_USEC_read_delay();

		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		bplane = vga.gfx.read_map_select;

		if (!(vga.seq.memory_mode&4))
			bplane = (bplane & ~1u) + (addr & 1u); /* FIXME: Is this what VGA cards do? It makes sense to me */
		if (vga.gfx.miscellaneous&2) /* Odd/Even mode */
			addr &= ~1u;

		return vga.mem.linear[CHECKED3(vga.svga.bank_read_full+(addr<<2)+bplane)];
	}
	void writeb(PhysPt addr,Bitu val){
		VGA_Latch pixels;
		Bitu memaddr;

		VGAMEM_USEC_write_delay();

		addr = PAGING_GetPhysicalAddress(addr) & vgapages.mask;
		memaddr = addr;

		/* Chain Odd/Even enable: A0 is replaced by a "higher order bit" (0 apparently) */
		if (vga.gfx.miscellaneous&2)
			memaddr &= ~1u;

		pixels.d=((Bit32u*)vga.mem.linear)[memaddr];

		if ((vga.seq.memory_mode&4)/*Odd/Even disable*/ || (addr & 1)) {
			if (vga.seq.map_mask & 0x2) /* bitplane 1: attribute RAM */
				pixels.b[1] = val;
			if (vga.seq.map_mask & 0x8) /* bitplane 3: unused RAM */
				pixels.b[3] = val;
		}
		if ((vga.seq.memory_mode&4)/*Odd/Even disable*/ || !(addr & 1)) {
			if (vga.seq.map_mask & 0x1) /* bitplane 0: character RAM */
				pixels.b[0] = val;
			if (vga.seq.map_mask & 0x4) { /* bitplane 2: font RAM */
				pixels.b[2] = val;
				vga.draw.font[memaddr] = val;
			}
		}

		((Bit32u*)vga.mem.linear)[memaddr]=pixels.d;
	}
};

class VGA_Map_Handler : public PageHandler {
public:
	VGA_Map_Handler() : PageHandler(PFLAG_READABLE|PFLAG_WRITEABLE|PFLAG_NOCODE) {}
	HostPt GetHostReadPt(Bitu phys_page) {
 		phys_page-=vgapages.base;
		return &vga.mem.linear[CHECKED3(vga.svga.bank_read_full+phys_page*4096)];
	}
	HostPt GetHostWritePt(Bitu phys_page) {
 		phys_page-=vgapages.base;
		return &vga.mem.linear[CHECKED3(vga.svga.bank_write_full+phys_page*4096)];
	}
};

class VGA_Slow_CGA_Handler : public PageHandler {
public:
	VGA_Slow_CGA_Handler() : PageHandler(PFLAG_NOCODE) {}
	void delay() {
		Bits delaycyc = CPU_CycleMax/((Bit32u)(1024/2.80)); 
		if(GCC_UNLIKELY(CPU_Cycles < 3*delaycyc)) delaycyc=0;
		CPU_Cycles -= delaycyc;
		CPU_IODelayRemoved += delaycyc;
	}

	Bitu readb(PhysPt addr) {
		delay();
		return vga.tandy.mem_base[addr - 0xb8000];
	}
	void writeb(PhysPt addr,Bitu val){
		delay();
		vga.tandy.mem_base[addr - 0xb8000] = (Bit8u) val;
	}
	
};

class VGA_LIN4_Handler : public VGA_UnchainedEGA_Handler {
public:
	VGA_LIN4_Handler() : VGA_UnchainedEGA_Handler(PFLAG_NOCODE) {}
	void writeb(PhysPt addr,Bitu val) {
		VGAMEM_USEC_write_delay();
		addr = vga.svga.bank_write_full + (PAGING_GetPhysicalAddress(addr) & 0xffff);
		addr = CHECKED4(addr);
		writeHandler<false>(addr+0,(Bit8u)(val >> 0));
	}
	void writew(PhysPt addr,Bitu val) {
		VGAMEM_USEC_write_delay();
		addr = vga.svga.bank_write_full + (PAGING_GetPhysicalAddress(addr) & 0xffff);
		addr = CHECKED4(addr);
		writeHandler<false>(addr+0,(Bit8u)(val >> 0));
		writeHandler<false>(addr+1,(Bit8u)(val >> 8));
	}
	void writed(PhysPt addr,Bitu val) {
		VGAMEM_USEC_write_delay();
		addr = vga.svga.bank_write_full + (PAGING_GetPhysicalAddress(addr) & 0xffff);
		addr = CHECKED4(addr);
		writeHandler<false>(addr+0,(Bit8u)(val >> 0));
		writeHandler<false>(addr+1,(Bit8u)(val >> 8));
		writeHandler<false>(addr+2,(Bit8u)(val >> 16));
		writeHandler<false>(addr+3,(Bit8u)(val >> 24));
	}
	Bitu readb(PhysPt addr) {
		VGAMEM_USEC_read_delay();
		addr = vga.svga.bank_read_full + (PAGING_GetPhysicalAddress(addr) & 0xffff);
		addr = CHECKED4(addr);
		return readHandler(addr);
	}
	Bitu readw(PhysPt addr) {
		VGAMEM_USEC_read_delay();
		addr = vga.svga.bank_read_full + (PAGING_GetPhysicalAddress(addr) & 0xffff);
		addr = CHECKED4(addr);
		Bitu ret = (readHandler(addr+0) << 0);
		ret     |= (readHandler(addr+1) << 8);
		return ret;
	}
	Bitu readd(PhysPt addr) {
		VGAMEM_USEC_read_delay();
		addr = vga.svga.bank_read_full + (PAGING_GetPhysicalAddress(addr) & 0xffff);
		addr = CHECKED4(addr);
		Bitu ret = (readHandler(addr+0) << 0);
		ret     |= (readHandler(addr+1) << 8);
		ret     |= (readHandler(addr+2) << 16);
		ret     |= (readHandler(addr+3) << 24);
		return ret;
	}
};

class VGA_LFB_Handler : public PageHandler {
public:
	VGA_LFB_Handler() : PageHandler(PFLAG_READABLE|PFLAG_WRITEABLE|PFLAG_NOCODE) {}
	HostPt GetHostReadPt( Bitu phys_page ) {
		phys_page -= vga.lfb.page;
		phys_page &= (vga.vmemsize >> 12) - 1;
		return &vga.mem.linear[CHECKED3(phys_page * 4096)];
	}
	HostPt GetHostWritePt( Bitu phys_page ) {
		return GetHostReadPt( phys_page );
	}
};

extern void XGA_Write(Bitu port, Bitu val, Bitu len);
extern Bitu XGA_Read(Bitu port, Bitu len);

class VGA_MMIO_Handler : public PageHandler {
public:
	VGA_MMIO_Handler() : PageHandler(PFLAG_NOCODE) {}
	void writeb(PhysPt addr,Bitu val) {
		VGAMEM_USEC_write_delay();
		Bitu port = PAGING_GetPhysicalAddress(addr) & 0xffff;
		XGA_Write(port, val, 1);
	}
	void writew(PhysPt addr,Bitu val) {
		VGAMEM_USEC_write_delay();
		Bitu port = PAGING_GetPhysicalAddress(addr) & 0xffff;
		XGA_Write(port, val, 2);
	}
	void writed(PhysPt addr,Bitu val) {
		VGAMEM_USEC_write_delay();
		Bitu port = PAGING_GetPhysicalAddress(addr) & 0xffff;
		XGA_Write(port, val, 4);
	}

	Bitu readb(PhysPt addr) {
		VGAMEM_USEC_read_delay();
		Bitu port = PAGING_GetPhysicalAddress(addr) & 0xffff;
		return XGA_Read(port, 1);
	}
	Bitu readw(PhysPt addr) {
		VGAMEM_USEC_read_delay();
		Bitu port = PAGING_GetPhysicalAddress(addr) & 0xffff;
		return XGA_Read(port, 2);
	}
	Bitu readd(PhysPt addr) {
		VGAMEM_USEC_read_delay();
		Bitu port = PAGING_GetPhysicalAddress(addr) & 0xffff;
		return XGA_Read(port, 4);
	}
};

class VGA_TANDY_PageHandler : public PageHandler {
public:
	VGA_TANDY_PageHandler() : PageHandler(PFLAG_READABLE|PFLAG_WRITEABLE) {}
	HostPt GetHostReadPt(Bitu phys_page) {
		// Odd banks are limited to 16kB and repeated
		if (vga.tandy.mem_bank & 1) 
			phys_page&=0x03;
		else 
			phys_page&=0x07;
		return vga.tandy.mem_base + (phys_page * 4096);
	}
	HostPt GetHostWritePt(Bitu phys_page) {
		return GetHostReadPt( phys_page );
	}
};


class VGA_PCJR_Handler : public PageHandler {
public:
	VGA_PCJR_Handler() : PageHandler(PFLAG_READABLE|PFLAG_WRITEABLE) {}
	HostPt GetHostReadPt(Bitu phys_page) {
		phys_page-=0xb8;
		// The 16kB map area is repeated in the 32kB range
		// On CGA CPU A14 is not decoded so it repeats there too
		phys_page&=0x03;
		return vga.tandy.mem_base + (phys_page * 4096);
	}
	HostPt GetHostWritePt(Bitu phys_page) {
		return GetHostReadPt( phys_page );
	}
};

class VGA_AMS_Handler : public PageHandler {
public:
	template< bool wrapping>
	void writeHandler(PhysPt start, Bit8u val) {
		vga.tandy.mem_base[ start ] = val;
#ifdef DIJDIJD
		Bit32u data=ModeOperation(val);
		/* Update video memory and the pixel buffer */
		VGA_Latch pixels;
		pixels.d=((Bit32u*)vga.mem.linear)[start];
		pixels.d&=vga.config.full_not_map_mask;
		pixels.d|=(data & vga.config.full_map_mask);
		((Bit32u*)vga.mem.linear)[start]=pixels.d;
		Bit8u * write_pixels=&vga.mem.linear[VGA_CACHE_OFFSET+(start<<3)];

		Bit32u colors0_3, colors4_7;
		VGA_Latch temp;temp.d=(pixels.d>>4) & 0x0f0f0f0f;
			colors0_3 = 
			Expand16Table[0][temp.b[0]] |
			Expand16Table[1][temp.b[1]] |
			Expand16Table[2][temp.b[2]] |
			Expand16Table[3][temp.b[3]];
		*(Bit32u *)write_pixels=colors0_3;
		temp.d=pixels.d & 0x0f0f0f0f;
		colors4_7 = 
			Expand16Table[0][temp.b[0]] |
			Expand16Table[1][temp.b[1]] |
			Expand16Table[2][temp.b[2]] |
			Expand16Table[3][temp.b[3]];
		*(Bit32u *)(write_pixels+4)=colors4_7;
		if (wrapping && GCC_UNLIKELY( start < 512)) {
			*(Bit32u *)(write_pixels+512*1024)=colors0_3;
			*(Bit32u *)(write_pixels+512*1024+4)=colors4_7;
		}
#endif
	}
//	template< bool wrapping>
	Bit8u readHandler(PhysPt start) {
		return vga.tandy.mem_base[ start ];
	}

public:
	VGA_AMS_Handler() {
		//flags=PFLAG_READABLE|PFLAG_WRITEABLE;
		flags=PFLAG_NOCODE;
	}
	inline PhysPt wrAddr( PhysPt addr )
	{
		if( vga.mode != M_AMSTRAD )
		{
			addr -= 0xb8000;
			Bitu phys_page = addr >> 12;
			//test for a unaliged bank, then replicate 2x16kb
			if (vga.tandy.mem_bank & 1) 
				phys_page&=0x03;
			return ( phys_page * 4096 ) + ( addr & 0x0FFF );
		}
		return ( (PAGING_GetPhysicalAddress(addr) & 0xffff) - 0x8000 ) & ( 32*1024-1 );
	}

	void writeb(PhysPt addr,Bitu val) {
		VGAMEM_USEC_write_delay();
		addr = wrAddr( addr );
		Bitu plane = vga.mode==M_AMSTRAD ? vga.amstrad.write_plane : 0x01; // 0x0F?
		if( plane & 0x08 ) writeHandler<false>(addr+49152,(Bit8u)(val >> 0));
		if( plane & 0x04 ) writeHandler<false>(addr+32768,(Bit8u)(val >> 0));
		if( plane & 0x02 ) writeHandler<false>(addr+16384,(Bit8u)(val >> 0));
		if( plane & 0x01 ) writeHandler<false>(addr+0,(Bit8u)(val >> 0));
	}
	void writew(PhysPt addr,Bitu val) {
		VGAMEM_USEC_write_delay();
		addr = wrAddr( addr );
		Bitu plane = vga.mode==M_AMSTRAD ? vga.amstrad.write_plane : 0x01; // 0x0F?
		if( plane & 0x01 )
		{
			writeHandler<false>(addr+0,(Bit8u)(val >> 0));
			writeHandler<false>(addr+1,(Bit8u)(val >> 8));
		}
		addr += 16384;
		if( plane & 0x02 )
		{
			writeHandler<false>(addr+0,(Bit8u)(val >> 0));
			writeHandler<false>(addr+1,(Bit8u)(val >> 8));
		}
		addr += 16384;
		if( plane & 0x04 )
		{
			writeHandler<false>(addr+0,(Bit8u)(val >> 0));
			writeHandler<false>(addr+1,(Bit8u)(val >> 8));
		}
		addr += 16384;
		if( plane & 0x08 )
		{
			writeHandler<false>(addr+0,(Bit8u)(val >> 0));
			writeHandler<false>(addr+1,(Bit8u)(val >> 8));
		}

	}
	void writed(PhysPt addr,Bitu val) {
		VGAMEM_USEC_write_delay();
		addr = wrAddr( addr );
		Bitu plane = vga.mode==M_AMSTRAD ? vga.amstrad.write_plane : 0x01; // 0x0F?
		if( plane & 0x01 )
		{
			writeHandler<false>(addr+0,(Bit8u)(val >> 0));
			writeHandler<false>(addr+1,(Bit8u)(val >> 8));
			writeHandler<false>(addr+2,(Bit8u)(val >> 16));
			writeHandler<false>(addr+3,(Bit8u)(val >> 24));
		}
		addr += 16384;
		if( plane & 0x02 )
		{
			writeHandler<false>(addr+0,(Bit8u)(val >> 0));
			writeHandler<false>(addr+1,(Bit8u)(val >> 8));
			writeHandler<false>(addr+2,(Bit8u)(val >> 16));
			writeHandler<false>(addr+3,(Bit8u)(val >> 24));
		}
		addr += 16384;
		if( plane & 0x04 )
		{
			writeHandler<false>(addr+0,(Bit8u)(val >> 0));
			writeHandler<false>(addr+1,(Bit8u)(val >> 8));
			writeHandler<false>(addr+2,(Bit8u)(val >> 16));
			writeHandler<false>(addr+3,(Bit8u)(val >> 24));
		}
		addr += 16384;
		if( plane & 0x08 )
		{
			writeHandler<false>(addr+0,(Bit8u)(val >> 0));
			writeHandler<false>(addr+1,(Bit8u)(val >> 8));
			writeHandler<false>(addr+2,(Bit8u)(val >> 16));
			writeHandler<false>(addr+3,(Bit8u)(val >> 24));
		}

	}
	Bitu readb(PhysPt addr) {
		VGAMEM_USEC_read_delay();
		addr = wrAddr( addr ) + ( vga.amstrad.read_plane * 16384u );
		addr &= (64u*1024u-1u);
		return readHandler(addr);
	}
	Bitu readw(PhysPt addr) {
		VGAMEM_USEC_read_delay();
		addr = wrAddr( addr ) + ( vga.amstrad.read_plane * 16384u );
		addr &= (64u*1024u-1u);
		return 
			((Bitu)readHandler(addr+0) << 0u) |
			((Bitu)readHandler(addr+1) << 8u);
	}
	Bitu readd(PhysPt addr) {
		VGAMEM_USEC_read_delay();
		addr = wrAddr( addr ) + ( vga.amstrad.read_plane * 16384u );
		addr &= (64u*1024u-1u);
		return 
			((Bitu)readHandler(addr+0) << 0u)  |
			((Bitu)readHandler(addr+1) << 8u)  |
			((Bitu)readHandler(addr+2) << 16u) |
			((Bitu)readHandler(addr+3) << 24u);
	}

/*
	HostPt GetHostReadPt(Bitu phys_page)
	{
		if( vga.mode!=M_AMSTRAD )
		{
			phys_page-=0xb8;
			//test for a unaliged bank, then replicate 2x16kb
			if (vga.tandy.mem_bank & 1) 
				phys_page&=0x03;
			return vga.tandy.mem_base + (phys_page * 4096);
		}
		phys_page-=0xb8;
		return vga.tandy.mem_base + (phys_page*4096) + (vga.amstrad.read_plane * 16384) ;
	}
*/
/*
	HostPt GetHostWritePt(Bitu phys_page) {
		return GetHostReadPt( phys_page );
	}
*/
};

class VGA_HERC_Handler : public PageHandler {
public:
	VGA_HERC_Handler() {
		flags=PFLAG_READABLE|PFLAG_WRITEABLE;
	}
	HostPt GetHostReadPt(Bitu phys_page) {
        (void)phys_page;//UNUSED
		// The 4kB map area is repeated in the 32kB range
		return &vga.mem.linear[0];
	}
	HostPt GetHostWritePt(Bitu phys_page) {
		return GetHostReadPt( phys_page );
	}
};

class VGA_Empty_Handler : public PageHandler {
public:
	VGA_Empty_Handler() : PageHandler(PFLAG_NOCODE) {}
	Bitu readb(PhysPt /*addr*/) {
//		LOG(LOG_VGA, LOG_NORMAL ) ( "Read from empty memory space at %x", addr );
		return 0xff;
	} 
	void writeb(PhysPt /*addr*/,Bitu /*val*/) {
//		LOG(LOG_VGA, LOG_NORMAL ) ( "Write %x to empty memory space at %x", val, addr );
	}
};

static struct vg {
	VGA_Map_Handler				map;
	VGA_Slow_CGA_Handler		slow;
	VGA_TEXT_PageHandler		text;
	VGA_CGATEXT_PageHandler		cgatext;
	VGA_TANDY_PageHandler		tandy;
	VGA_ChainedEGA_Handler		cega;
	VGA_ChainedVGA_Handler		cvga;
	VGA_ChainedVGA_Slow_Handler	cvga_slow;
	VGA_ET4000_ChainedVGA_Handler		cvga_et4000;
	VGA_ET4000_ChainedVGA_Slow_Handler	cvga_et4000_slow;
	VGA_UnchainedEGA_Handler	uega;
	VGA_UnchainedVGA_Handler	uvga;
	VGA_PCJR_Handler			pcjr;
	VGA_HERC_Handler			herc;
	VGA_LIN4_Handler			lin4;
	VGA_LFB_Handler				lfb;
	VGA_MMIO_Handler			mmio;
	VGA_AMS_Handler				ams;
    VGA_PC98_PageHandler        pc98;
	VGA_Empty_Handler			empty;
} vgaph;

void VGA_ChangedBank(void) {
	VGA_SetupHandlers();
}

void MEM_ResetPageHandler_Unmapped(Bitu phys_page, Bitu pages);
void MEM_ResetPageHandler_RAM(Bitu phys_page, Bitu pages);

extern bool adapter_rom_is_ram;

void VGA_SetupHandlers(void) {
	vga.svga.bank_read_full = vga.svga.bank_read*vga.svga.bank_size;
	vga.svga.bank_write_full = vga.svga.bank_write*vga.svga.bank_size;

	PageHandler *newHandler;
	switch (machine) {
	case MCH_CGA:
		if (enableCGASnow && (vga.mode == M_TEXT || vga.mode == M_TANDY_TEXT))
			MEM_SetPageHandler( VGA_PAGE_B8, 8, &vgaph.cgatext );
		else
			MEM_SetPageHandler( VGA_PAGE_B8, 8, &vgaph.slow );
		goto range_done;
	case MCH_PCJR:
		MEM_SetPageHandler( VGA_PAGE_B8, 8, &vgaph.pcjr );
		goto range_done;
	case MCH_HERC:
		vgapages.base=VGA_PAGE_B0;
		/* NTS: Implemented according to [http://www.seasip.info/VintagePC/hercplus.html#regs] */
		if (vga.herc.enable_bits & 0x2) { /* bit 1: page in upper 32KB */
			vgapages.mask=0xffff;
			/* NTS: I don't know what Hercules graphics cards do if you set bit 1 but not bit 0.
			 *      For the time being, I'm assuming that they respond to 0xB8000+ because of bit 1
			 *      but only map to the first 4KB because of bit 0. Basically, a configuration no
			 *      software would actually use. */
			if (vga.herc.enable_bits & 0x1) /* allow graphics and enable 0xB1000-0xB7FFF */
				MEM_SetPageHandler(VGA_PAGE_B0,16,&vgaph.map);
			else
				MEM_SetPageHandler(VGA_PAGE_B0,16,&vgaph.herc);
		} else {
			vgapages.mask=0x7fff;
			// With hercules in 32kB mode it leaves a memory hole on 0xb800
			// and has MDA-compatible address wrapping when graphics are disabled
			if (vga.herc.enable_bits & 0x1)
				MEM_SetPageHandler(VGA_PAGE_B0,8,&vgaph.map);
			else
				MEM_SetPageHandler(VGA_PAGE_B0,8,&vgaph.herc);
			MEM_SetPageHandler(VGA_PAGE_B8,8,&vgaph.empty);
		}
		goto range_done;
	case MCH_TANDY:
		/* Always map 0xa000 - 0xbfff, might overwrite 0xb800 */
		vgapages.base=VGA_PAGE_A0;
		vgapages.mask=0x1ffff;
		MEM_SetPageHandler(VGA_PAGE_A0, 32, &vgaph.map );
		if ( vga.tandy.extended_ram & 1 ) {
			//You seem to be able to also map different 64kb banks, but have to figure that out
			//This seems to work so far though
			vga.tandy.draw_base = vga.mem.linear;
			vga.tandy.mem_base = vga.mem.linear;
		} else {
			vga.tandy.draw_base = TANDY_VIDBASE( vga.tandy.draw_bank * 16 * 1024);
			vga.tandy.mem_base = TANDY_VIDBASE( vga.tandy.mem_bank * 16 * 1024);
			MEM_SetPageHandler( 0xb8, 8, &vgaph.tandy );
		}
		goto range_done;
//		MEM_SetPageHandler(vga.tandy.mem_bank<<2,vga.tandy.is_32k_mode ? 0x08 : 0x04,range_handler);
	case MCH_AMSTRAD: // Memory handler.
		MEM_SetPageHandler( 0xb8, 8, &vgaph.ams );
		goto range_done;
	case EGAVGA_ARCH_CASE:
    case PC98_ARCH_CASE:
		break;
	default:
		LOG_MSG("Illegal machine type %d", machine );
		return;
	}

	/* This should be vga only */
	switch (vga.mode) {
	case M_ERROR:
	default:
		return;
	case M_LIN4:
		newHandler = &vgaph.lin4;
		break;	
	case M_LIN15:
	case M_LIN16:
	case M_LIN24:
	case M_LIN32:
		newHandler = &vgaph.map;
		break;
	case M_LIN8:
	case M_VGA:
		if (vga.config.chained) {
			bool slow = false;

			/* NTS: Most demos and games do not use the Graphics Controller ROPs or bitmask in chained
			 *      VGA modes. But, for the few that do, we have a "slow and accurate" implementation
			 *      that will handle these demos properly at the expense of some emulation speed.
			 *
			 *      This fixes:
			 *        Impact Studios 'Legend' demo (1993) */
			if (vga.config.full_bit_mask != 0xFFFFFFFF)
				slow = true;

			if (slow || vga.config.compatible_chain4) {
				/* NTS: ET4000AX cards appear to have a different chain4 implementation from everyone else:
				 *      the planar memory byte address is address >> 2 and bits A0-A1 select the plane,
				 *      where all other clones I've tested seem to write planar memory byte (address & ~3)
				 *      (one byte per 4 bytes) and bits A0-A1 select the plane. */
				/* FIXME: Different chain4 implementation on ET4000 noted---is it true also for ET3000? */
				if (svgaCard == SVGA_TsengET3K || svgaCard == SVGA_TsengET4K)
					newHandler = slow ? ((PageHandler*)(&vgaph.cvga_et4000_slow)) : ((PageHandler*)(&vgaph.cvga_et4000));
				else
					newHandler = slow ? ((PageHandler*)(&vgaph.cvga_slow)) : ((PageHandler*)(&vgaph.cvga));
			}
			else {
				newHandler = &vgaph.map;
			}
		} else {
			newHandler = &vgaph.uvga;
		}
		break;
	case M_EGA:
		if (vga.config.chained) 
			newHandler = &vgaph.cega;
		else
			newHandler = &vgaph.uega;
		break;	
	case M_TEXT:
	case M_CGA2:
	case M_CGA4:
		newHandler = &vgaph.text;
		break;
    case M_PC98:
		newHandler = &vgaph.pc98;

        /* We need something to catch access to E0000-E7FFF IF 16/256-color mode */
        if (pc98_gdc_vramop & (1 << VOPBIT_ANALOG))
            MEM_SetPageHandler(0xE0, 8, newHandler );
        else
            MEM_ResetPageHandler_Unmapped(0xE0, 8);

        break;
	case M_AMSTRAD:
		newHandler = &vgaph.map;
		break;
	}
	switch ((vga.gfx.miscellaneous >> 2) & 3) {
	case 0:
		vgapages.base = VGA_PAGE_A0;
		switch (svgaCard) {
		case SVGA_TsengET3K:
			break;
		/* NTS: Looking at the official ET4000 programming guide, it does in fact support the full 128KB */
		case SVGA_S3Trio:
		default:
			vgapages.mask = 0x1ffff;
			break;
		}
		MEM_SetPageHandler(VGA_PAGE_A0, 32, newHandler );
		break;
	case 1:
		vgapages.base = VGA_PAGE_A0;
		vgapages.mask = 0xffff;
		MEM_SetPageHandler( VGA_PAGE_A0, 16, newHandler );
		if (adapter_rom_is_ram) MEM_ResetPageHandler_RAM( VGA_PAGE_B0, 16);
		else MEM_ResetPageHandler_Unmapped( VGA_PAGE_B0, 16);
		break;
	case 2:
		vgapages.base = VGA_PAGE_B0;
		vgapages.mask = 0x7fff;
		MEM_SetPageHandler( VGA_PAGE_B0, 8, newHandler );
		if (adapter_rom_is_ram) {
			MEM_ResetPageHandler_RAM( VGA_PAGE_A0, 16 );
			MEM_ResetPageHandler_RAM( VGA_PAGE_B8, 8 );
		}
		else {
			MEM_ResetPageHandler_Unmapped( VGA_PAGE_A0, 16 );
			MEM_ResetPageHandler_Unmapped( VGA_PAGE_B8, 8 );
		}
		break;
	case 3:
		vgapages.base = VGA_PAGE_B8;
		vgapages.mask = 0x7fff;
		MEM_SetPageHandler( VGA_PAGE_B8, 8, newHandler );
		if (adapter_rom_is_ram) {
			MEM_ResetPageHandler_RAM( VGA_PAGE_A0, 16 );
			MEM_ResetPageHandler_RAM( VGA_PAGE_B0, 8 );
		}
		else {
			MEM_ResetPageHandler_Unmapped( VGA_PAGE_A0, 16 );
			MEM_ResetPageHandler_Unmapped( VGA_PAGE_B0, 8 );
		}
		break;
	}
	if(svgaCard == SVGA_S3Trio && (vga.s3.ext_mem_ctrl & 0x10))
		MEM_SetPageHandler(VGA_PAGE_A0, 16, &vgaph.mmio);
range_done:
	PAGING_ClearTLB();
}

void VGA_StartUpdateLFB(void) {
	/* please obey the Linear Address Window Size register!
	 * Windows 3.1 S3 driver will reprogram the linear framebuffer down to 0xA0000 when entering a DOSBox
	 * and assuming the full VRAM size will cause a LOT of problems! */
	Bitu winsz = 0x10000;

	switch (vga.s3.reg_58&3) {
		case 1:
			winsz = 1 << 20;	//1MB
			break;
		case 2:
			winsz = 2 << 20;	//2MB
			break;
		case 3:
			winsz = 4 << 20;	//4MB
			break;
		// FIXME: What about the 8MB window?
	}

	/* if the DOS application or Windows 3.1 driver attempts to put the linear framebuffer
	 * below the top of memory, then we're probably entering a DOS VM and it's probably
	 * a 64KB window. If it's not a 64KB window then print a warning. */
	if ((unsigned long)(vga.s3.la_window << 4UL) < (unsigned long)MEM_TotalPages()) {
		if (winsz != 0x10000) // 64KB window normal for entering a DOS VM in Windows 3.1 or legacy bank switching in DOS
			LOG(LOG_MISC,LOG_WARN)("S3 warning: Window size != 64KB and address conflict with system RAM!");

		vga.lfb.page = (unsigned int)vga.s3.la_window << 4u;
		vga.lfb.addr = (unsigned int)vga.s3.la_window << 16u;
		vga.lfb.handler = NULL;
		MEM_SetLFB(0,0,NULL,NULL);
	}
	else {
		vga.lfb.page = (unsigned int)vga.s3.la_window << 4u;
		vga.lfb.addr = (unsigned int)vga.s3.la_window << 16u;
		vga.lfb.handler = &vgaph.lfb;
		MEM_SetLFB((unsigned int)vga.s3.la_window << 4u,(unsigned int)vga.vmemsize/4096u, vga.lfb.handler, &vgaph.mmio);
	}
}

static bool VGA_Memory_ShutDown_init = false;

static void VGA_Memory_ShutDown(Section * /*sec*/) {
	MEM_SetPageHandler(VGA_PAGE_A0,32,&vgaph.empty);
	PAGING_ClearTLB();

	if (vga.mem.linear_orgptr != NULL) {
		delete[] vga.mem.linear_orgptr;
		vga.mem.linear_orgptr = NULL;
		vga.mem.linear = NULL;
	}
}

void VGA_SetupMemory() {
	vga.svga.bank_read = vga.svga.bank_write = 0;
	vga.svga.bank_read_full = vga.svga.bank_write_full = 0;

    if (1 || vga.vmemsize_alloced != vga.vmemsize) {
        VGA_Memory_ShutDown(NULL);

        vga.mem.linear_orgptr = new Bit8u[vga.vmemsize+32u];
        memset(vga.mem.linear_orgptr,0,vga.vmemsize+32u);
        vga.mem.linear=(Bit8u*)(((uintptr_t)vga.mem.linear_orgptr + 16ull-1ull) & ~(16ull-1ull));
        vga.vmemsize_alloced = vga.vmemsize;

        /* HACK. try to avoid stale pointers */
	    vga.draw.linear_base = vga.mem.linear;
        vga.tandy.draw_base = vga.mem.linear;
        vga.tandy.mem_base = vga.mem.linear;

        /* may be related */
        VGA_SetupHandlers();
    }

	// In most cases these values stay the same. Assumptions: vmemwrap is power of 2, vmemwrap <= vmemsize
	vga.vmemwrap = vga.vmemsize;

	vga.svga.bank_read = vga.svga.bank_write = 0;
	vga.svga.bank_read_full = vga.svga.bank_write_full = 0;
	vga.svga.bank_size = 0x10000; /* most common bank size is 64K */

	if (!VGA_Memory_ShutDown_init) {
		AddExitFunction(AddExitFunctionFuncPair(VGA_Memory_ShutDown));
		VGA_Memory_ShutDown_init = true;
	}

	if (machine==MCH_PCJR) {
		/* PCJr does not have dedicated graphics memory but uses
		   conventional memory below 128k */
		//TODO map?	
	} 
}


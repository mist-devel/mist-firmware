/* ----------------------------------------------------------------------------
 *         SAM Software Package License
 * ----------------------------------------------------------------------------
 * Copyright (c) 2015, Atmel Corporation
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the disclaimer below.
 *
 * Atmel's name may not be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * DISCLAIMER: THIS SOFTWARE IS PROVIDED BY ATMEL "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
 * DISCLAIMED. IN NO EVENT SHALL ATMEL BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ----------------------------------------------------------------------------
 */

/*------------------------------------------------------------------------------
 *      Linker script for running in internal flash on the SAMV71
 *----------------------------------------------------------------------------*/

OUTPUT_FORMAT("elf32-littlearm", "elf32-littlearm", "elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(reset_handler)
SEARCH_DIR(.)

/* Memory Spaces Definitions */
MEMORY
{
	flash   (RX)   : ORIGIN = 0x00400000, LENGTH = 2M   /* Internal Flash */
	sram    (W!RX) : ORIGIN = 0x20400000, LENGTH = 380K /* SRAM */
	sram_nc (RWX)  : ORIGIN = 0x2045F000, LENGTH = 4K   /* SRAM (non-cached) */
}

/* Sizes of the stacks used by the application. NOTE: you need to adjust */
C_STACK_SIZE   = 0x10000;
HEAP_SIZE      = 0x10000;

/* Section Definitions */
SECTIONS
{
	.fixed0 :
	{
		. = ALIGN(4);
		_sfixed = .;
		KEEP(*(.vectors))
		*(.cstartup)
		*(.text .text.* .gnu.linkonce.t.*)
		*(.glue_7t) *(.glue_7)
		*(.rodata .rodata* .gnu.linkonce.r.*)
		*(.ARM.extab* .gnu.linkonce.armextab.*)

		/* Support C constructors, and C destructors in both user code
		   and the C library. This also provides support for C++ code. */
		. = ALIGN(4);
		KEEP(*(.init))
		. = ALIGN(4);
		__preinit_array_start = .;
		KEEP(*(.preinit_array))
		__preinit_array_end = .;

		. = ALIGN(4);
		__init_array_start = .;
		KEEP(*(SORT(.init_array.*)))
		KEEP(*(.init_array))
		__init_array_end = .;

		. = ALIGN(0x4);
		KEEP(*crtbegin.o(.ctors))
		KEEP(*(EXCLUDE_FILE (*crtend.o) .ctors))
		KEEP(*(SORT(.ctors.*)))
		KEEP(*crtend.o(.ctors))

		. = ALIGN(4);
		KEEP(*(.fini))

		. = ALIGN(4);
		__fini_array_start = .;
		KEEP(*(.fini_array))
		KEEP(*(SORT(.fini_array.*)))
		__fini_array_end = .;

		KEEP(*crtbegin.o(.dtors))
		KEEP(*(EXCLUDE_FILE (*crtend.o) .dtors))
		KEEP(*(SORT(.dtors.*)))
		KEEP(*crtend.o(.dtors))
		. = ALIGN(4);
		_efixed = .;            /* End of text section */
	} >flash

	/* .ARM.exidx is sorted, so has to go in its own output section.  */
	PROVIDE_HIDDEN (__exidx_start = .);
	.ARM.exidx :
	{
		*(.ARM.exidx* .gnu.linkonce.armexidx.*)
	} >flash
	PROVIDE_HIDDEN (__exidx_end = .);

	/* _etext must be just before .relocate section */
	. = ALIGN(4);
	_etext = .;

	.relocate :
	{
		. = ALIGN(4);
		_srelocate = .;
		*(.ramfunc)
		*(.ramsection)
		*(.data .data.*);
		. = ALIGN(4);
		_erelocate = .;
	} >sram AT>flash

	/* Please see drivers/mm/cache.h for details on the "Cache-aligned" sections */

	.region_cache_aligned_const :
	{
		. = ALIGN(32);
		*(.region_cache_aligned_const)
		. = ALIGN(32);
	} >sram AT>flash

	.region_sram (NOLOAD) :
	{
		. = ALIGN(4);
		*(.region_sram)
	} >sram

	/* .bss section which is used for uninitialized data */
	.bss (NOLOAD) :
	{
		__bss_start = . ;
		__bss_start__ = . ;
		. = ALIGN(4);
		_szero = .;
		*(.bss .bss.*)
		*(COMMON)
		. = ALIGN(4);
		_ezero = .;
	} >sram
	__bss_end__ = .;
	_end = .;
	PROVIDE (end = .);

	/* Please see drivers/mm/cache.h for details on the "Cache-aligned" sections */

	.region_nocache (NOLOAD) :
	{
		. = ALIGN(4);
		*(.region_nocache)
	} >sram_nc

	.region_cache_aligned (NOLOAD) :
	{
		. = ALIGN(32);
		*(.region_cache_aligned)
		. = ALIGN(32);
	} >sram

	.heap (NOLOAD) :
	{
		. = ALIGN(4);
		__heap_start__ = .;
		. += HEAP_SIZE;
		__heap_end__ = .;
	} >sram

	.stack (NOLOAD) :
	{
		. += C_STACK_SIZE;
		. = ALIGN(8);
		_cstack = .;
	} >sram
}

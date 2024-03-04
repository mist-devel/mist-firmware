/* ----------------------------------------------------------------------------
 *         SAM Software Package License
 * ----------------------------------------------------------------------------
 * Copyright (c) 2016, Atmel Corporation
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

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include "barriers.h"
#include "chip.h"
#include "core_cm7.h"

#include "irq/nvic.h"
#include "irqflags.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

/*----------------------------------------------------------------------------
 *        Local variables
 *----------------------------------------------------------------------------*/

ALIGNED(128 * 4)
static nvic_handler_t nvic_vectors[16 + ID_PERIPH_COUNT];

/*----------------------------------------------------------------------------
 *        Exported functions
 *----------------------------------------------------------------------------*/

void nvic_initialize(nvic_handler_t irq_handler)
{
	int i;

	/* Disable interrupts at core level */
	arch_irq_disable();

	memcpy(nvic_vectors, (void*)SCB->VTOR, 16 * 4);
	for (i = 0; i < ID_PERIPH_COUNT; i++)
		nvic_vectors[16 + i] = irq_handler;
	SCB->VTOR = (uint32_t)nvic_vectors;
	dsb();

	/* Enable interrupts at core level */
	arch_irq_enable();
}

uint32_t nvic_get_current_interrupt_source(void)
{
	uint32_t ipsr;
	asm("mrs %0, ipsr" : "=r"(ipsr));
	return ipsr - 16;
}

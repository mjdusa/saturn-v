// Copyright 2019-2024 Great Scott Gadgets <info@greatscottgadgets.com>
// Copyright 2019 Katherine J. Temkin <kate@ktemkin.com>
// Copyright 2014 Technical Machine, Inc. See the COPYRIGHT
// file at the top-level directory of this distribution.
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

#include "common/util.h"
#include "samd/usb_samd.h"

#include <string.h>
#include <stdbool.h>

#include "boot.h"
#include "common/nvm.h"

#include <board.h>

volatile bool exit_and_jump = 0;

void delay_ms(unsigned ms) {
	/* Approximate ms delay using dummy clock cycles of 48 MHz clock */
	for (unsigned i = 0; i < ms * (48000000 / 1000 / 5); ++i) {
		__NOP();
	}
}

/*** USB / DFU ***/

void dfu_cb_dnload_block(uint16_t block_num, uint16_t len) {
	if (usb_setup.wLength > DFU_TRANSFER_SIZE) {
		dfu_error(DFU_STATUS_errUNKNOWN);
		return;
	}

	if (block_num * DFU_TRANSFER_SIZE > FLASH_FW_SIZE) {
		dfu_error(DFU_STATUS_errADDRESS);
		return;
	}

	nvm_erase_row(FLASH_FW_START + block_num * DFU_TRANSFER_SIZE);
}

void dfu_cb_dnload_packet_completed(uint16_t block_num, uint16_t offset, uint8_t* data, uint16_t length) {
	unsigned addr = FLASH_FW_START + block_num * DFU_TRANSFER_SIZE + offset;
	nvm_write_page(addr, data, length);
}

unsigned dfu_cb_dnload_block_completed(uint16_t block_num, uint16_t length) {
	return 0;
}

void dfu_cb_manifest(void) {
	exit_and_jump = 1;
}

void noopFunction(void)
{
	// Placeholder function for code that isn't needed. Keep empty!
}

void bootloader_main(void)
{

	// Set up output pins.
#if ((_BOARD_REVISION_MAJOR_ == 0) && (_BOARD_REVISION_MINOR_ < 3))
	pins_out(0, (1 << LED_PIN.pin), 0);
#elif ((_BOARD_REVISION_MAJOR_ == 0) && (_BOARD_REVISION_MINOR_ < 6))
	pins_out(0, (1 << LED_PIN.pin) | (1 << SIDEBAND_RESET.pin), 0);
#else
	pins_out(0, (1 << LED_PIN.pin) | (1 << USB_SWITCH.pin), 0);

	// Take over USB port in board revisions >=0.6
	pin_high(USB_SWITCH);
#endif
	/*
	 * Cynthion r0.3 through r0.5 do not have a USB switch. On these
	 * platforms the Sideband PHY RESET is pulled low to ensure that it
	 * does not interfere with the bootloader's use of the Sideband USB
	 * port. The output is explicitly enabled but implicitly driven low (by
	 * reset default). Sideband PHY usage is unsupported on Cynthion r0.1
	 * and r0.2.
	 */


	// Set up the main clocks.
	clock_init_usb(GCLK_SYSTEM);

	__enable_irq();

	// Configure USB pins (24 and 25)
	pins_wrconfig(0, 0x03000000, PORT_WRCONFIG_WRPMUX | PORT_WRCONFIG_PMUXEN | 
								 PORT_WRCONFIG_PMUX(PORT_PMUX_PMUXE_G_Val));

	usb_init();
	dfu_reset();
	usb_attach();

	// Blink while we're in DFU mode.
	while(!exit_and_jump) {
		pin_toggle(LED_PIN);
		delay_ms(300);
	}

	usb_detach();
	nvm_invalidate_cache();

	// Hook: undo any special setup that board_setup_late might be needed to
	// undo the setup the bootloader code has done.
	NVIC_SystemReset();
}

bool flash_valid() {
	unsigned sp = ((unsigned *)FLASH_FW_ADDR)[0];
	unsigned ip = ((unsigned *)FLASH_FW_ADDR)[1];

	return     sp > 0x20000000
			&& ip >= FLASH_FW_START
			&& ip <  0x00400000;
}

bool bootloader_sw_triggered(void)
{
	// Was reset caused by watchdog timer (WDT)?
	return PM->RCAUSE.reg & PM_RCAUSE_WDT;
}


bool button_pressed(void)
{
	// Configure PROGRAM button
#if ((_BOARD_REVISION_MAJOR_ == 0) && (_BOARD_REVISION_MINOR_ < 6))
	const uint32_t flags = PORT_WRCONFIG_INEN | PORT_WRCONFIG_PULLEN;
#else
	const uint32_t flags = PORT_WRCONFIG_INEN;
#endif
	pins_in(0, 1 << PROGRAM_BUTTON.pin, flags);
	pins_high(0, 1 << PROGRAM_BUTTON.pin);

	// Drop into DFU mode if the PROGRAM button is presssed.
	if (pin_read(PROGRAM_BUTTON) == 0) {
		return true;
	}

	return false;
}




void main_bl(void) {
	uint32_t reason = PM->RCAUSE.reg;
	if (!flash_valid() || (reason & PM_RCAUSE_WDT) || ((reason & PM_RCAUSE_POR) && button_pressed())) {
		bootloader_main();
	}

	jump_to_flash(FLASH_FW_ADDR, 0);
}

#include <io.h>
#include <stdio.h>
#include <unistd.h>

#include "system.h"
#include "alt_types.h"
#include "sys/alt_irq.h"
#include "altera_avalon_timer_regs.h"

volatile alt_u8 switch_LED = 0x00;
volatile alt_u8 switch_SSeg = 0x00;

volatile int counterLED = 0;
volatile int counterSEG = 0;
volatile int ledState = 0;
volatile int sSState = 0;

static void TIMER_ISR(void* context, alt_u32 id) {

	//Clear the timer bit
	IOWR(TIMER_0_BASE, 0, 0x0);

	//If we are still iterating through the switches w/ LED
	if (ledState == 1) {

		//Check to see if the switch is a 1 or 0
		if (switch_LED & 0x1) {
			IOWR(LED_PIO_BASE, 0, 0x1);
		} else {
			IOWR(LED_PIO_BASE, 0, 0x0);
		}

		//Increment counter and move onto next switch
		counterLED++;
		switch_LED >>= 1;
	}

	//If we are still iterating through the switches w/ 7Seg
	if (sSState == 1) {

		//Check to see if the switch is a 1 or 0
		if (switch_SSeg & 0x1) {
			IOWR(SEVEN_SEG_PIO_BASE,0, 0xFFCF);
		} else {
			IOWR(SEVEN_SEG_PIO_BASE,0, 0xFF81);
		}
		counterSEG++;
		switch_SSeg >>= 1;
	}

	if (counterLED == 9) {
		counterLED = 0;
		ledState = 0;
		IOWR(LED_PIO_BASE,0, 0x0);
	}

	if (counterSEG == 9) {
		counterSEG = 0;
		sSState = 0;
		IOWR(SEVEN_SEG_PIO_BASE,0, 0xFFFF);
	}
}

static void button_ISR(void* context, alt_u32 id) {
	//Only need the first two buttons.
	alt_u8 buttons = IORD(BUTTON_PIO_BASE, 3) & 0x3;

	//If switch_LED was pressed
	if (buttons & 0x1) {
		switch_LED = IORD(SWITCH_PIO_BASE,0);
		counterLED = 0;
		ledState = 1;
	}

	//If switch_SSeg was pressed
	if (buttons & 0x2) {
		switch_SSeg = IORD(SWITCH_PIO_BASE,0);
		counterSEG = 0;
		sSState = 1;
	}

	// Clear interrupt flag
	IOWR(BUTTON_PIO_BASE, 3, 0x0);
}

int main(void) {
	//Registering the timer interrupt request
	alt_irq_register(TIMER_0_IRQ, (void*) 0, TIMER_ISR);

	//Establishing the timer's period
	IOWR(TIMER_0_BASE, 2, (alt_u16)TIMER_0_FREQ);
	IOWR(TIMER_0_BASE, 3, (alt_u16)(TIMER_0_FREQ >> 16));

	//Clear the timer bit in register and initialise
	IOWR(TIMER_0_BASE, 0, 0x0);
	IOWR(TIMER_0_BASE, 1, 0x7);

	//Initialise the interrupt request
	alt_irq_register(BUTTON_PIO_IRQ, (void*) 0, button_ISR);

	IOWR(BUTTON_PIO_BASE, 2, 0xF);
	IOWR(BUTTON_PIO_BASE, 3, 0x0);
	IOWR(LED_PIO_BASE, 0, 0x00);
	IOWR(SEVEN_SEG_PIO_BASE,0, 0xffff);

	while (1) {	}
	return (0);
}

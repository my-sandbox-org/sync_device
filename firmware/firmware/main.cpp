/*
 * Synchronization device for laser and camera control at pTIRF microscope
 *
 * @file main.cpp
 * @author Roman Kiselev (roman.kiselev@stjude.org)
 * @brief Synchronization device for laser and camera control at pTIRF microscope - main file
 * @version 0.3
 * @date 2022-01-10
 *
 * @copyright Copyright (c) 2023
*/ 

#include "sys_globals.h"
#include "triggers.h"
#include "timers.h"
#include "uart.h"

SystemSettings sys = {
	IDLE,    // STATUS   status;
	500UL,  // uint32_t shutter_delay_us;
	2500UL, // uint32_t cam_readout_us;
	1000UL,  // uint32_t exp_time_us;
	20000UL, // uint32_t acq_period_us; // at least the sum of all three above
	0,       // uint32_t n_frames;
	0,       // uint32_t n_acquired_frames;
	bit(CY2_PIN), // uint8_t current_laser;
	SHUTTERS_MASK
	};

int main(void)
{
    init_IO();
	init_UART();
	sei();

//	start_acq();

	UART_tx("Big hello to this world!\n");

	char buf[6];
	char byte;
    while (1) 
    {
		if(UART_rx(buf, 5) == OK)
		{
			buf[5] = 0;
			UART_tx(buf);
			UART_tx('\n');
		}
    }
}


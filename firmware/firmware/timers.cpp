#include "timers.h"
#include "triggers.h"
#include "uart.h"

static uint16_t t3_cycle = 0;
static uint16_t t3_N_OVF_cycles = 1;
static bool last_burst_frame = false;

void update_fluidics_pin()
{
	if (sys.fluidics_frame == static_cast <int32_t> (sys.n_acquired_frames))
	{
		bitSet(FLUIDIC_PORT, FLUIDIC_PIN);
	}
	else
	{
		bitClear(FLUIDIC_PORT, FLUIDIC_PIN);		
	}
}

void setup_timer3(uint32_t us)
{
	t3_cycle = 0;
	t3_N_OVF_cycles = 1;
	// We divide required #counts in half until if fits into 16bit
	uint32_t cts = us2cts(us);
	while (cts >= 65535)
	{
		cts >>= 1;             // divide in half
		t3_N_OVF_cycles <<= 1; // double number of cycles
	}
	ICR3 = cts - 1;
}

void start_continuous_acq(uint32_t n_frames /*= 0*/)
{
	stop_acq();
	sys.status = CONT_ACQ;
	sys.n_frames = n_frames;
	
	/* Write timing info to double-buffered output compare registers OCR1X */

	// OCR1A (inactive) triggers camera after the shutter opening delay
	OCR1A = us2cts(sys.shutter_delay_us);

	// OCR1B (inactive) closes laser shutter and stops the acquisition
	OCR1B = us2cts((sys.shutter_delay_us + sys.cam_readout_us));

	/* First frame is as short as possible and will be discarded,
	we need to read it out to clean out the signal accumulated in the sensor */
	// Overflow @ start of next frame
	ICR1 = us2cts((MAX(sys.cam_readout_us, sys.shutter_delay_us) + 250)) - 1;

	// OCR1C generates the falling edge of the camera trigger. It's
	// different for discard frame and subsequent frames
	OCR1C = ICR1 >> 1;
	
	/************************************************************************/
	/* TODO: SANITY CHECK FOR THE SUPPLIED VALUES                           */
	/* If values are weird, send error message back to the host and abort   */
	// errcode check_timings(STATUS acq_mode)
	/* Example: cam readout cannot be shorter than exposure time*/
	/************************************************************************/

	// Configure and start TC1
	TCCR1A = bit(WGM11); // WGM mode 14 (fast PWM with ICR1 max)
	TCCR1B = bit(WGM12) | bit(WGM13) | TCCR1B_prescaler_bits;
	
	// Enable interrupts for match C and overflow
	TIMSK1 = bit(TOIE1) | bit(OCIE1C);
	
	// Trigger camera
	bitSet(CAMERA_PORT, CAMERA_PIN);
	
	update_fluidics_pin();
}


void start_stroboscopic_acq(uint32_t n_frames /*= 0*/)
{
	stop_acq();
	sys.status = STRB_ACQ;
	sys.n_frames = n_frames;

	// Set timing interval for TC1 (controls lasers and camera)
	OCR1A = us2cts((sys.shutter_delay_us + 1));		// Camera raising edge
	OCR1B = us2cts(sys.exp_time_us);				// Laser off
	OCR1C = us2cts((sys.exp_time_us - 2)) + OCR1A;	// Camera falling edge
	ICR1 = OCR1C + us2cts(sys.cam_readout_us) - 1;	// Overflow - next frame

	// Set timing interval for TC3 (it may need multiple cycles)
	setup_timer3(sys.acq_period_us);

	// Pause and sync timers
	GTCCR |= bit(TSM) | bit(PSRSYNC);

	// Configure timers 1 and 3
	TCCR1A = bit(WGM11); // WGM mode 14 (fast PWM with ICR1 max)
	TCCR1B = bit(WGM12) | bit(WGM13) | TCCR1B_prescaler_bits;

	TCCR3A = bit(WGM31);
	TCCR3B = bit(WGM32) | bit(WGM33) | TCCR1B_prescaler_bits;

	// Enable interrupts for overflow, match A, and match B
	TIMSK1 = bit(TOIE1) | bit(OCIE1A) | bit(OCIE1B) | bit(OCIE1C);
	TIMSK3 = bit(TOIE3);

	// Let the timers go!
	bitClear(GTCCR, TSM);

	// open laser shutters
	set_lasers(sys.current_laser);
	
	update_fluidics_pin();
}



// ---------------------------
// INTERRUPTS
// ---------------------------

ISR(TIMER1_COMPA_vect)
{
	bitSet(CAMERA_PORT, CAMERA_PIN);
}

ISR(TIMER1_COMPB_vect)
{
	lasers_off();

	// In continuous mode, this concludes the acquisition
	if (sys.status == CONT_ACQ)
	{
		stop_acq();
		UART_tx("DONE\n");
	}
}

ISR(TIMER1_COMPC_vect)
{
	// Generate falling edge of the camera trigger
	bitClear(CAMERA_PORT, CAMERA_PIN);
	
	/* CONTINUOUS ACQUISITION LOGIC */
	if (sys.status == CONT_ACQ)
	{
		if (sys.n_acquired_frames == 0)  // discard frame
		{
			OCR1C = us2cts(sys.shutter_delay_us) + (us2cts(sys.cam_readout_us) >> 1);
		}
	}

	/* STROBOSCOPIC/ALEX ACQUISITION LOGIC */
	// In strobe mode, we might need to alternate lasers within a burst
	if (sys.status == STRB_ACQ)
	{
		if (sys.ALEX_enabled)
		{
			// Activate the next laser
			sys.current_laser = next_laser();
			// Cy2 because we already moved on to the next laser
			last_burst_frame = (sys.current_laser == get_first_laser());
		}
		else
		{
			last_burst_frame = true;
		}
	}
}


ISR(TIMER1_OVF_vect)
{
	/* CONTINUOUS ACQUISITION LOGIC */
	if (sys.status == CONT_ACQ)
	{
		// we call it here so that active lasers could be update on the fly
		set_lasers(sys.lasers_in_use);

		if (sys.n_acquired_frames == 0)  // discard frame
		{
			
			TIFR1 |= bit(OCF1A);   // clear interrupt flag
			TIMSK1 |= bit(OCIE1A); // activate interrupt
			
			// Change TC1 period (exposure time)
			ICR1 = us2cts(sys.exp_time_us) - 1;
		}

		// This was the last frame
		if ((sys.n_acquired_frames == sys.n_frames) && (sys.n_frames > 0))
		{
			// Setup interrupt B to close laser shutter and stop timer
			OCR1B = us2cts((sys.shutter_delay_us + sys.cam_readout_us));
			TIFR1 |= bit(OCF1B);   // clear interrupt flag
			TIMSK1 |= bit(OCIE1B); // activate interrupt
		}

		// Acquisition finished, we missed OCR1B event
		if ((sys.n_acquired_frames > sys.n_frames) && (sys.n_frames > 0))
		{
			stop_acq();
			UART_tx("DONE\n");
		}

		sys.n_acquired_frames++;
	}
	
	
	/* STROBOSCOPIC/ALEX ACQUISITION LOGIC */
	if (sys.status == STRB_ACQ)
	{
		if(last_burst_frame)
		{
			// Pause timer 1
			TCCR1B = bit(WGM12) | bit(WGM13);
		}
		else
		{
			// Open shutter for the next laser within the burst (ALEX only)
			set_lasers(sys.current_laser);
		}
	}
	
	update_fluidics_pin();
}


ISR(TIMER3_OVF_vect) // used only for stroboscopic acquisition mode
{
	t3_cycle++;
	if (t3_cycle >= t3_N_OVF_cycles) // count # of overflow events for long intervals
	{
		if ((++sys.n_acquired_frames < sys.n_frames) || (sys.n_frames == 0))
		{
			// Reset the prescaler to sync timers
			GTCCR |= PSRSYNC;

			t3_cycle = 0;

			// Open laser shutters
			set_lasers(sys.current_laser);

			// Start timer 1
			TCCR1B |= TCCR1B_prescaler_bits;
		}
		else
		{
			stop_acq();
			UART_tx("DONE\n");
		}
	}
}

void stop_acq()
{
	// Turn off lasers, camera, and fluidics
	lasers_off();
	bitClear(CAMERA_PORT, CAMERA_PIN);
	update_fluidics_pin();

	// Reset everything
	sys.status = IDLE;
	reset_lasers();
	sys.n_acquired_frames = 0;
	t3_cycle = 0;
	t3_N_OVF_cycles = 1;
	last_burst_frame = false;

	// Reset timers
	TCCR1A = 0;
	TCCR1B = 0;
	TCNT1 = 0;

	TCCR3A = 0;
	TCCR3B = 0;
	TCNT3 = 0;
	
	// Clear timer interrupt flags
	TIFR1 = bit(TOV1) | bit(OCF1A) | bit(OCF1B) | bit(OCF1C);
	TIFR3 = bit(TOV3) | bit(OCF3A) | bit(OCF3B) | bit(OCF3C);
}
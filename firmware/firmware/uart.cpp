#include "uart.h"
#include "timers.h"
#include "triggers.h"

#define BAUDRATE 2000000UL
#define UART_CTS F_CPU/8/BAUDRATE - 1

// convert given memory address to long integer (for pointers),
// then convert it to pointer to unsigned char, and dereference it.
#define MEM_IO_8bit(mem_addr) (*(volatile uint8_t *)(uintptr_t)(mem_addr))


/********************************
COMMUNICATION PROTOCOL DEFINITION
********************************/

// Address and value of register for read/write operations
typedef struct
{
	uint8_t addr;
	uint8_t value;
} Register;

// Laser shutter states - in active and idle mode
typedef struct
{
	uint8_t lasers_in_use;
	bool ALEX_enabled;
} LaserShutter;

// Data packet for serial communication
union Data
{
	struct
	{
		uint8_t cmd;

		// All members below share the same chunk of memory
		union
		{
			Register R;             // register access (R/W)
			LaserShutter lasers;
			int32_t int32_value;
			uint32_t uint32_value;
		};
	};

	char bytes[5];
};


/************************************************************************/
/* UART responses                                                       */
/************************************************************************/
void UART_tx_ok() { UART_tx("OK\n"); }
void UART_tx_err() { UART_tx("ERR\n"); }
void UART_tx_err(const char *msg)
{
	UART_tx(msg);
	UART_tx("\n");
}



void init_UART()
{
	// Set baud rate
	UBRR0 = UART_CTS;
	
	// Enable double speed
	UCSR0A = bit(U2X0);
	
	// Enable receiver and transmitter
	UCSR0B = bit(RXEN0) | bit(TXEN0);

	// Set 8bit asynchronous mode
	UCSR0C = bit(UCSZ01) | bit(UCSZ00);
	
	// Start timer 0 (to detect timeouts)
	TCCR0A = 0;
	TCCR0B = bit(CS02) | bit(CS00);  // /1024 prescaler, overflow every 16.384 ms
}


void UART_tx(const char byte)
{
	while ( !bitRead(UCSR0A, UDRE0) )
		;

	UDR0 = byte;
}

void UART_tx(const char *bytearray)
{
	for (uint8_t i = 0; bytearray[i] != 0; i++)
	{
		UART_tx(bytearray[i]);
	}
}

errcode UART_rx(char &byte)
{
	// Reset timeout clock and remove overflow flag
	TCNT0 = 0;
	TIFR0 = bit(TOV0);
	
	while ( !bitRead(UCSR0A, RXC0) ) // Wait for data...
		if (bitRead(TIFR0, TOV0))  // until a timeout is detected
			return ERR_TIMEOUT;

	byte = UDR0;
	return OK;
}

errcode UART_rx(char *bytearray, uint8_t size)
{
	for (uint8_t i = 0; i < size; i++)
		if (UART_rx(bytearray[i]) == ERR_TIMEOUT)
			return ERR_TIMEOUT;

	return OK;
}


/************************************************************************/
/* Implementation of the communication protocol                         */
/************************************************************************/

void parse_UART_command(const Data data)
{
	switch (data.cmd)
	{
		// Read register
		case 'R':
		UART_tx(MEM_IO_8bit(data.R.addr));
		break;

		// Write register
		case 'W':
		MEM_IO_8bit(data.R.addr) = data.R.value;
		break;

		// Set laser shutters and ALEX
		case 'L':
		sys.lasers_in_use = data.lasers.lasers_in_use & SHUTTERS_MASK;
		sys.ALEX_enabled = data.lasers.ALEX_enabled;
		reset_lasers();
		UART_tx_ok();
		if (sys.status == MANUAL)
		{
			set_lasers(sys.lasers_in_use);
		}
		break;

		// Set acquisition period between frames/bursts
		case 'A':
		sys.acq_period_us = data.uint32_value;
		UART_tx_ok();
		break;

		// Set exposure time
		case 'E':
		sys.exp_time_us = data.uint32_value;
		UART_tx_ok();
		break;

		// Set shutter delay
		case 'D':
		sys.shutter_delay_us = data.uint32_value;
		UART_tx_ok();
		break;

		// Set camera readout delay
		case 'I':
		sys.cam_readout_us = data.uint32_value;
		UART_tx_ok();
		break;

		// Set fluidics trigger
		case 'F':
		sys.fluidics_frame = data.int32_value;
		UART_tx_ok();
		break;

		// Start stroboscopic acquisition
		case 'S':
		UART_tx_ok();
		start_stroboscopic_acq(data.uint32_value);
		break;

		// Start continuous acquisition
		case 'C':
		UART_tx_ok();
		start_continuous_acq(data.uint32_value);
		break;

		// Manually open laser shutters
		case 'M':
		if (sys.status != IDLE)
		{
			UART_tx_err("M: Not in the IDLE state");
			break;
		}
		if (sys.lasers_in_use == 0)
		{
			UART_tx_err("M: All lasers are disabled");
			break;
		}
		UART_tx_ok();
		set_lasers(sys.lasers_in_use);
		sys.status = MANUAL;
		break;

		// Stop acquisition
		case 'Q':
		sys.n_frames = data.uint32_value;
		UART_tx_ok();
		stop_acq();
		break;

		default:
		break;
	}
}

void poll_UART()
{
	Data data;
	if (UART_rx(data.bytes, 5) == OK)
	{
		parse_UART_command(data);
	}
}

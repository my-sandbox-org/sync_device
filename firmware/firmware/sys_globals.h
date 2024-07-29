/**
 * @file sys_globals.h
 * @author Roman Kiselev (roman.kiselev@stjude.org)
 * @brief Global system definitions: pin out and settings.
 * @version 0.4
 * @date 2022-01-10
 *
 * @copyright Copyright (c) 2023
 *
 */
#pragma once

#define F_CPU 16000000UL

#include <avr/io.h>
#include <avr/interrupt.h>

#define VERSION "0.5.0\n"

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))

/***************/
/* Error codes */
/***************/
typedef signed char errcode;
#define OK 0
#define ERR_TIMEOUT -1


/*********************************
HELPFUL BIT MANIPULATION FUNCTIONS
*********************************/
#define bit(b)                         (1UL << (b))
#define bitRead(register, b)            (((register) >> (b)) & 0x01)
#define bitSet(register, b)             ((register) |= (1UL << (b)))
#define bitClear(register, b)           ((register) &= ~(1UL << (b)))
#define bitToggle(register, b)          ((register) ^= (1UL << (b)))
#define bitWrite(register, b, bitvalue) ((bitvalue) ? bitSet(register, b) : bitClear(register, b))

/****************************
PINOUT AND WIRING DEFINITIONS
****************************/
// Laser shutters
const uint8_t CY2_PIN = PINF0; // aka Arduino pin A0
const uint8_t CY3_PIN = PINF1; // aka Arduino pin A1
const uint8_t CY5_PIN = PINF2; // aka Arduino pin A2
const uint8_t CY7_PIN = PINF3; // aka Arduino pin A3
const uint8_t SHUTTERS_MASK = bit(CY2_PIN) | bit(CY3_PIN) | bit(CY5_PIN) | bit(CY7_PIN);
#define SHUTTERS_PORT PORTF
#define SHUTTERS_DDR DDRF

// Fluidic system trigger
const uint8_t FLUIDIC_PIN = PINE4; // aka Arduino pin 2
#define FLUIDIC_PORT PORTE
#define FLUIDIC_DDR DDRE

// Camera trigger
const uint8_t CAMERA_PIN = PINB7; // aka Arduino pin 13
#define CAMERA_PORT PORTB
#define CAMERA_DDR DDRB

/**********************
SYSTEM STATUS VARIABLES
**********************/
enum STATUS
{
	IDLE = 0,    // Doing nothing, waiting for commands
	CONT_ACQ,    // Continuous data acquisition (camera in synchronous readout mode)
	STRB_ACQ,    // Stroboscopic acquisition
	MANUAL,		 // Manually opened laser shutters
};

typedef struct SystemSettings
{
	STATUS   status;
	uint32_t shutter_delay_us;  // Laser shutter delay, 1000us by default
	uint32_t cam_readout_us;    // Camera readout time, 12 ms by default. Depends on the ROI size
	uint32_t exp_time_us;       // Exposure time = duration of laser shutter being open
	uint32_t acq_period_us;     // Time period between subsequent frames or bursts of frames (ALEX)
	uint32_t n_frames;          // 0 means unlimited. In ALEX, it means number of bursts
	uint32_t n_acquired_frames; // Total number of acquired frames (pulses to camera)
	int32_t  fluidics_frame;    // At what frame to trigger fluidics. -1, default, means no trigger
	uint8_t  current_laser;     // Bit pattern indicating currently active laser
	uint8_t  lasers_in_use;     // Bit pattern to control laser shutters. Bit 0: Cy2, bit 3: Cy7
	bool     ALEX_enabled;      // Whether ALEX is active or not
} SystemSettings;

extern SystemSettings sys;

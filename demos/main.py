from avrpy.mega2560 import Mega2560
from avrpy import *
from time import sleep

COM = "COM11"

if __name__ == "__main__":
    avr = Mega2560(COM)

    #    avr.PORTC |= PC0 | PC1 | PC3
    # avr.serial.write(bytearray(b"T" + w(2800) + w(12394) + w(1000) + w(35000)))

if False:
    # Setup timer/counter 1 to work in fast PWM mode
    # Timer/counter control register A, B and C
    #  bit    : 7      6      5      4      3     2    1     0
    #  TCCR1A : COM1A1 COM1A0 COM1B1 COM1B0 -     -    WGM10 WGM11
    #  TCCR1B : ICNC1  ICES1  -      WGM13  WGM12 CS12 CS11  CS10
    #  TCCR1C : FOC1A  FOC1B  -      -      -     -    -     -

    # Set output compare registers
    freq = 16 * MHz / 1024
    # Set pin A on bottom, clear on match. B is inverted
    avr.TCCR1A = COM1A1 | COM1B1 | COM1B0 | WGM11
    # Fast PWM mode, top=ICR1, prescaler 1024
    avr.TCCR1B = WGM13 | WGM12 | CS12 | CS10

    # Set PWM period to 383 ms
    avr.ICR1 = int(383 * ms * freq)
    # Output A (pin 10) stays high for 52.3 ms
    avr.OCR1A = int(52.3 * ms * freq)
    # Output B (pin 9) stays low for 74.5 ms
    avr.OCR1B = int(74.5 * ms * freq)

    # Reset and start timer 1
    avr.TCNT1 = 0

    # Enable outputs on port B
    avr.DDRB = 0xFF

    sleep(2)
    del avr

    # This is demo how to use a context manager for a single transaction
    avr = Mega2560(COM)
    with avr:
        # Set pin A on bottom, clear on match. B is inverted
        avr.TCCR1A = COM1A1 | COM1B1 | COM1B0 | WGM11
        # Fast PWM mode, top=ICR1, prescaler 1024
        avr.TCCR1B = WGM13 | WGM12 | CS12 | CS10

        # Set PWM period to 383 ms
        avr.ICR1 = int(383 * ms * freq)
        # Output A (pin 10) stays high for 52.3 ms
        avr.OCR1A = int(52.3 * ms * freq)
        # Output B (pin 9) stays low for 74.5 ms
        avr.OCR1B = int(74.5 * ms * freq)

        # Reset and start timer 1
        avr.TCNT1 = 0

        # Enable outputs on port B
        avr.DDRB = 0xFF

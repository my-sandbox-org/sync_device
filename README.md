# Synchronization Device for Laser and Camera Control at pTIRF Microscope

This project uses *Arduino Mega2560 rev3* microcontroller board for generation of precise TTL signals to trigger camera and laser shutters of a pTIRF instrument.
*ATMega2560* microcontroller has four 16-bit hardware timers that run independently from the computing core and provide configurable hardware interrupts that are called at predefined time points.
This mechanism allows generation of precise jitter-free waveforms with submillisecond resolution.

The project is based on Arduino hardware but does _not_ use the Arduino library due to overhead it introduces. The minimalistic firmware is developed in low-level C++ using Microchip Studio.

## High-level Overview

We use hardware 16-bit timer/counters 1 and 3 (`TCNT1` and `TCNT3`) with four corresponding interrupt vectors (compare matches `COMPA`, `COMPB`, `COMPC`, and overflow event `OVF`) for generation of signals, and hardware UART module for communication with the host computer. The UART communication runs in the background and does not rely on interrupts to avoid any jitter in the generated signals.

The synchronization device is a state machine with a set of global variables (timings, spectral channels, etc) that control its behavior. The variables are set by the software of the host computer via UART communication protocol (see corresponding section below). The values of global variables persist from acquisition to acquisition. Generally, we distinguish between four global system states:

 * `IDLE` - no signals are generated. Hardware timer/counters 1 and 3 are not running. Laser shutters are closed.
 * `STRB_ACQ` - stroboscopic/ALEX/timelapse imaging (see waveforms below). The CMOS camera runs in the _level trigger_ mode. During each camera exposure the field of view is briefly illuminated by a laser, following by camera readout, and (optional) waiting period for timelapse. If ALEX is enabled, imaging is done in bursts of frames, where each frame within the burst is illuminated by a different laser. There is an optional waiting period between bursts for timelapse ALEX imaging.
 * `CONT_ACQ` - continuous imaging. The CMOS camera is in the _synchronous readout_ mode. The laser shutter(s) is/are open during the entire acquisition, and camera is triggered at precise time points. The first camera frame must be thrown away as it contains noise accumulated before the acquisition has started.
 * `MANUAL` - laser shutters are manually opened without triggering the camera. Can be activated only from `IDLE` state and only if at least one laser is active. Send `Q` command to return to `IDLE` state and close laser shutters.

## Detailed description of imaging modes
### Stroboscopic Imaging

![Stroboscopic Imaging](doc/waveforms/stroboscopic%20imaging.svg)

Before starting the stroboscopic imaging, it is essential to correctly set the following system variables (in microseconds):

* `shutter_delay_us` - time it takes for the laser shutter to fully open (default: 1ms).
* `exp_time_us` - duration of the laser pulse, which defines the exposure time to the laser light (default: 5ms).
* `cam_readout_us` - duration of camera readout (default: 12ms). Depends on ROI settings of the camera.
* `acq_period_us` - time period between two subsequent frames or bursts of frames, for timelapse imaging (default: 100ms).
* `lasers` and `ALEX` - binary mask indicating what lasers are in use, and whether ALEX mode is active or not (default: all four lasers, ALEX is on).

Once the `S` command and requested number of frames `n_frames` is sent (`n_frames`=0 means unlimited), both `TCNT1` and `TCNT3` start synchronous counting from zero. `TCNT1` handles events happening within one frame:

* Overflow interrupt `TIMER1_OVF` opens laser shutter (raising edge). It also counts number of acquired frames.
* Compare match interrupt `COMPA` starts camera exposure (raising edge).
* Compare match interrupt `COMPB` closes laser shutter (falling edge).
* Compare match interrupt `COMPC` stops camera exposure and triggers readout (falling edge). It also pauses `TCNT1` unless it happens within ALEX frame burst. In this case, this routine handles logic of cycling through laser channels.

The period of `TCNT` is equal to `shutter_delay_us` + `exp_time_us` + `cam_readout_us`, and has to be smaller than 4.19s (current limitation). 

The other 16-bit timer, `TCNT3`, keeps track of the `acq_period_us` - time period between two subsequent frames or bursts of frames. The `acq_period_us` has to be at least as long as the duration of a stroboscopic frame times number of spectral channels if case of ALEX. 

The overflow interrupt `TIMER3_OVF` routine checks whether  the requested number of frames has been acquired. If so, it stops acquisition, sends "DONE\n" to the host, and stops both `TCNT1` and `TCNT3`. Otherwise, it resumes `TCNT1`, starting a new frame or a burst of frames. The overflow interrupt `TIMER3_OVF` routine has logic to correctly work with long `acq_period_us` time intervals (above 4.2s). There is no upper limit for `acq_period_us`.

### Continuous Imaging

![Continuous Imaging](doc/waveforms/continuous%20imaging.svg)

Before starting the continuous imaging, it is essential to correctly set the following system variables (in microseconds):

* `shutter_delay_us` - time it takes for the laser shutter to fully open (default: 1ms).
* `exp_time_us` - time period between two camera trigger pulses, i.e. exposure time. It has to be greater than the camera readout time, and depends on the ROI settings (default: 5ms).
* `cam_readout_us` - camera readout time, defines duration of the first "junk" frame (default: 12ms).
* `lasers` - binary mask indicating what lasers are in use.

Once the `C` command and requested number of frames `n_frames` is sent (`n_frames`=0 means unlimited), `TCNT1` starts counting from zero. At the same time the camera is triggered to acquire the first frame. The duration of the first frame is equal to `cam_readout_us` because it contains unwanted signal accumulated in the CMOS before the acquisition has started. The laser shutter is triggered during the first frame, ensuring that it will fully open by the end of the first frame. `TCNT1` handles following interrupts:

* Overflow interrupt `TIMER1_OVF` routine opens laser shutter. It also counts number of acquired frames, and sets `TCNT1` period to be equal to `exp_time_us` after the first discard frame has been acquired. This interrupt occurs before the start of each new frame, with delay given by `shutter_delay_us`.
* Interrupt `COMPA` triggers camera exposure (raising edge), except of the first frame.
* Interrupt `COMPB` gets activated during the last frame and closes laser shutter when called. It also sends "DONE\n" to the host, and stops `TCNT1`, ending the acquisition.
* Interrupt `COMPC` brings camera TTL back to low level. It's exact time point is irrelevant.

The period of `TCNT` is equal to `cam_readout_us` for the first frame and `exp_time_us` for subsequent frames; both have to be smaller than 4.19s (current limitation).


## Host Communication

### UART settings
The UART is running in high speed asynchronous mode at 2,000,000 baud with 8 data bits, 1 stop bit, no handshaking, no parity bits. Usually this is the default UART mode except of the high baudrate.

The synchronization device is recognized by the host as "Arduino Mega 2560". Upon startup, it sends the following message:

```
Synchronization device is ready. Firmware version: <x.y.z>\n
```

The version `<x.y.z>` is defined in `firmware/sys_globals.h`; it is recommended to check it agains the host sofware version. Any change in communication protocol should be reflected in `<x.y>` part of the version number.

### Communication protocol

Each data packet contains 5 bytes that should be transmitted together. Any delay of more than 16ms between subsequent data bytes indicates the end of transmission; incomplete data packet is silently discarded.

The first byte in the data packet is the command, other 4 bytes are arguments (usually in **uint32** format). Most of the arguments map directly to the `SystemSettings` structure of the microcontroller firmware and modify relevant global variables.

Commands `R` and `W` allow to read/write any arbitrary register of the microcontroller and are intented for debugging purposes.

The argument `lasers` of the `L` command is a bitmask where lower four bits represent enabled lasers. Bit order is `bit0`: Cy2 laser, `bit3`: Cy7 laser.
Once the laser states are set, they can be manually opened while the system is in the `IDLE` state using the _M_ command. Sending _Q_ will close the shutters.

_Mode_ column contains _C_ for continuous imaging and _S_ for stroboscopic mode.

Change of laser shutter states has an immediate effect on the waveform being currently generated. Any change in timings is reflected in a subsequent data acquisition and has no effect on a currently generated waveform.

At the end of data acquisition, when requested number of pulses has been generated, a message "DONE\n" is sent to host.

![data packet structure.png](doc/data%20packet%20structure.png)


## Starting Guide

### Wiring

By default, the four laser shutters are attached to Arduino pins A0..A3, and the camera trigger to pin 13. This behavior can be overriden in file `sys_globals.h` if necessary. Consult ATmega2560 datasheet and Arduino pinout to figure out which pins on the Arduino board correspond to which microcontroller pins and I/O ports.

The USB port is used for host communication and provides power to the microcontroller board. The sync device is recognized as a COM port named `Arduino Mega 2560` and may require a driver installation (visit [www.arduino.cc](www.arduino.cc)).

### Firmware

You'll need Microchip Studio (free) to build the firmware. Open the solution file and build the **Release** target.

After that, you can upload the firmware to the microcontroller using the Arduino programmer which is embedded into the board. It is not supported out of the box by the Microchip studio but it's [pretty straightforward to add it](https://youtu.be/zEbSQaQJvHI). Brifly, open Arduino IDE, in preferences set "show verbose output when uploading", and locate the COM port number corresponding to your microcontroller (menu Tools->Port). Run a test upload and pay attention to how `avrdude` was invoked in the command line.
Then go to Microchip Studio and open menu _Tools_->_External Tools_. Register `ArduinoBootloader` tool where the executable is

```
C:\Program Files (x86)\Arduino\hardware\tools\avr/bin/avrdude.exe
```

and the arguments are (update them if necessary, especially pay attention to the microcontroller type and COM port)

```
-C"C:\Program Files (x86)\Arduino\hardware\tools\avr/etc/avrdude.conf" -v -patmega2560 -cwiring -PCOM11 -b115200 -D -Uflash:w:"$(ProjectDir)Release\$(TargetName).hex":i 
```

Make sure that the build target is "Release" and not "Debug".


#### Overview of the firmware code

* `main.cpp` contains default system settings and the firmware starting point.
* `sys_globals.h` provides definitions of all global variables and helper functions.
* `timers.h` and `timers.cpp` control `TCNT1` and `TCNT3` and contain logic of waveform generation.
* `triggers.h` and `triggers.cpp` contain functions to initialize IO and send TTL pulses, as well as functions to work with ALEX.
* `uart.h` and `uart.cpp` implement communication protocol. They also activate 8-bit `TCNT0` to keep track of the communication timeout, but don't use any interrupts. The `poll_UART()` function is repeatedly called in the main loop when there is nothing else to do.


### Python module

The `avrpy` module provides a high-level Python API to work with the synchronization device. To install it and all dependencies, open terminal with active python environment in the root folder (next to `setyp.py`), and use `pip`:

```
pip install .
```


# Known issues

* No sanity checks are done on the provided timing values (yet). The system would not send back `ERR` response for nonsense inputs and will silently generate an incorrect waveform.
* Maximum exposure time is currently limited to 4.19 seconds. The value is defined by system oscillator running at 16 MHz, 1/1024 prescaler providing clock pulses every 64µs, and the size of 16-bit timer that can count up to 65536× 64µs pulses. There is no upper limit on the acquisition period for timelapse, though.
* Continuous acquisition does not work reliably when camera exposure time is close to shutter opening delay (very fast imaging).
* The timing intervals are changed in 64µs steps, which is very coarse for sub-millisecond imaging. The hardware prescaler supports finer quantization intervals but the implementation is quite challenging.
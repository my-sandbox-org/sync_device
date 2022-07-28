from argparse import ArgumentError
from cProfile import run
from numpy import byte
from serial import Serial
from enum import Enum
from time import sleep
from .__version__ import __version__
from .constants import ms, MHz
from ctypes import c_uint8
from ctypes import c_uint16
from ctypes import c_uint32
from ctypes import c_int32


def cu8(value):
    assert value < 2**8, "value exceeds 8 bit"
    return c_uint8(value)


def cu16(value):
    assert value < 2**16, "value exceeds 16 bit"
    return c_uint16(value)


def cu32(value):
    assert value < 2**32, "value exceeds 32 bit"
    return c_uint32(value)


def c32(value):
    assert -(2**31) < value < 2**31, "value exceeds 32 bit"
    return c_int32(value)


class SyncDeviceError(ValueError):
    def __init__(self, reply, message="Incorrect args supplied to the sync device."):
        self.reply = reply
        self.message = message + "\nDevice reply:\n -> " + reply
        super().__init__(self.message)


class Port(Serial):
    def __enter__(self):
        self.reset_input_buffer()
        return self

    def __exit__(self, *args, **kwargs):
        reply = self.readline().strip().decode()
        if reply == "OK":
            return True
        raise SyncDeviceError(reply)


class RegisterBase(Enum):
    """
    Base class to create microcontroller-specific list of registers.
    Stores register name, address, and type (8-bit or 16-bit).
    """

    def __repr__(self):
        return f"{self.name}: {self.bits}-bit register at {self.addr}"

    @property
    def bits(self):
        return self.value[1]

    @property
    def addr(self):
        return self.value[0]


def _compare_versions(v1, v2):
    return {
        k: a == b
        for k, a, b in zip(["major", "minor", "patch"], v1.split("."), v2.split("."))
    }


def pad(data: bytearray, length=5):
    return data + bytearray([0] * (length - len(data)))


class AVR_Base(object):
    """
    Abstract AVR microcontroller unit (MCU). You can read/write registers if you know their address.
    However, it is recommended to derive a class for a specific MCU by calling `define_AVR` with list of registers
    """

    def __init__(self, port, baudrate):
        self.com = Port(port, baudrate=baudrate)
        if not self.com.is_open:
            self.com.open()

        # Opening of the serial port resets Arduino
        # We are going to wait for it to start up
        self.com.timeout = 5  # seconds
        msg = self.com.readline().strip().decode()
        self.com.timeout = 10 * ms

        # Ensure that firmware and driver have the same version
        msg_template = "Arduino is ready. Firmware version: "
        if msg != msg_template + __version__:
            if msg.find(msg_template) != 0:
                raise ConnectionError(
                    f"Could not connect to Arduino on port {port};\n"
                    + f"Received message:\n{msg}\n"
                    + f"Expected message:\n{msg_template + __version__}"
                )
            v = msg[len(msg_template) :]

            version_match = _compare_versions(v, __version__)
            if not version_match["major"] or not version_match["minor"]:
                raise RuntimeWarning(
                    f"Version mismatch: driver {__version__} != firmware {v}"
                )

        self.com.reset_input_buffer()
        sleep(10 * ms)

        self._transaction_mode_ = False
        self._buffer_ = bytearray()

        # properties
        self._fluidics_delay = 0
        self._interframe_time_us = 100000
        self._strobe_duration_us = 25000
        self._ALEX_cycle_delay_us = 0

    def __del__(self):
        self.com.close()

    def __enter__(self):
        """
        Enter a transaction - cache all outgoing data in a buffer
        """
        self._transaction_mode_ = True
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """
        Exit a transaction - transmit all cached data to the microcontroller
        """
        self.com.write(self._buffer_)
        self._buffer_ = bytearray()
        self._transaction_mode_ = False

    def get_register(self, register: RegisterBase):
        if register.bits == 8:
            return self._get_8bit_register(register.addr)
        elif register.bits == 16:
            return self._get_16bit_register(register.addr)
        else:
            ValueError(f"Unknown register {register}")

    def set_register(self, register: RegisterBase, x):
        if register.bits == 8:
            self._set_8bit_register(register.addr, x)
        if register.bits == 16:
            self._set_16bit_register(register.addr, x)
        else:
            ValueError(f"Unknown register {register}")

    def _get_8bit_register(self, addr):
        self.com.reset_input_buffer()
        self.com.write(pad(b"R" + cu8(addr)))
        return ord(self.com.read(1))

    def _get_16bit_register(self, addrL):
        byte_L = self._get_8bit_register(addrL)
        byte_H = self._get_8bit_register(addrL + 1)
        return byte_H << 8 | byte_L

    def _set_8bit_register(self, addr, x):
        cmd = pad(b"W" + cu8(addr) + cu8(x))
        if self._transaction_mode_:
            self._buffer_ += cmd
            if len(self._buffer_) > 63:
                raise MemoryError("Buffer overflow: more than 64 bytes of data cached.")
        else:
            self.com.write(cmd)

    def _set_16bit_register(self, addrL, x):
        byte_L = x & 0xFF
        byte_H = x >> 8
        self._set_8bit_register(addrL + 1, byte_H)
        self._set_8bit_register(addrL, byte_L)

    @property
    def fluidics_delay(self):
        """
        Delay between the fluidic trigger and the start of the image acquisition
        """
        return self._fluidics_delay

    @fluidics_delay.setter
    def fluidics_delay(self, us=0):
        with self.com as com:
            com.write(pad(b"F" + c32(us)))

    @property
    def interframe_time_us(self):
        """
        Time delay between two camera frames
        """
        return self._interframe_time_us

    @interframe_time_us.setter
    def interframe_time_us(self, us=100_000):
        with self.com as com:
            com.write(pad(b"I" + cu32(us)))

    @property
    def strobe_duration_us(self):
        """
        Duration of the laser pulse during a stroboscopic image acquisition
        """
        return self._strobe_duration_us

    @strobe_duration_us.setter
    def strobe_duration_us(self, us=25_000):
        with self.com as com:
            com.write(pad(b"E" + cu32(us)))

    @property
    def ALEX_cycle_delay_us(self):
        """
        Time delay between two cycles of ALEX
        """
        return self._ALEX_cycle_delay_us

    @ALEX_cycle_delay_us.setter
    def ALEX_cycle_delay_us(self, us=25_000):
        with self.com as com:
            com.write(pad(b"A" + cu32(us)))

    def start_continuous(self, N=0):
        """Start continuous image acquisition"""
        with self.com as com:
            com.write(pad(b"C" + cu32(N)))

    def start_stroboscopic(self, N=0):
        """Start stroboscopic image acquisition"""
        with self.com as com:
            com.write(pad(b"S" + cu32(N)))

    def stop(self):
        """
        Stop running camera trigger
        """
        with self.com as com:
            com.write(pad(b"Q"))

    def _bitlist2int(self, bitlist, rev=False):
        """Convert list of bits into integer. Example: [1, 1, 0, 1, 0] => b11011"""
        out = 0
        if rev:
            bitlist = reversed(bitlist)
        for bit in bitlist:
            out = (out << 1) | bit
        return out

    def set_shutters(self, active, idle=None, ALEX=False):
        """
        Set shutter state for active and idle cases.
        If idle is not provided, it will be the inverse of active.
        Expected input: list of four values, order: cy2, cy3, cy5, cy7.
        """
        a = self._bitlist2int(active, rev=True)
        i = self._bitlist2int(idle, rev=True) if idle is not None else 0xFF - a
        ALEX = 1 if ALEX else 0
        with self.com as com:
            com.write(pad(b"L" + cu8(a) + cu8(i) + cu8(ALEX)))


def define_AVR(RegisterList: RegisterBase):
    """
    Creates class representing a specific microcontroller based on a list of registers
    """

    class AVR(AVR_Base):
        def __init__(self, port, baudrate=2 * MHz):
            super().__init__(port, baudrate)

    for register in RegisterList:

        def _getter(self, register=register):
            return self.get_register(register)

        def _setter(self, x, register=register):
            self.set_register(register, x)

        prop = property(fget=_getter, fset=_setter)

        setattr(AVR, register.name, prop)

    return AVR

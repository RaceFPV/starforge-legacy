#ifndef SERIAL_GUARD_H
#define SERIAL_GUARD_H

// The USB serial port carries RotorHazard's binary node protocol, so any
// text the system writes to it (ESP-IDF log lines, Wi-Fi/PHY driver
// messages, ROM printf) can be parsed as a command or corrupt a response.
//
// On the ESP32-C3/C5/C6/S3 the ESP-IDF console's secondary output is the same
// USB Serial/JTAG port Arduino uses for Serial, so this matters there most.
//
// Call silenceSystemOutput() first thing in setup(). It turns off ESP-IDF
// logging and the ROM printf channels. Panic/crash output is not affected:
// the panic handler writes to the hardware directly.
void silenceSystemOutput();

#endif  // SERIAL_GUARD_H

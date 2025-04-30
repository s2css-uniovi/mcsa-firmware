# Firmware for current sampling monitor

This repo contains the source code of the firmware of the 4-channel current sampling monitor.

The multi-threaded firmware defines two tasks to overlap sampling with data transmission to an MQTT broker, so the module may sample data up to 40kHz.

Some MQTT topics can be used to send commands to the module:
 * `pump-monitor/cmd/run`: start sampling: 1: start, 0: stop (retained).
 * `pump-monitor/cmd/input`: select input: 1-4 (retained)
 * `pump-monitor/cmd/avg-count`: select the number of samples to average: 1-5 (retained)
 * `pump-monitor/cmd/sample-count`: select the number of samples to read: 1-40000 (retained)
 * `pump-monitor/cmd/sampling-freq`: select the sampling frequency: 1-40000 Hz (retained)

The monitor publishes data using the following topics:

 * `pump-monitor/inputX/data`: publish samples of 16 bits, where X is the currently active input.
 * `pump-monitor/state`: publish sampling state: 1: running, 0: stopped (retained)
 * `pump-monitor/connected`: publish connection state: 1: connected, 0: disconnected (retained)

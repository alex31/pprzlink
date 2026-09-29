#!/usr/bin/env python3
"""Exercise the public-API clients as real external processes, without radio hardware."""
import argparse
import os
from pathlib import Path
import socket
import struct
import subprocess
from LinkParityTest import Process, SerialWire, free_port, pprz, require


def serial_aircraft(executable, dictionary):
    wire = SerialWire()
    process = subprocess.Popen([executable, dictionary, wire.path], stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True)
    try:
        altitude = bytes([42, 0, 1, 42]) + struct.pack('<f', 123.5)
        require(wire.frame(4) == altitude, 'Aircraft example did not send its altitude')
        wire.inject(bytes([0, 42, 2, 8]))
        require(wire.frame(4) == bytes([42, 0, 1, 5]), 'Aircraft example did not answer PING')
        output, errors = process.communicate(timeout=4)
        require(process.returncode == 0 and 'Answered PING (8 received bytes)' in output, output + errors)
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        wire.close()


def udp_recorder(executable, dictionary):
    process = subprocess.Popen([executable, dictionary, '0'], stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True)
    try:
        line = process.stdout.readline().strip()
        require(line.startswith('Listening on '), f'UDP example did not expose its bound port: {line}')
        destination = ('127.0.0.1', int(line.split()[-1]))
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as first, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as second:
            first.bind(('127.0.0.1', 0))
            second.bind(('127.0.0.1', 0))
            def message(altitude):
                return pprz(bytes([42, 0, 1, 42]) + struct.pack('<f', altitude))
            first.sendto(message(10) + message(20), destination)
            second.sendto(message(30), destination)
            output, errors = process.communicate(timeout=4)
            expected = [f'127.0.0.1:{first.getsockname()[1]} [12 bytes] 42 CLIENT_ALTITUDE {value}.000000'
                        for value in [10, 20]]
            expected.append(f'127.0.0.1:{second.getsockname()[1]} [12 bytes] 42 CLIENT_ALTITUDE 30.000000')
            require(process.returncode == 0 and sorted(output.splitlines()) == sorted(expected), output + errors)
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()


def ivy_receiver(executable, bus, dictionary):
    domain = f'127.255.255.255:{free_port()}'
    peer = Process([bus, domain], dict(os.environ))
    process = None
    try:
        peer.wait(lambda text: text == 'TEST_BUS_READY')
        process = subprocess.Popen([executable, dictionary, domain], stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True)
        peer.wait(lambda text: text == 'ivy-receiver-example ready')
        peer.send('42 CLIENT_ALTITUDE 123.5')
        output, errors = process.communicate(timeout=4)
        require(process.returncode == 0 and 'Aircraft 42: 123.5 m' in output, output + errors)
    finally:
        if process and process.poll() is None:
            process.kill()
            process.wait()
        peer.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--udp', required=True)
    parser.add_argument('--ivy')
    parser.add_argument('--bus')
    args = parser.parse_args()
    dictionary = str(Path(__file__).resolve().parents[1] / 'examples/clients/client_messages.xml')
    serial_aircraft(args.serial, dictionary)
    print('Serial aircraft: altitude and PING/PONG passed', flush=True)
    udp_recorder(args.udp, dictionary)
    print('UDP recorder: concatenated frames and distinct peers passed', flush=True)
    if args.ivy:
        require(args.bus is not None, 'Ivy testing requires the test peer')
        ivy_receiver(args.ivy, args.bus, dictionary)
        print('Ivy receiver: scoped subscription and converted field read passed', flush=True)


if __name__ == '__main__':
    main()

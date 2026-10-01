#!/usr/bin/env python3
"""Exchange workshop messages between two SDK clients through connected PTYs."""
import argparse
from contextlib import contextmanager
import os
from pathlib import Path
import pty
import select
import subprocess
import tempfile
import threading
import time
import tty
from LinkParityTest import Process, require
from SocatSessionTest import ports


@contextmanager
def connected_ports(controller, environment):
    if controller:
        try:
            started = subprocess.run([controller, '-socat', 'start'], env=environment,
                                     capture_output=True, text=True, timeout=5)
            require(started.returncode == 0, started.stdout + started.stderr)
            yield [str(port) for port in ports(started)]
        finally:
            stopped = subprocess.run([controller, '-socat', 'stop'], env=environment,
                                     capture_output=True, text=True, timeout=5)
            require(stopped.returncode == 0, stopped.stdout + stopped.stderr)
    else:
        # SDK clients also work without Ivy or the link executable. Connect two
        # PTYs in Python so this test does not require socat in that configuration.
        first_master, first_slave = pty.openpty()
        second_master, second_slave = pty.openpty()
        masters = [first_master, second_master]
        for descriptor in [first_slave, second_slave]:
            tty.setraw(descriptor)
        finished = threading.Event()
        errors = []

        def relay():
            try:
                while not finished.is_set():
                    for source in select.select(masters, [], [], .05)[0]:
                        data = os.read(source, 4096)
                        target = second_master if source == first_master else first_master
                        offset = 0
                        while offset < len(data):
                            offset += os.write(target, data[offset:])
            except OSError as error:
                errors.append(error)

        worker = threading.Thread(target=relay, daemon=True)
        worker.start()
        try:
            yield [os.ttyname(first_slave), os.ttyname(second_slave)]
            require(not errors, f'PTY relay failed: {errors}')
        finally:
            finished.set()
            worker.join(timeout=1)
            for descriptor in [first_master, first_slave, second_master, second_slave]:
                os.close(descriptor)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--requester', required=True)
    parser.add_argument('--responder', required=True)
    parser.add_argument('--controller')
    args = parser.parse_args()
    dictionary = str(Path(__file__).resolve().parents[1] / 'examples/clients/guide_messages.xml')
    with tempfile.TemporaryDirectory(prefix='pty-peers-test-') as temporary:
        environment = dict(os.environ, XDG_RUNTIME_DIR=temporary)
        for requester_first in [True, False]:
            processes = []
            with connected_ports(args.controller, environment) as pair:
                try:
                    order = [('Requester', args.requester, pair[0]), ('Responder', args.responder, pair[1])]
                    if not requester_first:
                        order.reverse()
                    by_name = {}
                    for name, executable, port in order:
                        process = Process([executable, dictionary, port], environment)
                        processes.append(process)
                        by_name[name] = process
                        process.wait(lambda line, name=name: line.startswith(name + ' ready on '))
                        time.sleep(.05)
                    by_name['Requester'].wait(lambda line: line == 'Aircraft 42: 123.5 m (12 received bytes)')
                    by_name['Responder'].wait(lambda line: line == 'Answered GUIDE_ALTITUDE_REQ with 123.5 m')
                    for process in processes:
                        require(process.process.wait(timeout=3) == 0, f'Peer failed: {process.errors}')
                    print('PASS: request/reply, ' + ('requester' if requester_first else 'responder') + ' started first', flush=True)
                finally:
                    for process in reversed(processes):
                        process.close()


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Listener integration tests; no VPN or external network is required."""
import os
import socket
import stat
import subprocess
import tempfile
import time
import unittest

BINARY = os.path.abspath(os.environ.get('OCPROXY', './ocproxy'))


class ListenerTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='ocp-', dir='/tmp')
        self.path = os.path.join(self.tmp.name, 's')
        self.parent, self.child = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
        self.processes = []

    def tearDown(self):
        for proc in self.processes:
            if proc.poll() is None:
                proc.terminate()
            proc.communicate(timeout=5)
        self.parent.close()
        self.child.close()
        self.tmp.cleanup()

    def start(self, *args):
        env = os.environ.copy()
        env.update(VPNFD=str(self.child.fileno()), INTERNAL_IP4_ADDRESS='192.0.2.2',
                   INTERNAL_IP4_MTU='1500')
        proc = subprocess.Popen([BINARY, *args], env=env,
                                pass_fds=(self.child.fileno(),),
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.processes.append(proc)
        return proc

    def wait_for(self, predicate, proc):
        for _ in range(100):
            if predicate():
                return
            if proc.poll() is not None:
                self.fail('ocproxy exited early: %r' % (proc.communicate(),))
            time.sleep(0.02)
        self.fail('listener did not become ready')

    def handshake(self, family, address):
        with socket.socket(family, socket.SOCK_STREAM) as client:
            client.settimeout(2)
            client.connect(address)
            client.sendall(b'\x05\x01\x00')
            self.assertEqual(client.recv(2), b'\x05\x00')
            client.sendall(b'\x05\x02\x00\x01\x7f\x00\x00\x01\x00\x50')
            self.assertEqual(client.recv(10)[:2], b'\x05\x07')

    def test_unix_and_tcp(self):
        with socket.socket() as probe:
            probe.bind(('127.0.0.1', 0))
            port = probe.getsockname()[1]
        proc = self.start('-U', self.path, '-D', str(port))
        self.wait_for(lambda: os.path.exists(self.path), proc)
        self.assertTrue(stat.S_ISSOCK(os.stat(self.path).st_mode))
        self.assertEqual(stat.S_IMODE(os.stat(self.path).st_mode) & 0o077, 0)
        self.handshake(socket.AF_UNIX, self.path)
        self.handshake(socket.AF_INET, ('127.0.0.1', port))

    def test_tcp_only(self):
        with socket.socket() as probe:
            probe.bind(('127.0.0.1', 0))
            port = probe.getsockname()[1]
        proc = self.start('-D', str(port))
        def ready():
            with socket.socket() as client:
                client.settimeout(0.1)
                return client.connect_ex(('127.0.0.1', port)) == 0
        self.wait_for(ready, proc)
        self.handshake(socket.AF_INET, ('127.0.0.1', port))
        self.assertFalse(os.path.exists(self.path))

    def test_existing_file_and_symlink_are_preserved(self):
        for kind in ('file', 'symlink'):
            with self.subTest(kind=kind):
                target = os.path.join(self.tmp.name, 'target')
                with open(target, 'w') as f:
                    f.write('unchanged')
                if kind == 'file':
                    with open(self.path, 'w') as f:
                        f.write('existing')
                else:
                    os.symlink(target, self.path)
                before = os.lstat(self.path)
                proc = self.start('--unix-socks', self.path)
                proc.communicate(timeout=5)
                self.assertNotEqual(proc.returncode, 0)
                after = os.lstat(self.path)
                self.assertEqual((before.st_ino, before.st_mode), (after.st_ino, after.st_mode))
                with open(target) as f:
                    self.assertEqual(f.read(), 'unchanged')
                os.unlink(self.path)

    def test_existing_socket_is_preserved(self):
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as listener:
            listener.bind(self.path)
            listener.listen(1)
            before = os.lstat(self.path)
            proc = self.start('-U', self.path)
            proc.communicate(timeout=5)
            self.assertNotEqual(proc.returncode, 0)
            self.assertEqual(before.st_ino, os.lstat(self.path).st_ino)
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
                client.connect(self.path)

    def test_invalid_paths(self):
        for path in ('', '/' + 'x' * 200):
            with self.subTest(path=path):
                proc = self.start('-U', path)
                proc.communicate(timeout=5)
                self.assertNotEqual(proc.returncode, 0)


if __name__ == '__main__':
    unittest.main(verbosity=2)

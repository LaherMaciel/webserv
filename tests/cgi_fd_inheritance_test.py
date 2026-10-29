#!/usr/bin/env python3
"""Run against ./webserv, or pass a binary path; all fixtures are temporary."""

import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time


PROJECT = Path(__file__).resolve().parent.parent
CGI_SCRIPT = '''import json
import os
from pathlib import Path
import stat
import sys
import time

def inherited_io():
    result = []
    directory = '/proc/self/fd' if Path('/proc/self/fd').exists() else '/dev/fd'
    for name in os.listdir(directory):
        fd = int(name)
        if fd <= 2:
            continue
        try:
            mode = os.fstat(fd).st_mode
        except OSError:
            continue
        if stat.S_ISSOCK(mode) or stat.S_ISFIFO(mode):
            result.append(fd)
    return sorted(result)

def wait_for(name):
    deadline = time.monotonic() + 10
    while not Path(name).exists():
        if time.monotonic() >= deadline:
            raise RuntimeError('fixture gate timed out: ' + name)
        time.sleep(0.01)

mode = os.environ.get('QUERY_STRING', 'inspect')
report = {'inherited_io': inherited_io()}
if mode == 'eof':
    Path('eof.ready').write_text(json.dumps(report))
    wait_for('eof.release')
    report['bytes_read'] = len(sys.stdin.buffer.read())
elif mode == 'holder':
    Path('holder.ready').write_text(json.dumps(report))
    wait_for('holder.release')
sys.stdout.write('Content-Type: application/json\\r\\n\\r\\n')
sys.stdout.write(json.dumps(report))
'''


def free_port():
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        return probe.getsockname()[1]


def connect(port):
    return socket.create_connection(('127.0.0.1', port), timeout=2)


def receive(client):
    data = b''
    while b'\r\n\r\n' not in data:
        part = client.recv(65536)
        if not part:
            raise RuntimeError('EOF before response headers')
        data += part
    head, body = data.split(b'\r\n\r\n', 1)
    lines = head.split(b'\r\n')
    headers = dict(line.split(b':', 1) for line in lines[1:])
    length = int(headers[b'Content-Length'])
    while len(body) < length:
        part = client.recv(65536)
        if not part:
            raise RuntimeError('truncated response body')
        body += part
    return int(lines[0].split()[1]), body[:length]


def send(client, path, body=b'', keep_alive=False):
    method = b'POST' if body else b'GET'
    headers = method + b' ' + path.encode() + b' HTTP/1.1\r\nHost: localhost\r\n'
    if not keep_alive:
        headers += b'Connection: close\r\n'
    if body:
        headers += b'Content-Length: ' + str(len(body)).encode() + b'\r\n'
    client.sendall(headers + b'\r\n' + body)


def wait_for(path, process):
    deadline = time.monotonic() + 2
    while not path.exists():
        if process.poll() is not None:
            raise RuntimeError('server exited before fixture became ready')
        if time.monotonic() >= deadline:
            raise RuntimeError('fixture did not become ready: ' + str(path))
        time.sleep(0.01)
    return json.loads(path.read_text())


def check(name, condition, detail=''):
    print(('PASS  ' if condition else 'FAIL  ') + name +
          (': ' + detail if detail else ''), flush=True)
    return 0 if condition else 1


def run(binary, root):
    port, second_port = free_port(), free_port()
    while second_port == port:
        second_port = free_port()
    (root / 'probe.py').write_text(CGI_SCRIPT)
    (root / 'index.html').write_text('server healthy')
    config = ''
    for endpoint in (port, second_port):
        config += '''server {
listen %s;
host 127.0.0.1;
client_max_body_size 1000000;
location / { root %s; index index.html; allowed_methods GET; }
location /cgi { root %s; cgi_extension .py %s; allowed_methods GET POST; }
}
''' % (endpoint, root, root, sys.executable)
    config_path = root / 'test.conf'
    config_path.write_text(config)
    failures = 0
    clients = []
    with (root / 'server.log').open('wb') as log:
        process = subprocess.Popen([str(binary), str(config_path)],
                                   cwd=str(PROJECT), stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 3
            while True:
                if process.poll() is not None:
                    raise RuntimeError('server failed to start')
                try:
                    idle = connect(port)
                    clients.append(idle)
                    break
                except OSError:
                    if time.monotonic() >= deadline:
                        raise RuntimeError('server did not start listening')
                    time.sleep(0.02)

            # Keep a client and two listeners open when the child is forked.
            with connect(port) as inspector:
                send(inspector, '/cgi/probe.py?inspect')
                status, body = receive(inspector)
                inherited = json.loads(body)['inherited_io']
                failures += check('CGI inherits no server sockets or unrelated pipes',
                                  status == 200 and not inherited, str(inherited))

            reader = connect(port)
            clients.append(reader)
            payload = b'x' * 524288
            send(reader, '/cgi/probe.py?eof', payload, keep_alive=True)
            wait_for(root / 'eof.ready', process)
            reader.shutdown(socket.SHUT_WR)

            # The health response gives the server time to remove reader from
            # poll while it still owns that socket and its CGI pipe ends.
            with connect(second_port) as health:
                send(health, '/')
                failures += check('second listener responds during paused CGI',
                                  receive(health) == (200, b'server healthy'))

            holder = connect(port)
            clients.append(holder)
            send(holder, '/cgi/probe.py?holder')
            inherited = wait_for(root / 'holder.ready', process)['inherited_io']
            failures += check('second CGI excludes first CGI pipes and unpolled client',
                              not inherited, str(inherited))
            (root / 'eof.release').touch()
            try:
                status, body = receive(reader)
                received = json.loads(body)['bytes_read']
                failures += check('CGI receives complete body and EOF while other CGI waits',
                                  status == 200 and received == len(payload))
            except socket.timeout:
                failures += check('CGI receives EOF while other CGI waits', False,
                                  'reader remained blocked until unrelated CGI exits')
            finally:
                (root / 'holder.release').touch()
            failures += check('unrelated CGI completes normally', receive(holder)[0] == 200)

            # Child-side close calls must leave the parent's client usable.
            send(idle, '/')
            failures += check('parent retains its client socket',
                              receive(idle) == (200, b'server healthy'))
        finally:
            (root / 'eof.release').touch()
            (root / 'holder.release').touch()
            for client in clients:
                client.close()
            # Allow existing non-blocking child reaping to finish before exit.
            time.sleep(0.2)
            process.terminate()
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=2)
    return failures


def main():
    binary = Path(sys.argv[1] if len(sys.argv) > 1 else PROJECT / 'webserv').resolve()
    with tempfile.TemporaryDirectory(prefix='webserv-cgi-fds-') as directory:
        root = Path(directory)
        try:
            return 1 if run(binary, root) else 0
        except Exception as error:
            print('FAIL  ' + str(error), file=sys.stderr)
            log = root / 'server.log'
            if log.exists():
                print(log.read_text(errors='replace')[-4000:], file=sys.stderr)
            return 1


if __name__ == '__main__':
    sys.exit(main())

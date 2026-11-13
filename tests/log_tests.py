#!/usr/bin/env python3
"""Build focused C++98 logger tests, then exercise a server with full output pipes."""

import argparse
import http.client
import json
import os
from pathlib import Path
import re
import shlex
import socket
import subprocess
import sys
import tempfile
import time


PROJECT = Path(__file__).resolve().parent.parent


def check(name, condition):
    if not condition:
        raise RuntimeError(name)
    print('PASS  ' + name, flush=True)


def unit_tests(root):
    compiler = shlex.split(os.environ.get('CXX', 'c++'))
    flags = ['-Wall', '-Wextra', '-Werror', '-std=c++98', '-I' + str(PROJECT / 'includes')]
    obj = root / 'Log.o'
    binary = root / 'log_tests'
    subprocess.run(compiler + flags + ['-Dwrite=logTestWrite', '-c', str(PROJECT / 'src/Log.cpp'), '-o', str(obj)], check=True)
    subprocess.run(compiler + flags + [str(PROJECT / 'tests/log_tests.cpp'), str(obj), '-o', str(binary)], check=True)
    os.mkfifo(root / 'fifo')
    subprocess.run([str(binary), str(root)], check=True, timeout=5)


def free_port():
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        return probe.getsockname()[1]


def request(port, method='GET', path='/', body=None, headers=None, chunked=False, timeout=4):
    client = http.client.HTTPConnection('127.0.0.1', port, timeout=timeout)
    try:
        fields = dict(headers or {}, Connection='close')
        client.request(method, path, body=body, headers=fields, encode_chunked=chunked)
        response = client.getresponse()
        return response.status, response.read()
    finally:
        client.close()


def fill_pipe(fd):
    os.set_blocking(fd, False)
    try:
        while True:
            os.write(fd, b'x' * 4096)
    except BlockingIOError:
        pass
    finally:
        os.set_blocking(fd, True)


def configuration(root, port):
    return '''server {
listen %s;
host 127.0.0.1;
server_name localhost;
client_max_body_size 1000000;
error_page 404 %s/missing-error-page.html;
location / { root %s; index index.html; allowed_methods GET; }
location /upload { root %s/upload; upload_store %s/upload; allowed_methods GET POST DELETE; }
location /cgi { root %s/cgi; cgi_extension .py %s; allowed_methods GET POST; }
location /cgi-bin { root %s/www/cgi-bin; cgi_extension .py %s; allowed_methods GET POST; }
}
''' % (port, root, root, root, root, root, sys.executable, PROJECT, sys.executable)


def script_test(name, root, port, binary, extra_env=None):
    env = dict(os.environ, HOST='127.0.0.1', PORT=str(port), BIN=str(binary))
    if extra_env is not None:
        env.update(extra_env)
    command = ['bash', str(PROJECT / 'tests' / name)]
    result = subprocess.run(command, cwd=str(root), env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=45)
    if result.returncode:
        raise RuntimeError(name + '\n' + result.stdout)
    print('PASS  ' + name, flush=True)


def integration_tests(root, binary):
    port = free_port()
    (root / 'upload').mkdir()
    (root / 'cgi').mkdir()
    (root / 'index.html').write_text('healthy')
    scripts = {
        'failed.py': 'import os\nos._exit(1)\n',
        'malformed.py': "print('Broken-Header\\r\\n\\r\\nbody')\n",
        'missing_headers.py': "print('body without headers')\n",
        'status500.py': "print('Status: 500 Internal Server Error\\r\\nContent-Type: text/plain\\r\\n\\r\\napplication error')\n",
    }
    for name, source in scripts.items():
        (root / 'cgi' / name).write_text(source)
    (root / 'cgi' / 'inspect.py').write_text('''import json,os
from pathlib import Path
target=Path('../webserv.log').stat()
inherited=[]
directory='/proc/self/fd' if Path('/proc/self/fd').exists() else '/dev/fd'
for name in os.listdir(directory):
    try:
        info=os.fstat(int(name))
    except OSError:
        continue
    if info.st_dev==target.st_dev and info.st_ino==target.st_ino:
        inherited.append(int(name))
print('Content-Type: application/json\\r\\n\\r\\n'+json.dumps(inherited))
''')
    config = root / 'server.conf'
    config.write_text(configuration(root, port))
    out_read, out_write = os.pipe()
    err_read, err_write = os.pipe()
    descriptors = [out_read, out_write, err_read, err_write]
    process = None
    try:
        process = subprocess.Popen([str(binary), str(config)], cwd=str(root),
                                   stdout=out_write, stderr=err_write)
        deadline = time.monotonic() + 3
        while True:
            if process.poll() is not None:
                raise RuntimeError('server exited during startup')
            try:
                if request(port) == (200, b'healthy'):
                    break
            except OSError:
                if time.monotonic() >= deadline:
                    raise RuntimeError('server did not start')
                time.sleep(.02)
        fill_pipe(out_write)
        fill_pipe(err_write)
        for fd in (out_write, err_write):
            os.close(fd)
            descriptors.remove(fd)

        marker = b'LOGGER_BODY_SECRET_'
        body = (marker * 30000)[:524288]
        code, _ = request(port, 'POST', '/upload/large.bin', body,
                          {'X-Logger-Secret': 'LOGGER_HEADER_SECRET'})
        check('large upload completes with stdout and stderr pipes full', code == 201)
        check('uploaded bytes are intact', request(port, path='/upload/large.bin') == (200, body))
        check('chunked body remains functional',
              request(port, 'POST', '/upload/chunked.bin', iter([b'abc', b'def']), chunked=True)[0] == 201)
        check('missing custom error page falls back safely', request(port, path='/missing')[0] == 404)
        check('method rejection remains functional', request(port, 'POST', '/', b'')[0] == 405)
        (root / 'upload' / 'blocked.bin').mkdir()
        check('failed upload returns 500', request(port, 'POST', '/upload/blocked.bin', b'data')[0] == 500)
        for name in ('failed.py', 'malformed.py', 'missing_headers.py', 'status500.py'):
            check('CGI failure demonstration: ' + name, request(port, path='/cgi/' + name)[0] == 500)

        code, data = request(port, path='/cgi/inspect.py')
        check('CGI closes the inherited log-file descriptor', code == 200 and json.loads(data) == [])
        script_test('cgi_post_test.sh', root, port, binary)
        script_test('cgi_env_test.sh', root, port, binary)
        check('server remains responsive after CGI', request(port) == (200, b'healthy'))

        # Shutdown interrupts a response that is larger than the peer's receive buffer.
        (root / 'large.bin').write_bytes(b'x' * (16 * 1024 * 1024))
        with socket.socket() as peer:
            peer.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)
            peer.settimeout(3)
            peer.connect(('127.0.0.1', port))
            peer.sendall(b'GET /large.bin HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n')
            peer.recv(1024)
            process.terminate()
            check('shutdown completes with output pipes full', process.wait(timeout=3) == 0)
        text = (root / 'webserv.log').read_text()
        check('interrupted response reports bytes sent and remaining',
              re.search(r'Response send aborted .*path=/large.bin bytes_sent=\d+ remaining=[1-9]\d*', text))
        check('log contains request metadata and shutdown',
              'method=POST path=/upload/large.bin status=201' in text
              and 'Server shutdown requested' in text)
        check('custom-page failure is reported by the caller',
              '[WARN] Response: Custom error page unavailable for status=404 file='
              + str(root / 'missing-error-page.html') in text)
        check('routine 404 and 405 summaries use INFO and identify the listener',
              all(re.search(r'\[INFO\] Connection: response_queued fd=\d+ port=' + str(port)
                            + r' method=\w+ path=\S+ status=' + str(code), text)
                  for code in (404, 405)))
        for reason, path in (('Script failed', 'failed.py'), ('Malformed output', 'malformed.py'),
                             ('Malformed output', 'missing_headers.py')):
            check('CGI log identifies ' + reason.lower(),
                  any('[ERROR] CGI: ' + reason in line and 'path=/cgi/' + path in line
                      and 'port=' + str(port) in line for line in text.splitlines()))
        check('valid CGI status 500 is not mistaken for failed execution',
              not any('[ERROR] CGI:' in line and 'path=/cgi/status500.py' in line
                      for line in text.splitlines()))
        check('failed upload identifies operation and filesystem path',
              '[ERROR] Router: Upload open failed file=' + str(root / 'upload' / 'blocked.bin') in text)
        check('body, headers and debug messages are absent by default',
              marker.decode() not in text and 'LOGGER_HEADER_SECRET' not in text and '[DEBUG]' not in text)
        check('every log entry is capped and formatted',
              all(len(line.encode()) + 1 <= 512 and re.match(r'^\[(ERROR|WARN|INFO|DEBUG)\] ', line)
                  for line in text.splitlines()))
    finally:
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=3)
        for fd in descriptors:
            os.close(fd)

    shutdown = root / 'shutdown'
    shutdown.mkdir()
    shutdown_port = free_port()
    shutdown_config = root / 'shutdown.conf'
    shutdown_config.write_text(configuration(root, shutdown_port))
    shutdown_log = shutdown / 'webserv.log'
    with (shutdown / 'server-output.log').open('wb') as output:
        shutdown_process = subprocess.Popen([str(binary), str(shutdown_config)],
                                            cwd=str(shutdown), stdout=output,
                                            stderr=output)
        try:
            deadline = time.monotonic() + 3
            while True:
                if shutdown_process.poll() is not None:
                    raise RuntimeError('shutdown-test server failed to start')
                try:
                    if request(shutdown_port) == (200, b'healthy'):
                        break
                except OSError:
                    if time.monotonic() >= deadline:
                        raise RuntimeError('shutdown-test server did not start')
                    time.sleep(.02)
            script_test('cgi_shutdown_test.sh', shutdown, shutdown_port, binary,
                        {'SERVER_PID': str(shutdown_process.pid),
                         'SERVER_LOG_FILE': str(shutdown_log)})
        finally:
            if shutdown_process.poll() is None:
                shutdown_process.terminate()
                try:
                    shutdown_process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    shutdown_process.kill()
                    shutdown_process.wait(timeout=3)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--unit-only', action='store_true')
    parser.add_argument('--binary', type=Path, default=PROJECT / 'webserv')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='webserv-log-tests-') as directory:
        root = Path(directory)
        try:
            unit_tests(root)
            if not args.unit_only:
                integration_tests(root, args.binary.resolve())
        except Exception as error:
            print('FAIL  ' + str(error), file=sys.stderr)
            log = root / 'webserv.log'
            if log.exists():
                print(log.read_text(errors='replace')[-2000:], file=sys.stderr)
            return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())

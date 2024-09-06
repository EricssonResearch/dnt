from subprocess import Popen, run, run, PIPE, DEVNULL
from pyroute2.netns import setns
from select import *
from socket import *
import platform
import shlex
import time
import os

OUT_NONE = 1
OUT_PIPE = 2
OUT_STDOUT = 3

PY_VER_MAJOR = platform.sys.version_info.major
PY_VER_MINOR = platform.sys.version_info.minor

#platform.sys.version_info.major
def exec_bg(cmd, out=OUT_NONE):
    """
    Execute the @cmd in the background, with optional
    stdout/stderr output saved to stdouts map
    Nonblockig, @cmd running in the bacground

    @out tells what would we like to do the output. Valid values:
    'OUT_NONE' - no output at all (devnull)
    'OUT_STDOUT' - output straight into the stdout
    'OUT_PIPE' - output saved into pipe, use Popen.communicate() on return value

    @return Popen object with the running command
    """
    cmdout = -1
    if out == OUT_NONE:
        cmdout = DEVNULL
    elif out == OUT_STDOUT:
        cmdout = None
    elif out == OUT_PIPE:
        cmdout = PIPE
    else:
        print("exec_bg: invalid output specified.")
        print("Use: none, pipe or stdout")
    kwargs = {
        "stdout" : cmdout,
        "stderr" : cmdout,
        "text" : True
    }
    if PY_VER_MAJOR >= 3 and PY_VER_MINOR >= 10:
        pass
        # kwargs["pipesize"] = 10000000
    p = Popen(shlex.split(cmd), **kwargs)
    return p

def exec_fg(cmd, silent=True, timeout=None):
    """
    Execute the @cmd in foreground, with optional
    stdout/stderr output saved to stdouts map
    Blocking until the command returns
    @return CompletedProcess object of the finished command
    """
    os.environ['LC_ALL']='C'
    kwargs = {
        "text" : True,
        "capture_output" : silent,
        "timeout" : timeout
    }
    if PY_VER_MAJOR >= 3 and PY_VER_MINOR >= 10:
        pass
        # kwargs["pipesize"] = 10000000
    r = run(shlex.split(cmd), **kwargs)
    return r

def get_mininet_bash_pids():
    '''
    Return a ductionary with mininet hostnames and pids
    Format is { hostname : pid }
    '''
    bashs = {}
    with Popen(['ps', 'a'], stdout=PIPE, text=True) as proc:
        out, err = proc.communicate()
        if "mininet" not in out:
            return None
        for line in out.splitlines():
            if "mininet" in line:
                tmp = line.split()
                pid, hostname = tmp[0], tmp[-1].split(":")[1]
                bashs[hostname] = pid
    return bashs

def switch_netns(hostname = None):
    '''
    Switch the network namespace of the process to the
    namespace of the given mininet host's net namespace.
    If @hostname == None (default) it will swtich back to
    the default namespace.
    Note: Assumes that the caller is in the defalt netns!
    '''
    netnspaces = get_mininet_bash_pids()
    if hostname == None:
        pid = 1
    else:
        pid = netnspaces[hostname]
    setns(f"/proc/{pid}/ns/net")


class Telnet:
    def __init__(self, ip : str, port : int, auto_recv : bool = False) -> None:
        self.sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP)
        self.sock.connect((ip, port))
        self.auto_recv = auto_recv
        if auto_recv:
            _ = self.sock.recv(1000) # OAM ready

    def send(self, msg : str) -> None:
        msg += "\r\n"
        self.sock.send(msg.encode())
        if self.auto_recv:
            _ = self.sock.recv(1000)
        time.sleep(0.1)

    def recv(self, timeout : float = 0.1, aggregate : bool = False) -> str:
        """
        @timeout: if no message received, timeout and return with empty msg
        @aggregate: collect and concatenate messages until timeout and return
                    as one message
        retuns: the received message or message aggregate
        """
        start = time.time()
        self.sock.setblocking(False)

        msgstr = ""
        while True:
            incoming, _, _ = select([self.sock], [], [], timeout)
            for sock in incoming:
                msg = sock.recv(10000)
                if not msg:
                    continue
                msgstr += msg.decode()
            if start + timeout < time.time():
                break
            if not aggregate:
                break

        self.sock.setblocking(True)
        return msgstr

    def close(self) -> None:
        try:
            self.sock.setblocking(True)
            self.sock.send(b"\x04")
            self.sock.close()
            time.sleep(0.1)
        except:
            time.sleep(0.1)
            pass

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

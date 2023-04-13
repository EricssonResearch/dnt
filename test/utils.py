from subprocess import Popen, run, run, PIPE, DEVNULL
import shlex

def exec_bg(cmd, silent=False):
    """
    Execute the @cmd in the background, with optional
    stdout/stderr output saved to stdouts map
    Nonblockig, @cmd running in the bacground
    @return Popen object with the running command
    """
    cmdout = PIPE
    if silent:
        cmdout = DEVNULL
    p = Popen(shlex.split(cmd),
              pipesize=100000000,
              stdout=cmdout,
              stderr=cmdout)
    return p

def exec_fg(cmd, silent=True, timeout=None):
    """
    Execute the @cmd in foreground, with optional
    stdout/stderr output saved to stdouts map
    Blocking until the command returns
    @return CompletedProcess object of the finished command
    """
    r = run(shlex.split(cmd),
            pipesize=100000000,
            text=True,
            capture_output=silent,
            timeout=timeout)
    return r

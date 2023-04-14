from subprocess import Popen, run, run, PIPE, DEVNULL
from enum import Enum
import shlex

OUT_NONE = 1
OUT_PIPE = 2
OUT_STDOUT = 3

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

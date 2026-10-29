#!/usr/bin/env python3
"""Task-owned child boundary: time, streamed output, process-group closure."""
import os
import selectors
import signal
import subprocess
import time


def run(command, timeout=60, env=None):
    child=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=env,start_new_session=True)
    output={child.stdout:bytearray(),child.stderr:bytearray()}
    end=time.monotonic()+timeout
    try:
        with selectors.DefaultSelector() as selector:
            for pipe in output:
                selector.register(pipe,selectors.EVENT_READ)
            while selector.get_map():
                remaining=end-time.monotonic()
                if remaining<=0:
                    raise TimeoutError('bounded child deadline exceeded')
                for key,_ in selector.select(min(remaining,.1)):
                    chunk=os.read(key.fileobj.fileno(),65536)
                    if not chunk:
                        selector.unregister(key.fileobj)
                    elif len(output[key.fileobj])+len(chunk)>1024*1024:
                        raise OverflowError('bounded child output exceeded 1MiB per stream')
                    else:
                        output[key.fileobj].extend(chunk)
            code=child.wait(timeout=max(.001,end-time.monotonic()))
        return subprocess.CompletedProcess(command,code,bytes(output[child.stdout]),bytes(output[child.stderr]))
    finally:
        try:
            os.killpg(child.pid,signal.SIGKILL)
        except ProcessLookupError:
            pass
        if child.poll() is None:
            child.wait(timeout=5)
        child.stdout.close();child.stderr.close()

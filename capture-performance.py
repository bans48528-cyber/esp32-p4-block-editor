"""Capture performance logs without resetting the screen during interaction."""
import argparse,time,serial
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--port',default='COM10')
p.add_argument('--seconds',type=int,default=55)
p.add_argument('--output',default='device-scroll-perf.log')
a=p.parse_args()
with serial.Serial(a.port,115200,timeout=.2) as s, Path(a.output).open('ab') as f:
    print('Performance capture ready; no reset requested.',flush=True)
    end=time.monotonic()+a.seconds
    while time.monotonic()<end:
        data=s.read(max(s.in_waiting,1))
        if data:
            f.write(data);f.flush()
            print(data.decode('utf-8',errors='replace'),end='',flush=True)

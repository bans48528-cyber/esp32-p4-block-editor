"""Reset the selected USB serial/JTAG device and capture a bounded boot log."""
import argparse, time
from pathlib import Path
import serial
p=argparse.ArgumentParser()
p.add_argument('--port',default='COM10')
p.add_argument('--seconds',type=int,default=12)
p.add_argument('--output',default='device-boot.log')
a=p.parse_args()
with serial.Serial(a.port,115200,timeout=.2) as s:
    s.dtr=False
    s.rts=True
    time.sleep(.15)
    s.rts=False
    deadline=time.monotonic()+a.seconds
    data=bytearray()
    while time.monotonic()<deadline:
        data.extend(s.read(max(s.in_waiting,1)))
Path(a.output).write_bytes(data)
print(data.decode('utf-8',errors='replace'))

#!/usr/bin/env python3

from trace_lib import Trace

t = Trace(__file__.replace(".py", ".trf"), 1)

# Fill up set 7
t.read(0xE0)
t.read(0x10E0)
t.read(0x20E0)
t.read(0x30E0)
t.read(0x40E0)
t.read(0x50E0)
t.read(0x60E0)
t.read(0x70E0)

# Set is full, now test eviction policy.
t.read(0x80E0)

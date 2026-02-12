#!/usr/bin/env python3

from trace_lib import Trace

t = Trace(__file__.replace(".py", ".trf"), 1)

# Read address 247 which should end up in line 7.
t.read(0xF7)

# All the following bytes should be read from line 7.
t.read(0xE0)
t.read(0xE1)
t.read(0xE2)
t.read(0xE3)
t.read(0xE4)
t.read(0xE5)
t.read(0xE6)
t.read(0xE7)
t.read(0xE8)
t.read(0xE9)
t.read(0xEA)
t.read(0xEB)
t.read(0xEC)
t.read(0xED)
t.read(0xEE)
t.read(0xEF)
t.read(0xF0)
t.read(0xF1)
t.read(0xF2)
t.read(0xF3)
t.read(0xF4)
t.read(0xF5)
t.read(0xF6)
t.read(0xF7)
t.read(0xF8)
t.read(0xF9)
t.read(0xFA)
t.read(0xFB)
t.read(0xFC)
t.read(0xFD)
t.read(0xFE)
t.read(0xFF)

# Next byte should be a miss, so read from line 8.
t.read(0x100)
# Byte before block should also be a miss.
t.read(0xDE)

t.close()

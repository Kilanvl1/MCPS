from trace_lib import Trace  # Assuming your class is in trace_lib.py


def generate_stress_test():
    # Initialize trace for 1 processor
    t = Trace(__file__.replace(".py", ".trf"), 1)

    # 1. Spatial Locality: Sequential Array Initialization
    # Simulates: for(int i=0; i<256; i++) array[i] = 0;
    # Results in 1 Read Miss per 32-byte line, then 7 Read Hits.
    ARRAY_START = 0x1000
    for addr in range(ARRAY_START, ARRAY_START + 1024, 4):
        t.write(addr)

    # 2. Temporal Locality: Frequent Variable Access
    # Simulates: A "Hot" loop counter or accumulator.
    # Results in 1 Miss followed by many Hits.
    LOOP_VAR = 0x2000
    for _ in range(50):
        t.read(LOOP_VAR)
        t.write(LOOP_VAR)

    # 3. Cache Capacity & Conflict Stress: Filling an 8-way Set
    # We target Set 7 (Index bits 5-11).
    # Stride of 4096 (0x1000) keeps the index same but changes the Tag.
    SET_7_BASE = 7 << 5  # 0x00E0
    STRIDE = 4096  # 0x1000

    # Fill all 8 ways with dirty data
    for i in range(8):
        t.write(SET_7_BASE + (i * STRIDE))

    # Trigger 9th access: This forces an LRU eviction of a dirty line.
    # Result: 100 cycle Write-Back + 100 cycle Read Miss.
    t.read(SET_7_BASE + (8 * STRIDE))

    t.close()
    print("Trace 'app_sim.trf' generated successfully.")


if __name__ == "__main__":
    generate_stress_test()

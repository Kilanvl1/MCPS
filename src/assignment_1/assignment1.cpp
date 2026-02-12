/*
 * File: assignment1.cpp
 *
 * Framework to implement Task 1 of the Multi-Core Processor Systems lab
 * session. This uses the framework library to interface with tracefiles which
 * will drive the read/write requests
 *
 * Author(s): Michiel W. van Tol, Mike Lankamp, Jony Zhang,
 *            Konstantinos Bousias, Simon Polstra
 *
 */

#include <iostream>
#include <iomanip>
#include <systemc>

#include "psa.h"

using namespace std;
using namespace sc_core; // This pollutes namespace, better: only import what you need.

SC_MODULE(Memory) {
    public:
    enum Function { FUNC_READ, FUNC_WRITE };

    enum RetCode { RET_READ_DONE, RET_WRITE_DONE };

    // Ports that this module uses to communicate with other modules
    sc_in<bool> Port_CLK;
    sc_in<Function> Port_Func;
    sc_in<uint64_t> Port_Addr;
    sc_out<RetCode> Port_Done;
    sc_inout_rv<64> Port_Data;

    SC_CTOR(Memory) {
        SC_THREAD(execute);
        sensitive << Port_CLK.pos();
        dont_initialize();
    }

    private:

    void execute() {
        while (true) {
            wait(Port_Func.value_changed_event());

            Function f = Port_Func.read();
            uint64_t addr = Port_Addr.read();
            uint64_t data = 0;
            if (f == FUNC_WRITE) {
                data = Port_Data.read().to_uint64();
                log(name(), "received write on address", addr, "with data", data);
            } else {
                log(name(), "received read on address", addr);
            }

            // This simulates memory read/write delay
            wait(100);

            if (f == FUNC_READ) {
                Port_Data.write(addr * 10);
                Port_Done.write(RET_READ_DONE);
                wait();
                Port_Data.write(float_64_bit_wire); // string with 64 "Z"'s
            } else {
                Port_Done.write(RET_WRITE_DONE);
            }
        }
    }
};

// cache module between memory and CPU


SC_MODULE(Cache) {
public:
    // Clock
    sc_in<bool> Port_CLK;

    // ===== CPU-side interface =====
    sc_in<Memory::Function> Port_CPUFunc;
    sc_in<uint64_t>         Port_CPUAddr;
    sc_inout_rv<64>         Port_CPUData;   // CPU writes here; cache returns read data here
    sc_out<Memory::RetCode> Port_CPUDone;
    sc_in<bool> Port_CPUReq;


    // ===== Memory-side interface =====
    sc_out<Memory::Function> Port_MemFunc;
    sc_out<uint64_t>          Port_MemAddr;
    sc_inout_rv<64>           Port_MemData;
    sc_in<Memory::RetCode>    Port_MemDone;

    SC_CTOR(Cache) : time_counter(0) {
        SC_THREAD(execute);
        sensitive << Port_CLK.pos();
        dont_initialize();

        // init all lines invalid
        for (int s = 0; s < NUM_SETS; s++) {
            for (int w = 0; w < WAYS; w++) {
                lines[s][w].valid = false;
                lines[s][w].dirty = false;
                lines[s][w].tag = 0;
                lines[s][w].last_used = 0;
            }
        }
    }

private:

    // Cache configuration
    static const int LINE_SIZE   = 32;          // bytes
    static const int CACHE_SIZE  = 32 * 1024;   // bytes
    static const int WAYS        = 8;
    static const int NUM_SETS    = CACHE_SIZE / (LINE_SIZE * WAYS); // 128

    static const int OFFSET_BITS = 5;  // log2(32)
    static const int INDEX_BITS  = 7;  // log2(128)


    // Cache line metadata
    struct Line {
        bool valid;
        bool dirty;
        uint64_t tag;
        uint64_t last_used; // timestamp for LRU
    };

    Line lines[NUM_SETS][WAYS];
    uint64_t time_counter;

    // Helpers
    uint32_t get_set(uint64_t addr) const {
        return (addr >> OFFSET_BITS) & ((1u << INDEX_BITS) - 1u);
    }

    uint64_t get_tag(uint64_t addr) const {
        return addr >> (OFFSET_BITS + INDEX_BITS);
    }

    // Base address of a cache line (for writeback logging/memory op)
    uint64_t line_base_addr(uint64_t tag, uint32_t set) const {
        return (tag << (OFFSET_BITS + INDEX_BITS)) | (uint64_t(set) << OFFSET_BITS);
    }

    int find_hit_way(uint32_t set, uint64_t tag) const {
        for (int w = 0; w < WAYS; w++) {
            if (lines[set][w].valid && lines[set][w].tag == tag) {
                return w;
            }
        }
        return -1;
    }

    int choose_victim(uint32_t set) const {
        // Prefer invalid line
        for (int w = 0; w < WAYS; w++) {
            if (!lines[set][w].valid) return w;
        }
        // Else LRU: smallest last_used
        int victim = 0;
        uint64_t min_used = lines[set][0].last_used;
        for (int w = 1; w < WAYS; w++) {
            if (lines[set][w].last_used < min_used) {
                min_used = lines[set][w].last_used;
                victim = w;
            }
        }
        return victim;
    }

    void mem_read_blocking(uint64_t addr) {
        // Request memory read; Memory module models 100 cycles and then toggles done.
        Port_MemAddr.write(addr);
        Port_MemFunc.write(Memory::FUNC_READ);
        wait(Port_MemDone.value_changed_event());
    }

    void mem_write_blocking(uint64_t addr, uint64_t data) {
        // Request memory write; Memory module models 100 cycles and then toggles done.
        Port_MemAddr.write(addr);
        Port_MemFunc.write(Memory::FUNC_WRITE);

        // Drive write data for one cycle
        Port_MemData.write(data);
        wait();
        Port_MemData.write(float_64_bit_wire);

        wait(Port_MemDone.value_changed_event());
    }

    void reply_to_cpu(Memory::Function f, uint64_t addr) {
        if (f == Memory::FUNC_READ) {
            // return deterministic dummy value
            uint64_t data = addr * 10;
            Port_CPUData.write(data);
            wait(); // allow CPU to sample
            Port_CPUData.write(float_64_bit_wire);

            Port_CPUDone.write(Memory::RET_READ_DONE);
        } else {
            Port_CPUDone.write(Memory::RET_WRITE_DONE);
            wait(); // hold done for one cycle so CPU can see value_changed_event
        }
    }


    // Main behavior
    void execute() {
        while (true) {
            // sc_buffer triggers value_changed_event on every write
            // safe for back-to-back reads/writes.
            wait(Port_CPUReq.posedge_event());


            Memory::Function f = Port_CPUFunc.read();
            uint64_t addr      = Port_CPUAddr.read();

            // Log CPU->Cache request
            if (f == Memory::FUNC_WRITE) {
                uint64_t data = Port_CPUData.read().to_uint64();
                log(name(), "write address =", addr, "data =", data);
            } else {
                log(name(), "read address =", addr);
            }

            uint32_t set = get_set(addr);
            uint64_t tag = get_tag(addr);

            int hit_way = find_hit_way(set, tag);


            // HIT
            if (hit_way != -1) {
                // Stats + log
                if (f == Memory::FUNC_READ) {
                    stats_readhit(0);
                    log(name(), "read hit address =", addr, "set =", set, "line =", hit_way);
                } else {
                    stats_writehit(0);
                    lines[set][hit_way].dirty = true; // write-back
                    log(name(), "write hit address =", addr, "set =", set, "line =", hit_way);
                }

                // Update LRU
                lines[set][hit_way].last_used = ++time_counter;

                // 1-cycle cache latency
                wait(1);

                // Reply to CPU (no memory access on hit)
                reply_to_cpu(f, addr);

                continue;
            }

            // MISS
            if (f == Memory::FUNC_READ) {
                stats_readmiss(0);
                log(name(), "read miss address =", addr);
            } else {
                stats_writemiss(0);
                log(name(), "write miss address =", addr);
            }

            int victim = choose_victim(set);

            // If victim valid and dirty -> write-back before replacing
            if (lines[set][victim].valid && lines[set][victim].dirty) {
                uint64_t evict_tag  = lines[set][victim].tag;
                uint64_t evict_addr = line_base_addr(evict_tag, set);

                log(name(), "evict dirty line",
                    "evict_addr =", evict_addr,
                    "set =", set, "line =", victim,
                    "tag =", evict_tag);

                // Write-back takes 100 cycles via Memory module
                mem_write_blocking(evict_addr, evict_addr * 10);

                lines[set][victim].dirty = false;
            } else if (lines[set][victim].valid) {
                // Clean eviction
                uint64_t evict_tag  = lines[set][victim].tag;
                uint64_t evict_addr = line_base_addr(evict_tag, set);

                log(name(), "evict clean line",
                    "evict_addr =", evict_addr,
                    "set =", set, "line =", victim,
                    "tag =", evict_tag);
            }

            // Allocate-on-write policy:
            mem_read_blocking(addr);

            // Install new line
            lines[set][victim].valid = true;
            lines[set][victim].tag = tag;
            lines[set][victim].dirty = (f == Memory::FUNC_WRITE); // write-back: dirty if write
            lines[set][victim].last_used = ++time_counter;

            // Log completion
            if (f == Memory::FUNC_READ) {
                log(name(), "read completed address =", addr, "set =", set, "line =", victim);
            } else {
                log(name(), "write completed address =", addr, "set =", set, "line =", victim);
            }

            // 1-cycle cache latency
            wait(1);

            // Reply to CPU
            reply_to_cpu(f, addr);
        }
    }
};



SC_MODULE(CPU) {
    public:
    sc_in<bool> Port_CLK;
    sc_in<Memory::RetCode> Port_MemDone;
    sc_out<Memory::Function> Port_MemFunc;
    sc_out<uint64_t> Port_MemAddr;
    sc_inout_rv<64> Port_MemData;

    //added for blocking cache
    sc_out<bool> Port_CPUReq;


    SC_CTOR(CPU) {
        SC_THREAD(execute);
        sensitive << Port_CLK.pos();
        dont_initialize();
    }

    private:
    void execute() {
        TraceFile::Entry tr_data;
        Memory::Function f;

        //init cpu req = false
        Port_CPUReq.write(false);
        wait();

        // Loop until end of tracefile
        while (!tracefile_ptr->eof()) {
            // Get the next action for the processor in the trace
            if (!tracefile_ptr->next(0, tr_data)) {
                cerr << "Error reading trace for CPU" << endl;
                break;
            }

            // To demonstrate the statistic functions, we generate a 50%
            // probability of a 'hit' or 'miss', and call the statistic
            // functions below

            //int j = rand() % 2; --> random hit is removed

            switch (tr_data.type) {
            case TraceFile::ENTRY_TYPE_READ:
                f = Memory::FUNC_READ;
                //random hit is removed
                //if (j)
                //    stats_readhit(0);
                //else
                //    stats_readmiss(0);
                break;

            case TraceFile::ENTRY_TYPE_WRITE:
                f = Memory::FUNC_WRITE;
                //random hit is removed
                //if (j)
                //    stats_writehit(0);
                //else
                //    stats_writemiss(0);
                break;

            case TraceFile::ENTRY_TYPE_NOP: break;

            default:
                cerr << "Error, got invalid data from Trace" << endl;
                exit(0);
            }

            if (tr_data.type != TraceFile::ENTRY_TYPE_NOP) {
                Port_MemAddr.write(tr_data.addr);
                Port_MemFunc.write(f);


                if (f == Memory::FUNC_WRITE) {
                    // No data in trace, use address * 10 as data value.
                    uint64_t data = tr_data.addr * 10;
                    log(name(), "write value", data,
                            "to address", tr_data.addr);
                    Port_MemData.write(data);
                    wait();

                    //explicit signal of request
                    Port_CPUReq.write(true);
                    wait();                 // 1 cycle pulse
                    Port_CPUReq.write(false);


                    // Now float the data wires with 64 "Z"'s
                    Port_MemData.write(float_64_bit_wire);

                } else {
                    log(name(), "read on address", tr_data.addr);
                    Port_CPUReq.write(true);
                    wait();                 // 1 cycle pulse
                    Port_CPUReq.write(false);
                }

                wait(Port_MemDone.value_changed_event());

               /* if (f == Memory::FUNC_READ) {
                    log(name(), "read data", Port_MemData.read().to_uint64(),
                            "from address", tr_data.addr);
                }*/

                if (f == Memory::FUNC_READ) {
                    log(name(), "read done address =", tr_data.addr);
                } else {
                    log(name(), "write done address =", tr_data.addr);
                }


            } else {
                //log(name(), "executing NOP");
            }
            // Advance one cycle in simulated time
            wait();
        }

        // Finished the Tracefile, now stop the simulation
        sc_stop();

    }
};


int sc_main(int argc, char *argv[]) {
    sc_report_handler::set_verbosity_level(SC_MEDIUM);
    // Uncomment the next line to silence the log() messages.
    // sc_report_handler::set_verbosity_level(SC_LOW);



    try {
        // Get the tracefile argument and create Tracefile object
        // This function sets tracefile_ptr and num_cpus
        init_tracefile(&argc, &argv);

        // Initialize statistics counters
        stats_init();

        // Instantiate Modules
        Memory mem("memory");
        Cache cache("cache"); //init cache
        CPU cpu("cpu");



        // CPU <-> Cache signals
        sc_buffer<Memory::Function> sigCPUFunc;
        sc_buffer<Memory::RetCode>  sigCPUDone;
        sc_signal<uint64_t>         sigCPUAddr;
        sc_signal_rv<64>            sigCPUData;

        // Cache <-> Memory signals
        sc_buffer<Memory::Function> sigMemFunc;
        sc_buffer<Memory::RetCode>  sigMemDone;
        sc_signal<uint64_t>         sigMemAddr;
        sc_signal_rv<64>            sigMemData;

        // for cache cpu wiring
        sc_signal<bool> sigCPUReq;

        // The clock that will drive the CPU and Memory
        sc_clock clk;

        // Connecting module ports with signals
        //rewiring with cache
        //Memory ports (connected to Cache <-> Memory signals)
        mem.Port_Func(sigMemFunc);
        mem.Port_Addr(sigMemAddr);
        mem.Port_Data(sigMemData);
        mem.Port_Done(sigMemDone);
        mem.Port_CLK(clk);

        //Cache CPU-side ports (connected to CPU <-> Cache signals)
        cache.Port_CPUFunc(sigCPUFunc);
        cache.Port_CPUAddr(sigCPUAddr);
        cache.Port_CPUData(sigCPUData);
        cache.Port_CPUDone(sigCPUDone);
        cache.Port_CPUReq(sigCPUReq);


        //Cache Memory-side ports (connected to Cache <-> Memory signals)
        cache.Port_MemFunc(sigMemFunc);
        cache.Port_MemAddr(sigMemAddr);
        cache.Port_MemData(sigMemData);
        cache.Port_MemDone(sigMemDone);
        cache.Port_CLK(clk);

        //CPU ports
        cpu.Port_MemFunc(sigCPUFunc);
        cpu.Port_MemAddr(sigCPUAddr);
        cpu.Port_MemData(sigCPUData);
        cpu.Port_MemDone(sigCPUDone);
        cpu.Port_CLK(clk);
        cpu.Port_CPUReq(sigCPUReq);


        cout << "Running (press CTRL+C to interrupt)... " << endl;


        // Start Simulation
        sc_start();

        // Print statistics after simulation finished
        stats_print();
        // mem.dump(); // Uncomment to dump memory to stdout.
    }

    catch (exception &e) {
        cerr << e.what() << endl;
    }

    return 0;
}

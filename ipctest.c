// ipctest: sanity-check rusage instruction/cycle counters with kernels of
// known IPC. Each kernel is a loop whose body is UNROLL copies of one
// instruction (via .rept), plus 2 loop-control instructions.
//
//   dependent eor/add chain  -> 1-cycle latency       -> IPC ~1
//   dependent mul chain      -> 3-cycle latency       -> IPC ~0.33
//   dependent fadd chain     -> ~3-cycle latency      -> IPC ~0.33
//   L1 pointer chase (ldr)   -> ~3-4 cycle load-use   -> IPC ~0.25-0.33
//     (a random cyclic permutation: a self-pointer or a strided chain gets
//      broken by the core's load value/address prediction)
//   8 independent eor chains -> ALU-throughput bound  -> IPC ~#int ALUs
//
// The measured instruction count is also compared to the exact expected
// count, which validates the instructions counter independently of timing.
#include <libproc.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/qos.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#define UNROLL 100
#define STR_(x) #x
#define STR(x) STR_(x)

typedef struct { uint64_t instr, cycles, pcycles, user_ticks; } snap_t;

static snap_t snap(void) {
    struct rusage_info_v6 ru;
    proc_pid_rusage(getpid(), RUSAGE_INFO_V6, (rusage_info_t *)&ru);
    return (snap_t){ru.ri_instructions, ru.ri_cycles, ru.ri_pcycles, ru.ri_user_time};
}

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// Loop skeleton: body, then "subs; b.ne" (2 instructions per iteration).
#define LOOP(setup, body, clobbers...)                         \
    __asm__ volatile(setup                                     \
                     "1:\n\t"                                  \
                     ".rept " STR(UNROLL) "\n\t" body "\n\t.endr\n\t" \
                     "subs %[n], %[n], #1\n\t"                 \
                     "b.ne 1b\n\t"                             \
                     : [n] "+r"(n) : [p] "r"(p) : "cc", "memory", clobbers)

static void k_eor_dep(long n, void *p)  { LOOP("mov x9, #1\n\t", "eor x10, x10, x9", "x9", "x10"); }
static void k_add_dep(long n, void *p)  { LOOP("mov x9, #1\n\t", "add x10, x10, x9", "x9", "x10"); }
static void k_mul_dep(long n, void *p)  { LOOP("mov x9, #3\n\tmov x10, #1\n\t", "mul x10, x10, x9", "x9", "x10"); }
static void k_fadd_dep(long n, void *p) { LOOP("fmov d1, #1.0\n\t", "fadd d0, d0, d1", "d0", "d1"); }
static void k_ldr_chase(long n, void *p){ LOOP("mov x10, %[p]\n\t", "ldr x10, [x10]", "x10"); }
static void k_add_8way(long n, void *p) {
    LOOP("mov x9, #1\n\t",
         "add x10, x10, x9\n\tadd x11, x11, x9\n\tadd x12, x12, x9\n\tadd x13, x13, x9\n\t"
         "add x14, x14, x9\n\tadd x15, x15, x9\n\tadd x16, x16, x9\n\tadd x17, x17, x9",
         "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17");
}
static void k_eor_8way(long n, void *p) {
    LOOP("mov x9, #1\n\t",
         "eor x10, x10, x9\n\teor x11, x11, x9\n\teor x12, x12, x9\n\teor x13, x13, x9\n\t"
         "eor x14, x14, x9\n\teor x15, x15, x9\n\teor x16, x16, x9\n\teor x17, x17, x9",
         "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17");
}

typedef struct {
    const char *name;
    void (*fn)(long, void *);
    int per_rept;          // instructions per .rept copy
    double lo, hi;         // acceptable IPC range
} test_t;

static const test_t tests[] = {
    {"eor dependent chain",     k_eor_dep,   1, 0.95, 1.05},
    {"add dependent chain",     k_add_dep,   1, 0.95, 1.05},
    {"mul dependent chain",     k_mul_dep,   1, 0.30, 0.36},
    {"fadd dependent chain",    k_fadd_dep,  1, 0.24, 0.52},
    {"ldr L1 random chase",     k_ldr_chase, 1, 0.20, 0.36},
    {"eor 8 indep. chains",     k_eor_8way,  8, 3.00, 10.0},
    {"add 8 indep. chains",     k_add_8way,  8, 3.00, 10.0},
};

int main(int argc, char **argv) {
    long iters = argc > 1 ? atol(argv[1]) : 20000000;   // x UNROLL instructions
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    // ldr chase: random cycle of pointers, one per 64B line, 32 KB (fits L1D).
    enum { LINES = 512, STRIDE = 8 };
    void **buf = aligned_alloc(16384, LINES * STRIDE * sizeof(void *));
    int perm[LINES];
    for (int i = 0; i < LINES; i++) perm[i] = i;
    srand(1);
    for (int i = LINES - 1; i > 0; i--) { int j = rand() % (i + 1), t = perm[i]; perm[i] = perm[j]; perm[j] = t; }
    for (int i = 0; i < LINES; i++)
        buf[perm[i] * STRIDE] = &buf[perm[(i + 1) % LINES] * STRIDE];
    void *self = buf;
    int fails = 0;

    printf("%-24s %8s %8s %6s %6s %7s %8s  %s\n",
           "kernel", "instr(M)", "err ppm", "IPC", "GHz", "P-cyc%", "expect", "");
    for (size_t t = 0; t < sizeof(tests) / sizeof(tests[0]); t++) {
        const test_t *T = &tests[t];
        T->fn(iters / 20, self);                         // warm up / ramp clocks
        snap_t a = snap();
        double w0 = now();
        T->fn(iters, self);
        double w1 = now();
        snap_t b = snap();

        double expect_instr = (double)iters * (UNROLL * T->per_rept + 2);
        double di = b.instr - a.instr, dc = b.cycles - a.cycles;
        // Kernel-body IPC: exclude the 2 loop instructions per iteration.
        double ipc = di / dc;
        double body_ipc = ipc * (UNROLL * T->per_rept) / (UNROLL * T->per_rept + 2);
        double ghz = dc / (w1 - w0) / 1e9;
        double ppm = (di - expect_instr) / expect_instr * 1e6;
        int ok = body_ipc >= T->lo && body_ipc <= T->hi && ppm < 5000 && ppm > -100;
        fails += !ok;
        char range[32];
        snprintf(range, sizeof range, "%.2f-%.2f", T->lo, T->hi);
        printf("%-24s %8.0f %8.0f %6.3f %6.2f %7.1f %8s  %s\n",
               T->name, di / 1e6, ppm, body_ipc, ghz,
               100.0 * (b.pcycles - a.pcycles) / dc, range, ok ? "ok" : "FAIL");
    }
    printf("%s\n", fails ? "SOME CHECKS FAILED" : "all checks passed");
    return fails != 0;
}

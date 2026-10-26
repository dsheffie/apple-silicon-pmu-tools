// hbcbench: compare b.cond vs bc.cond (FEAT_HBC "branch consistent" hint).
// Runs each generated kernel pair, interleaving b/bc over several repetitions,
// and reports the median cycles per iteration (and per branch for the
// footprint kernels) from the process's fixed cycle/instruction counters.
// Build: see Makefile (assembles hbcbench.S with +hbc).
#include <libproc.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/qos.h>
#include <sys/resource.h>
#include <unistd.h>

typedef void (*kern_t)(long, const uint8_t *);
#define K(name) void k_b_##name(long, const uint8_t *); void k_bc_##name(long, const uint8_t *);
K(single) K(fp1024) K(fp4096) K(fp16384) K(fp65536)
K(tk1024) K(tk4096) K(tk16384) K(tk65536) K(alt) K(p8) K(data)

typedef struct {
    const char *name;
    kern_t b, bc;
    long iters;
    int branches;      // static test branches executed per iteration
    int data;          // 0: none, 1: random 50/50, 2: 95% zero / 5% one
} test_t;

#define T(n, it, br, d) {#n, k_b_##n, k_bc_##n, it, br, d}
static const test_t tests[] = {
    T(single, 400000000, 1, 0),
    T(fp1024, 400000, 1024, 0),
    T(fp4096, 100000, 4096, 0),
    T(fp16384, 25000, 16384, 0),
    T(fp65536, 6000, 65536, 0),
    T(tk1024, 400000, 1024, 0),
    T(tk4096, 100000, 4096, 0),
    T(tk16384, 25000, 16384, 0),
    T(tk65536, 6000, 65536, 0),
    T(alt, 400000000, 1, 0),
    T(p8, 400000000, 1, 0),
    {"data-random50", k_b_data, k_bc_data, 200000000, 1, 1},
    {"data-biased95", k_b_data, k_bc_data, 200000000, 1, 2},
};

static void counters(uint64_t *instr, uint64_t *cycles) {
    struct rusage_info_v6 ru;
    proc_pid_rusage(getpid(), RUSAGE_INFO_V6, (rusage_info_t *)&ru);
    *instr = ru.ri_instructions;
    *cycles = ru.ri_cycles;
}

static double measure(kern_t k, long iters, const uint8_t *bits, double *ipc) {
    uint64_t i0, c0, i1, c1;
    counters(&i0, &c0);
    k(iters, bits);
    counters(&i1, &c1);
    *ipc = (double)(i1 - i0) / (c1 - c0);
    return (double)(c1 - c0) / iters;
}

static int cmp(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

int main(int argc, char **argv) {
    int reps = argc > 1 ? atoi(argv[1]) : 5;
    // HBC_DEFAULT_QOS=1 keeps default QoS so hog spinners can steer us to P/E cores.
    if (!getenv("HBC_DEFAULT_QOS")) pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    uint8_t *bits = aligned_alloc(16384, 65536);

    printf("%-15s %12s %12s %8s %8s %8s %13s\n", "kernel", "b cyc/iter", "bc cyc/iter", "bc/b",
           "b IPC", "bc IPC", "cyc/branch b|bc");
    for (size_t t = 0; t < sizeof(tests) / sizeof(tests[0]); t++) {
        const test_t *T = &tests[t];
        if (argc > 2 && !strstr(argv[2], T->name)) continue;  // optional comma list filter
        srand(12345);
        for (int i = 0; i < 65536; i++)
            bits[i] = T->data == 1 ? rand() & 1 : T->data == 2 ? (rand() % 100) < 5 : 0;
        double sb[64], sbc[64], ipcb = 0, ipcbc = 0, x;
        T->b(T->iters / 10, bits);   // warm up caches, predictor, clocks
        T->bc(T->iters / 10, bits);
        for (int r = 0; r < reps; r++) {     // interleave to cancel drift
            sb[r] = measure(T->b, T->iters, bits, &x);  ipcb += x;
            sbc[r] = measure(T->bc, T->iters, bits, &x); ipcbc += x;
        }
        qsort(sb, reps, sizeof(double), cmp);
        qsort(sbc, reps, sizeof(double), cmp);
        double mb = sb[reps / 2], mbc = sbc[reps / 2];
        printf("%-15s %12.3f %12.3f %8.3f %8.2f %8.2f %6.3f|%.3f\n", T->name, mb, mbc, mbc / mb,
               ipcb / reps, ipcbc / reps, mb / T->branches, mbc / T->branches);
    }
    struct rusage_info_v6 ru;
    proc_pid_rusage(getpid(), RUSAGE_INFO_V6, (rusage_info_t *)&ru);
    printf("placement: super-core cycles %.1f%%, clock %.2f GHz\n",
           100.0 * ru.ri_pcycles / ru.ri_cycles, ru.ri_cycles / (ru.ri_user_time / 24e6) / 1e9);
    return 0;
}

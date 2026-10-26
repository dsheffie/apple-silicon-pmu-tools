// coldbench: b.cond vs bc.cond (FEAT_HBC) on large-footprint and cold code.
// Regions come from gen_cold.py. Two modes per kernel:
//   warm: the region is called repeatedly; steady state with a footprint
//         beyond the L1I / BTB at the larger sizes.
//   cold: before every timed call, an evictor region (plain b, 2 MiB) and a
//         64 MiB data sweep flush the L1I, BTB, direction predictor, L2 and
//         TLBs, so each timed call executes the region cold.
// b and bc measurements are interleaved; cold mode also reports the median of
// the per-repetition (consecutive) bc/b ratios. Overhead of an empty call is
// measured and subtracted.
// Usage: coldbench [warm_reps] [cold_reps] [filter]
#include <libproc.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/qos.h>
#include <sys/resource.h>
#include <unistd.h>

typedef void (*fn_t)(void);
void hbc_enter(fn_t);
void hbc_empty(void);
void kc_evict_262144(void);

#define D(r, n) void kc_b_##r##_##n(void); void kc_bc_##r##_##n(void);
#define DENSE(r) D(r, 4096) D(r, 16384) D(r, 65536) D(r, 262144) D(r, 524288)
DENSE(mix) DENSE(tk) DENSE(nt)
D(sparse, 4096) D(sparse, 16384) D(sparse, 65536) D(sparse, 131072)

typedef struct {
    const char *name;
    fn_t b, bc;
    long n;        // static branches executed per call
    int bytes;     // code bytes per branch
} test_t;

#define T(r, n, by) {#r "-" #n, kc_b_##r##_##n, kc_bc_##r##_##n, n, by}
#define TD(r) T(r, 4096, 8), T(r, 16384, 8), T(r, 65536, 8), T(r, 262144, 8), T(r, 524288, 8)
static const test_t tests[] = {
    TD(mix), TD(tk), TD(nt),
    T(sparse, 4096, 24), T(sparse, 16384, 24), T(sparse, 65536, 24), T(sparse, 131072, 24),
};

static void counters(uint64_t *instr, uint64_t *cycles) {
    struct rusage_info_v6 ru;
    proc_pid_rusage(getpid(), RUSAGE_INFO_V6, (rusage_info_t *)&ru);
    *instr = ru.ri_instructions;
    *cycles = ru.ri_cycles;
}

static double ovh_cyc, ovh_ins;

// Cycles (and instructions, via *ins) for `calls` calls of f, minus overhead.
static double measure(fn_t f, long calls, double *ins) {
    uint64_t i0, c0, i1, c1;
    counters(&i0, &c0);
    for (long k = 0; k < calls; k++) hbc_enter(f);
    counters(&i1, &c1);
    *ins = (double)(i1 - i0) - ovh_ins;
    return (double)(c1 - c0) - ovh_cyc;
}

static int cmp(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
static double median(double *v, int n) {
    qsort(v, n, sizeof(double), cmp);
    return n & 1 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

#define EVBYTES (64L << 20)
static volatile uint64_t sink;
static uint64_t *evbuf;
static void evict(void) {
    hbc_enter(kc_evict_262144);
    hbc_enter(kc_evict_262144);
    uint64_t s = 0;
    for (long i = 0; i < EVBYTES / 8; i += 8) s += evbuf[i];
    sink = s;
}

int main(int argc, char **argv) {
    int wreps = argc > 1 ? atoi(argv[1]) : 5, creps = argc > 2 ? atoi(argv[2]) : 15;
    const char *filter = argc > 3 ? argv[3] : NULL;
    if (wreps > 64) wreps = 64;
    if (creps > 256) creps = 256;
    if (!getenv("HBC_DEFAULT_QOS")) pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    evbuf = malloc(EVBYTES);
    for (long i = 0; i < EVBYTES / 8; i++) evbuf[i] = i;

    double v[256], w[256], x;
    for (int r = 0; r < 64; r++) { ovh_cyc = ovh_ins = 0; v[r] = measure(hbc_empty, 1, &w[r]); }
    ovh_cyc = median(v, 64);
    ovh_ins = median(w, 64);

    printf("%-14s %6s | %8s %8s %7s | %8s %8s %7s %7s | %5s\n", "kernel", "KiB", "warm b", "warm bc",
           "bc/b", "cold b", "cold bc", "bc/b", "pair", "ins/br");
    for (size_t t = 0; t < sizeof(tests) / sizeof(tests[0]); t++) {
        const test_t *T = &tests[t];
        if (filter && !strstr(filter, T->name)) continue;
        // Warm: about 16M branches per measurement.
        long calls = (1L << 24) / T->n;
        double wb[64], wbc[64], ins = 0;
        hbc_enter(T->b); hbc_enter(T->bc);
        for (int r = 0; r < wreps; r++) {
            wb[r] = measure(T->b, calls, &x) / (calls * T->n);
            wbc[r] = measure(T->bc, calls, &x) / (calls * T->n);
            ins += x / (calls * T->n);
        }
        double mwb = median(wb, wreps), mwbc = median(wbc, wreps);
        // Cold: evict, then one call; alternate which variant goes first.
        double cb[256], cbc[256], pr[256];
        for (int r = 0; r < creps; r++) {
            if (r & 1) {
                evict(); cbc[r] = measure(T->bc, 1, &x) / T->n;
                evict(); cb[r] = measure(T->b, 1, &x) / T->n;
            } else {
                evict(); cb[r] = measure(T->b, 1, &x) / T->n;
                evict(); cbc[r] = measure(T->bc, 1, &x) / T->n;
            }
            pr[r] = cbc[r] / cb[r];
        }
        double mcb = median(cb, creps), mcbc = median(cbc, creps), mpr = median(pr, creps);
        printf("%-14s %6ld | %8.3f %8.3f %7.3f | %8.3f %8.3f %7.3f %7.3f | %5.2f\n", T->name,
               T->n * T->bytes / 1024, mwb, mwbc, mwbc / mwb, mcb, mcbc, mcbc / mcb, mpr,
               ins / wreps);
        fflush(stdout);
    }
    struct rusage_info_v6 ru;
    proc_pid_rusage(getpid(), RUSAGE_INFO_V6, (rusage_info_t *)&ru);
    printf("placement: super-core cycles %.1f%%, clock %.2f GHz\n",
           100.0 * ru.ri_pcycles / ru.ri_cycles, ru.ri_cycles / (ru.ri_user_time / 24e6) / 1e9);
    return 0;
}

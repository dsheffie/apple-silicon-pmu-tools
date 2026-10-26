// tabbench: run the b.cond / bc.cond kernel pairs listed in gen_tab.py's table.
// Interleaves b/bc for `reps` repetitions and reports median cycles per
// iteration, the ratio of medians, the median of per-repetition (consecutive)
// ratios, and cycles per static test branch.
// Usage: tabbench [reps] [comma-separated kernel names]
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
typedef struct {
    const char *name;
    kern_t b, bc;
    long iters, branches;
} entry_t;
extern const long tab_count;
extern const entry_t tab[];

static uint64_t cycles(void) {
    struct rusage_info_v6 ru;
    proc_pid_rusage(getpid(), RUSAGE_INFO_V6, (rusage_info_t *)&ru);
    return ru.ri_cycles;
}

static double measure(kern_t k, long iters, const uint8_t *bits) {
    uint64_t c0 = cycles();
    k(iters, bits);
    return (double)(cycles() - c0) / iters;
}

static int cmp(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
static double median(double *v, int n) {
    qsort(v, n, sizeof(double), cmp);
    return n & 1 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

static int selected(const char *list, const char *name) {
    if (!list) return 1;
    size_t n = strlen(name);
    for (const char *p = list; (p = strstr(p, name)); p += n)
        if ((p == list || p[-1] == ',') && (p[n] == ',' || p[n] == 0)) return 1;
    return 0;
}

int main(int argc, char **argv) {
    int reps = argc > 1 ? atoi(argv[1]) : 9;
    const char *list = argc > 2 ? argv[2] : NULL;
    if (reps > 256) reps = 256;
    if (!getenv("HBC_DEFAULT_QOS")) pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    uint8_t *bits = aligned_alloc(16384, 1 << 20);
    srand(2026);
    for (int i = 0; i < 1 << 20; i++) bits[i] = rand() & 1;

    printf("%-12s %12s %12s %8s %8s %13s\n", "kernel", "b cyc/iter", "bc cyc/iter", "bc/b",
           "pair", "cyc/branch b|bc");
    for (long t = 0; t < tab_count; t++) {
        const entry_t *e = &tab[t];
        if (!selected(list, e->name)) continue;
        double sb[256], sbc[256], pr[256];
        e->b(e->iters / 10 + 1, bits);   // warm up caches, predictor, clocks
        e->bc(e->iters / 10 + 1, bits);
        for (int r = 0; r < reps; r++) {
            if (r & 1) {
                sbc[r] = measure(e->bc, e->iters, bits);
                sb[r] = measure(e->b, e->iters, bits);
            } else {
                sb[r] = measure(e->b, e->iters, bits);
                sbc[r] = measure(e->bc, e->iters, bits);
            }
            pr[r] = sbc[r] / sb[r];
        }
        double mb = median(sb, reps), mbc = median(sbc, reps), mp = median(pr, reps);
        printf("%-12s %12.3f %12.3f %8.3f %8.3f %6.3f|%.3f\n", e->name, mb, mbc, mbc / mb, mp,
               mb / e->branches, mbc / e->branches);
        fflush(stdout);
    }
    struct rusage_info_v6 ru;
    proc_pid_rusage(getpid(), RUSAGE_INFO_V6, (rusage_info_t *)&ru);
    printf("placement: super-core cycles %.1f%%, clock %.2f GHz\n",
           100.0 * ru.ri_pcycles / ru.ri_cycles, ru.ri_cycles / (ru.ri_user_time / 24e6) / 1e9);
    return 0;
}

// layoutbench: random50 b.cond vs bc.cond over many code placements.
// Each round measures every b and bc copy (gen_layout.py) once, in a shuffled
// order, on the same 64 KiB random 0/1 buffer as hbcbench's data-random50.
// Reports per-copy medians and compares the b and bc distributions.
// Usage: [HBC_BIASED=1] layoutbench [rounds] [iters]
#include <libproc.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/qos.h>
#include <sys/resource.h>
#include <unistd.h>

typedef void (*kern_t)(long, const uint8_t *);
extern const long lay_count;
extern const kern_t lay[][2];

static uint64_t cycles(void) {
    struct rusage_info_v6 ru;
    proc_pid_rusage(getpid(), RUSAGE_INFO_V6, (rusage_info_t *)&ru);
    return ru.ri_cycles;
}

static int cmp(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
static double median(double *v, int n) {
    qsort(v, n, sizeof(double), cmp);
    return n & 1 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

#define MAXC 64
#define MAXR 128

int main(int argc, char **argv) {
    int rounds = argc > 1 ? atoi(argv[1]) : 15;
    long iters = argc > 2 ? atol(argv[2]) : 20000000;
    if (rounds > MAXR) rounds = MAXR;
    if (!getenv("HBC_DEFAULT_QOS")) pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    uint8_t *bits = aligned_alloc(16384, 65536);
    // Same buffer contents as hbcbench's data-random50, or data-biased95 with HBC_BIASED=1.
    int biased = getenv("HBC_BIASED") != NULL;
    srand(12345);
    for (int i = 0; i < 65536; i++) bits[i] = biased ? (rand() % 100) < 5 : rand() & 1;

    int n = (int)lay_count * 2, idx[2 * MAXC];
    static double s[2 * MAXC][MAXR];
    for (int i = 0; i < n; i++) { idx[i] = i; lay[i / 2][i % 2](iters / 10, bits); }
    srandom(1);
    for (int r = 0; r < rounds; r++) {
        for (int i = n - 1; i > 0; i--) { int j = random() % (i + 1), t = idx[i]; idx[i] = idx[j]; idx[j] = t; }
        for (int i = 0; i < n; i++) {
            int k = idx[i];
            uint64_t c0 = cycles();
            lay[k / 2][k % 2](iters, bits);
            s[k][r] = (double)(cycles() - c0) / iters;
        }
    }
    double mb[MAXC], mbc[MAXC], d[MAXC], sb = 0, sbc = 0;
    printf("copy   b cyc/iter  bc cyc/iter   bc/b\n");
    for (int c = 0; c < lay_count; c++) {
        mb[c] = median(s[2 * c], rounds);
        mbc[c] = median(s[2 * c + 1], rounds);
        d[c] = mbc[c] / mb[c];
        sb += mb[c]; sbc += mbc[c];
        printf("%4d %12.3f %12.3f %6.3f\n", c, mb[c], mbc[c], d[c]);
    }
    int m = (int)lay_count;
    double lm = 0, lv = 0;
    for (int c = 0; c < m; c++) lm += log(d[c]);
    lm /= m;
    for (int c = 0; c < m; c++) lv += (log(d[c]) - lm) * (log(d[c]) - lm);
    lv /= m - 1;
    printf("mean b %.3f  mean bc %.3f  geomean bc/b %.3f  t=%.2f (n=%d copies)\n", sb / m, sbc / m,
           exp(lm), lm / sqrt(lv / m), m);
    printf("median b %.3f  median bc %.3f\n", median(mb, m), median(mbc, m));
    printf("logratio mean %.6f sd %.6f n %d\n", lm, sqrt(lv), m);
    struct rusage_info_v6 ru;
    proc_pid_rusage(getpid(), RUSAGE_INFO_V6, (rusage_info_t *)&ru);
    printf("placement: super-core cycles %.1f%%, clock %.2f GHz\n",
           100.0 * ru.ri_pcycles / ru.ri_cycles, ru.ri_cycles / (ru.ri_user_time / 24e6) / 1e9);
    return 0;
}

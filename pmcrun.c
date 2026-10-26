// pmcrun: Apple-silicon PMU counting wrapper for SPEC CPU runs.
//
//   sudo pmcrun setup EV1,EV2,...   program configurable counters system-wide
//   pmcrun run -o LOG -- cmd ...    run cmd, append per-CPU counter deltas +
//                                   exact child rusage (instr/cycles) to LOG
//   sudo pmcrun teardown            release the PMU
//   pmcrun list                     list events in this CPU's kpep database
//
// Uses the private kperf/kperfdata frameworks (API as documented by
// ibireme's kpc_demo.c). Counting is system-wide per CPU; the child's exact
// instruction count from proc_pid_rusage lets the analysis quantify how much
// of the per-CPU total belongs to the benchmark.
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <libproc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef uint64_t kpc_config_t;
typedef struct kpep_db kpep_db;
typedef struct kpep_config kpep_config;
typedef struct kpep_event kpep_event;

#define KPC_CLASS_FIXED_MASK        (1u << 0)
#define KPC_CLASS_CONFIGURABLE_MASK (1u << 1)
#define KPC_MAX_COUNTERS 32
#define STATE_FILE "/tmp/pmcrun.state"

static int (*kpc_set_counting)(uint32_t);
static int (*kpc_set_thread_counting)(uint32_t);
static int (*kpc_set_config)(uint32_t, kpc_config_t *);
static uint32_t (*kpc_get_counter_count)(uint32_t);
static int (*kpc_get_cpu_counters)(bool, uint32_t, int *, uint64_t *);
static int (*kpc_force_all_ctrs_set)(int);
static int (*kpc_force_all_ctrs_get)(int *);

static int (*kpep_config_create)(kpep_db *, kpep_config **);
static int (*kpep_config_add_event)(kpep_config *, kpep_event **, uint32_t, uint32_t *);
static int (*kpep_config_force_counters)(kpep_config *);
static int (*kpep_config_kpc)(kpep_config *, kpc_config_t *, size_t);
static int (*kpep_config_kpc_count)(kpep_config *, size_t *);
static int (*kpep_config_kpc_classes)(kpep_config *, uint32_t *);
static int (*kpep_config_kpc_map)(kpep_config *, size_t *, size_t);
static int (*kpep_db_create)(const char *, kpep_db **);
static int (*kpep_db_name)(kpep_db *, const char **);
static int (*kpep_db_events_count)(kpep_db *, size_t *);
static int (*kpep_db_events)(kpep_db *, kpep_event **, size_t);
static int (*kpep_db_event)(kpep_db *, const char *, kpep_event **);
static int (*kpep_event_name)(kpep_event *, const char **);
static int (*kpep_event_description)(kpep_event *, const char **);

#define LOAD(h, sym)                                                      \
    do {                                                                  \
        *(void **)&sym = dlsym(h, #sym);                                  \
        if (!sym) { fprintf(stderr, "pmcrun: missing %s\n", #sym); exit(1); } \
    } while (0)

static void load_frameworks(void) {
    void *kperf = dlopen("/System/Library/PrivateFrameworks/kperf.framework/kperf", RTLD_LAZY);
    void *kpdat = dlopen("/System/Library/PrivateFrameworks/kperfdata.framework/kperfdata", RTLD_LAZY);
    if (!kperf || !kpdat) { fprintf(stderr, "pmcrun: dlopen: %s\n", dlerror()); exit(1); }
    LOAD(kperf, kpc_set_counting);
    LOAD(kperf, kpc_set_thread_counting);
    LOAD(kperf, kpc_set_config);
    LOAD(kperf, kpc_get_counter_count);
    LOAD(kperf, kpc_get_cpu_counters);
    LOAD(kperf, kpc_force_all_ctrs_set);
    LOAD(kperf, kpc_force_all_ctrs_get);
    LOAD(kpdat, kpep_config_create);
    LOAD(kpdat, kpep_config_add_event);
    LOAD(kpdat, kpep_config_force_counters);
    LOAD(kpdat, kpep_config_kpc);
    LOAD(kpdat, kpep_config_kpc_count);
    LOAD(kpdat, kpep_config_kpc_classes);
    LOAD(kpdat, kpep_config_kpc_map);
    LOAD(kpdat, kpep_db_create);
    LOAD(kpdat, kpep_db_name);
    LOAD(kpdat, kpep_db_events_count);
    LOAD(kpdat, kpep_db_events);
    LOAD(kpdat, kpep_db_event);
    LOAD(kpdat, kpep_event_name);
    LOAD(kpdat, kpep_event_description);
}

// State shared between setup and run: event names and their counter indices.
typedef struct {
    uint32_t classes;
    int nev;
    char names[KPC_MAX_COUNTERS][64];
    size_t map[KPC_MAX_COUNTERS];
} state_t;

static int cmd_list(void) {
    kpep_db *db;
    if (kpep_db_create(NULL, &db)) { fprintf(stderr, "kpep_db_create failed\n"); return 1; }
    const char *dbname; kpep_db_name(db, &dbname);
    size_t n; kpep_db_events_count(db, &n);
    kpep_event **evs = calloc(n, sizeof(*evs));
    kpep_db_events(db, evs, n * sizeof(*evs));
    printf("# kpep db %s, %zu events\n", dbname, n);
    for (size_t i = 0; i < n; i++) {
        const char *nm = "", *desc = "";
        kpep_event_name(evs[i], &nm);
        kpep_event_description(evs[i], &desc);
        printf("%-44s %s\n", nm, desc ? desc : "");
    }
    return 0;
}

static int cmd_setup(char *evlist) {
    kpep_db *db;
    kpep_config *cfg;
    if (kpep_db_create(NULL, &db)) { fprintf(stderr, "kpep_db_create failed\n"); return 1; }
    if (kpep_config_create(db, &cfg)) { fprintf(stderr, "kpep_config_create failed\n"); return 1; }
    if (kpep_config_force_counters(cfg)) { fprintf(stderr, "kpep_config_force_counters failed\n"); return 1; }

    state_t st = {0};
    const char *fixed[] = {"FIXED_CYCLES", "FIXED_INSTRUCTIONS"};
    char *tok, *save = NULL;
    // Always count the fixed counters first so every record has cycles/instr.
    for (int i = 0; i < 2 + KPC_MAX_COUNTERS; i++) {
        const char *name;
        if (i < 2) name = fixed[i];
        else if (!(tok = strtok_r(i == 2 ? evlist : NULL, ",", &save))) break;
        else name = tok;
        kpep_event *ev;
        if (kpep_db_event(db, name, &ev)) { fprintf(stderr, "pmcrun: unknown event %s\n", name); return 1; }
        uint32_t err = 0;
        int r = kpep_config_add_event(cfg, &ev, 0, &err);
        if (r) {
            fprintf(stderr, "pmcrun: cannot add %s (ret %d, err %u): counter conflict or too many events\n", name, r, err);
            return 1;
        }
        snprintf(st.names[st.nev++], 64, "%s", name);
    }

    size_t reg_count = 0;
    kpc_config_t regs[KPC_MAX_COUNTERS] = {0};
    kpep_config_kpc_classes(cfg, &st.classes);
    kpep_config_kpc_count(cfg, &reg_count);
    kpep_config_kpc_map(cfg, st.map, sizeof(st.map));
    kpep_config_kpc(cfg, regs, sizeof(regs));

    if (kpc_force_all_ctrs_set(1)) { perror("kpc_force_all_ctrs_set (need root?)"); return 1; }
    if ((st.classes & KPC_CLASS_CONFIGURABLE_MASK) && reg_count && kpc_set_config(st.classes, regs)) {
        perror("kpc_set_config"); return 1;
    }
    if (kpc_set_counting(st.classes)) { perror("kpc_set_counting"); return 1; }
    if (kpc_set_thread_counting(st.classes)) { perror("kpc_set_thread_counting"); return 1; }

    FILE *f = fopen(STATE_FILE, "wb");
    if (!f || fwrite(&st, sizeof(st), 1, f) != 1) { perror(STATE_FILE); return 1; }
    fclose(f);
    chmod(STATE_FILE, 0644);
    printf("pmcrun: counting %d events (classes 0x%x, %u counters/cpu):\n",
           st.nev, st.classes, kpc_get_counter_count(st.classes));
    for (int i = 0; i < st.nev; i++) printf("  [%zu] %s\n", st.map[i], st.names[i]);
    return 0;
}

static int cmd_teardown(void) {
    kpc_set_counting(0);
    kpc_set_thread_counting(0);
    kpc_force_all_ctrs_set(0);
    unlink(STATE_FILE);
    printf("pmcrun: PMU released\n");
    return 0;
}

static int ncpus(void) {
    int n; size_t sz = sizeof(n);
    sysctlbyname("hw.ncpu", &n, &sz, NULL, 0);
    return n;
}

static int read_all(uint32_t classes, uint64_t *buf) {
    int cur;
    return kpc_get_cpu_counters(true, classes, &cur, buf);
}

static double epoch(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static int cmd_run(const char *log, char **argv) {
    // Without a setup state file, record only the child's rusage (fixed
    // instruction/cycle counters), which needs no privileges.
    state_t st = {0};
    bool have = false;
    FILE *sf = fopen(STATE_FILE, "rb");
    if (sf) {
        have = fread(&st, sizeof(st), 1, sf) == 1;
        fclose(sf);
    }

    int nc = ncpus();
    uint32_t per = have ? kpc_get_counter_count(st.classes) : 0;
    uint64_t *b0 = calloc((size_t)nc * per + 1, sizeof(uint64_t));
    uint64_t *b1 = calloc((size_t)nc * per + 1, sizeof(uint64_t));
    if (have && read_all(st.classes, b0) != 0) {
        fprintf(stderr, "pmcrun: kpc_get_cpu_counters failed (errno %d); recording rusage only\n", errno);
        have = false;
    }

    double e0 = epoch(), t0 = now();
    pid_t pid = fork();
    if (pid == 0) {
        execvp(argv[0], argv);
        perror(argv[0]);
        _exit(127);
    }
    // Wait without reaping so the zombie's rusage_info is still available.
    siginfo_t si;
    while (waitid(P_PID, pid, &si, WEXITED | WNOWAIT) < 0 && errno == EINTR) {}
    double t1 = now(), e1 = epoch();
    if (have) have = read_all(st.classes, b1) == 0;
    struct rusage_info_v6 ru;
    memset(&ru, 0, sizeof(ru));
    int rok = proc_pid_rusage(pid, RUSAGE_INFO_V6, (rusage_info_t *)&ru) == 0;
    int status;
    waitpid(pid, &status, 0);

    int fd = open(log, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        char cwd[1024] = "";
        getcwd(cwd, sizeof(cwd));
        size_t cap = 1 << 16, len = 0;
        char *out = malloc(cap);
#define P(...) len += snprintf(out + len, cap - len, __VA_ARGS__)
        P("{\"cwd\":\"%s\",\"cmd\":\"", cwd);
        for (char **a = argv; *a; a++) {
            for (const char *c = *a; *c && len < cap - 8; c++) {
                if (*c == '"' || *c == '\\') out[len++] = '\\';
                out[len++] = *c;
            }
            if (a[1]) out[len++] = ' ';
        }
        P("\",\"wall\":%.6f,\"start\":%.3f,\"end\":%.3f,\"status\":%d", t1 - t0, e0, e1, status);
        if (rok)
            P(",\"ru_instr\":%llu,\"ru_cycles\":%llu,\"ru_pinstr\":%llu,\"ru_pcycles\":%llu,"
              "\"ru_user_ns\":%llu,\"ru_sys_ns\":%llu,\"ru_user_pns\":%llu,\"ru_energy_nj\":%llu,\"ru_phys_footprint_peak\":%llu",
              ru.ri_instructions, ru.ri_cycles, ru.ri_pinstructions, ru.ri_pcycles,
              ru.ri_user_time, ru.ri_system_time, ru.ri_user_ptime, ru.ri_energy_nj,
              ru.ri_lifetime_max_phys_footprint);
        if (have) {
            P(",\"events\":[");
            for (int e = 0; e < st.nev; e++) P("%s\"%s\"", e ? "," : "", st.names[e]);
            P("],\"percpu\":[");
            for (int c = 0; c < nc; c++) {
                P("%s[", c ? "," : "");
                for (int e = 0; e < st.nev; e++) {
                    size_t k = (size_t)c * per + st.map[e];
                    P("%s%llu", e ? "," : "", b1[k] - b0[k]);
                }
                P("]");
            }
            P("]");
        }
        P("}\n");
#undef P
        write(fd, out, len);
        close(fd);
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 128 + WTERMSIG(status);
}

int main(int argc, char **argv) {
    if (argc < 2) goto usage;
    load_frameworks();
    if (!strcmp(argv[1], "list")) return cmd_list();
    if (!strcmp(argv[1], "setup") && argc == 3) return cmd_setup(argv[2]);
    if (!strcmp(argv[1], "teardown")) return cmd_teardown();
    if (!strcmp(argv[1], "run") && argc >= 5 && !strcmp(argv[2], "-o")) {
        int i = 4;
        if (!strcmp(argv[i], "--")) i++;
        if (i >= argc) goto usage;
        return cmd_run(argv[3], argv + i);
    }
usage:
    fprintf(stderr, "usage: pmcrun list | setup EV,EV,... | teardown | run -o LOG [--] cmd ...\n");
    return 2;
}

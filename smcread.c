// smcread: read Apple SMC keys via IOKit (no root needed).
//   smcread list            all keys with type and decoded value
//   smcread KEY [KEY ...]   decoded values of the given keys
//   smcread sample MS KEY|PREFIX* ...
//                           print a CSV row every MS milliseconds: epoch
//                           seconds, then each key's value; PREFIX* reports
//                           the max over all numeric keys with that prefix
// Build: clang -O2 -o smcread smcread.c -framework IOKit
#include <IOKit/IOKitLib.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { KERNEL_INDEX_SMC = 2, SMC_CMD_READ_BYTES = 5, SMC_CMD_READ_INDEX = 8, SMC_CMD_READ_KEYINFO = 9 };

typedef struct { char major, minor, build, reserved[1]; uint16_t release; } SMCVers;
typedef struct { uint16_t version, length; uint32_t cpuPLimit, gpuPLimit, memPLimit; } SMCPLimit;
typedef struct { uint32_t dataSize, dataType; uint8_t dataAttributes; } SMCKeyInfo;
typedef struct {
    uint32_t key;
    SMCVers vers;
    SMCPLimit pLimitData;
    SMCKeyInfo keyInfo;
    uint8_t result, status, data8;
    uint32_t data32;
    uint8_t bytes[32];
} SMCKeyData;

static io_connect_t conn;

static uint32_t str2key(const char *s) { return (uint32_t)s[0] << 24 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 8 | (uint32_t)s[3]; }
static void key2str(uint32_t k, char *s) { s[0] = k >> 24; s[1] = k >> 16; s[2] = k >> 8; s[3] = k; s[4] = 0; }

static kern_return_t call(SMCKeyData *in, SMCKeyData *out) {
    size_t osz = sizeof(*out);
    return IOConnectCallStructMethod(conn, KERNEL_INDEX_SMC, in, sizeof(*in), out, &osz);
}

static int read_key(uint32_t key, SMCKeyInfo *info, uint8_t *bytes) {
    SMCKeyData in = {0}, out = {0};
    in.key = key;
    in.data8 = SMC_CMD_READ_KEYINFO;
    if (call(&in, &out) != KERN_SUCCESS || out.result) return -1;
    *info = out.keyInfo;
    in.keyInfo.dataSize = info->dataSize;
    in.data8 = SMC_CMD_READ_BYTES;
    memset(&out, 0, sizeof(out));
    if (call(&in, &out) != KERN_SUCCESS || out.result) return -1;
    memcpy(bytes, out.bytes, 32);
    return 0;
}

// Decode common types; returns 1 and sets *v if numeric.
static int decode(const SMCKeyInfo *info, const uint8_t *b, double *v) {
    char t[5];
    key2str(info->dataType, t);
    if (!strcmp(t, "flt ") && info->dataSize == 4) { float f; memcpy(&f, b, 4); *v = f; return 1; }
    if (!strcmp(t, "ui8 ")) { *v = b[0]; return 1; }
    if (!strcmp(t, "ui16")) { *v = (b[0] << 8) | b[1]; return 1; }
    if (!strcmp(t, "ui32")) { *v = ((uint32_t)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]; return 1; }
    if (!strcmp(t, "sp78")) { *v = (int16_t)((b[0] << 8) | b[1]) / 256.0; return 1; }
    if (!strcmp(t, "fpe2")) { *v = ((b[0] << 8) | b[1]) / 4.0; return 1; }
    return 0;
}

static void print_key(uint32_t key) {
    SMCKeyInfo info;
    uint8_t b[32];
    char ks[5], ts[5];
    key2str(key, ks);
    if (read_key(key, &info, b)) { printf("%s  <unreadable>\n", ks); return; }
    key2str(info.dataType, ts);
    double v;
    if (decode(&info, b, &v)) printf("%s  [%s]  %.3f\n", ks, ts, v);
    else printf("%s  [%s]  (%u bytes)\n", ks, ts, info.dataSize);
}

static uint32_t key_count(void) {
    SMCKeyInfo info;
    uint8_t b[32];
    if (read_key(str2key("#KEY"), &info, b)) return 0;
    return ((uint32_t)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
}

static uint32_t key_at(uint32_t i) {
    SMCKeyData in = {0}, out = {0};
    in.data8 = SMC_CMD_READ_INDEX;
    in.data32 = i;
    return call(&in, &out) == KERN_SUCCESS ? out.key : 0;
}

static double read_value(uint32_t key) {
    SMCKeyInfo info;
    uint8_t b[32];
    double v;
    if (read_key(key, &info, b) || !decode(&info, b, &v)) return NAN;
    return v;
}

// Each column is one key, or the max over a set of keys (a PREFIX* group).
typedef struct { char name[16]; uint32_t *keys; int n; } column_t;

static int cmd_sample(int ms, int ncol, char **specs) {
    column_t *cols = calloc(ncol, sizeof(*cols));
    uint32_t nkeys = key_count();
    for (int c = 0; c < ncol; c++) {
        snprintf(cols[c].name, sizeof(cols[c].name), "%s", specs[c]);
        size_t len = strlen(specs[c]);
        cols[c].keys = calloc(nkeys + 1, sizeof(uint32_t));
        if (len && specs[c][len - 1] == '*') {
            for (uint32_t i = 0; i < nkeys; i++) {
                uint32_t k = key_at(i);
                char ks[5];
                key2str(k, ks);
                if (!strncmp(ks, specs[c], len - 1) && !isnan(read_value(k))) cols[c].keys[cols[c].n++] = k;
            }
        } else {
            cols[c].keys[cols[c].n++] = str2key(specs[c]);
        }
    }
    printf("time");
    for (int c = 0; c < ncol; c++) printf(",%s", cols[c].name);
    printf("\n");
    for (;;) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        printf("%.3f", ts.tv_sec + ts.tv_nsec * 1e-9);
        for (int c = 0; c < ncol; c++) {
            double m = NAN;
            for (int i = 0; i < cols[c].n; i++) {
                double v = read_value(cols[c].keys[i]);
                if (!isnan(v) && (isnan(m) || v > m)) m = v;
            }
            printf(",%.3f", m);
        }
        printf("\n");
        fflush(stdout);
        usleep(ms * 1000);
    }
}

int main(int argc, char **argv) {
    io_service_t svc = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("AppleSMC"));
    if (!svc || IOServiceOpen(svc, mach_task_self(), 0, &conn) != KERN_SUCCESS) {
        fprintf(stderr, "smcread: cannot open AppleSMC\n");
        return 1;
    }
    if (argc > 3 && !strcmp(argv[1], "sample")) return cmd_sample(atoi(argv[2]), argc - 3, argv + 3);
    if (argc > 1 && strcmp(argv[1], "list")) {
        for (int i = 1; i < argc; i++) print_key(str2key(argv[i]));
        return 0;
    }
    SMCKeyInfo info;
    uint8_t b[32];
    if (read_key(str2key("#KEY"), &info, b)) { fprintf(stderr, "smcread: cannot read #KEY\n"); return 1; }
    uint32_t n = ((uint32_t)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
    for (uint32_t i = 0; i < n; i++) {
        SMCKeyData in = {0}, out = {0};
        in.data8 = SMC_CMD_READ_INDEX;
        in.data32 = i;
        if (call(&in, &out) != KERN_SUCCESS) continue;
        print_key(out.key);
    }
    return 0;
}

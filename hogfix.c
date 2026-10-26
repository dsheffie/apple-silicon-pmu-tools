// hogfix SECONDS: spin as a fixed-priority (non-timeshare) user-interactive thread.
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/qos.h>
#include <time.h>
int main(int c, char **v) {
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    thread_extended_policy_data_t ext = {.timeshare = FALSE};
    kern_return_t kr = thread_policy_set(mach_thread_self(), THREAD_EXTENDED_POLICY, (thread_policy_t)&ext, THREAD_EXTENDED_POLICY_COUNT);
    thread_precedence_policy_data_t prec = {.importance = 63};
    kern_return_t kr2 = thread_policy_set(mach_thread_self(), THREAD_PRECEDENCE_POLICY, (thread_policy_t)&prec, THREAD_PRECEDENCE_POLICY_COUNT);
    if (kr || kr2) fprintf(stderr, "hogfix: policy kr=%d kr2=%d\n", kr, kr2);
    time_t end = time(0) + atoi(v[1]);
    volatile uint64_t x = 0;
    while (time(0) < end) for (int i = 0; i < 1000000; i++) x++;
    return 0;
}

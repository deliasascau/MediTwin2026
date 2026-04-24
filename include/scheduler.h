#pragma once
#include <Arduino.h>

// ─── Simple cooperative task scheduler ───────────────────────────────────────
// Fiecare task are: nume, interval (ms), timestamp ultima rulare, callback.
// sched.tick() se apeleaza din loop() — ruleaza fiecare task la intervalul sau.

#define SCHED_MAX_TASKS 8

typedef void (*TaskFn)(void);

struct SchedTask {
    const char *name;
    uint32_t    intervalMs;
    uint32_t    lastRunMs;
    TaskFn      fn;
};

class Scheduler {
public:
    void addTask(const char *name, uint32_t ms, TaskFn fn);
    void tick();

private:
    SchedTask _tasks[SCHED_MAX_TASKS];
    uint8_t   _count = 0;
};

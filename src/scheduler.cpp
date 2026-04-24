#include "scheduler.h"

void Scheduler::addTask(const char *name, uint32_t ms, TaskFn fn) {
    if (_count >= SCHED_MAX_TASKS) return;
    _tasks[_count++] = { name, ms, 0, fn };
}

void Scheduler::tick() {
    uint32_t now = millis();
    for (uint8_t i = 0; i < _count; i++) {
        if ((now - _tasks[i].lastRunMs) >= _tasks[i].intervalMs) {
            _tasks[i].lastRunMs = now;
            if (_tasks[i].fn) _tasks[i].fn();
        }
    }
}

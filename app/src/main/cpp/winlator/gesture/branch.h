#ifndef GESTURE_BRANCH_H
#define GESTURE_BRANCH_H

#include "../touch_processor_internal.h"

void gesture_branch(TouchFinger* f, GestureFingerCtx* ctx,
                    TouchActionResult* restrict result, uint64_t time_ms,
                    GestureProcessEvent event,
                    float dx, float dy);

#endif

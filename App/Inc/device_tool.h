#ifndef __DEVICE_TOOL_H
#define __DEVICE_TOOL_H

#include <stdint.h>

#define DEVICE_TOOL_OK              0
#define DEVICE_TOOL_ERR_PARAM      -1
#define DEVICE_TOOL_ERR_UNKNOWN    -2

int device_tool_execute(const char *action, int times);

#endif

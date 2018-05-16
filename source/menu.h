#pragma once

#include <switch.h>

typedef struct {
    const char* text;
    void (*callback)();
} MenuItem;

void menuPrint();
void menuSetCurrent(MenuItem* menuItems, void (*exitCallback)());
void menuUpdate(FsDeviceOperator* fsOperator);
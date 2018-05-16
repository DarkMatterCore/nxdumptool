#pragma once

#include <switch.h>

typedef struct _MenuItem {
    char* text;
    void (*callback)(struct _MenuItem* item);
    void* userdata;
} MenuItem;

void menuPrint();
void menuSetCurrent(MenuItem* menuItems, void (*exitCallback)());
void menuUpdate(FsDeviceOperator* fsOperator);
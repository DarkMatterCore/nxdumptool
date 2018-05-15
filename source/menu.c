#include "menu.h"

#include <stdio.h>
#include "ccolor.h"
#include "util.h"

MenuItem* menuCurrent;
int menuCurrentCount;
bool menuCardIsInserted;
int menuSelIndex = 0;

void menuPrint() {
    consoleClear();
    printf(C_CYAN "Game Card dump tool" C_RESET "\n");
    printf(menuCardIsInserted ? C_GREEN "Game Card is inserted\n" : C_RED "Game Card is NOT inserted\n");
    printf(C_RESET "\n");

    int index = 0;
    MenuItem* menuItems = menuCurrent;
    while (menuItems->text) {
        if (index == menuSelIndex)
            printf(C_INVERT "%s" C_RESET "\n", menuItems->text);
        else
            printf("%s\n", menuItems->text);
        menuItems++;
        index++;
    }
}

bool menuHandleGameCardStatus(FsDeviceOperator* fsOperator) {
    bool cardInserted = isGameCardInserted(fsOperator);
    if (menuCardIsInserted != cardInserted) {
        menuCardIsInserted = cardInserted;
        return true;
    }
    return false;
}

int menuHandleInput() {
    bool needsRefresh = false;
    u64 keys = hidKeysDown(CONTROLLER_P1_AUTO);
    if ((keys & KEY_A) && menuCurrent[menuSelIndex].callback != NULL) {
        menuCurrent[menuSelIndex].callback();
        return -1;
    }
    if (((keys & KEY_RSTICK_UP) | (keys & KEY_LSTICK_UP)) && menuSelIndex > 0) {
        menuSelIndex--;
        needsRefresh = true;
    }
    if (((keys & KEY_RSTICK_DOWN) | (keys & KEY_LSTICK_DOWN)) && menuSelIndex + 1 < menuCurrentCount) {
        menuSelIndex++;
        needsRefresh = true;
    }
    return needsRefresh ? 1 : 0;
}


void menuSetCurrent(MenuItem* menuItems) {
    menuCurrent = menuItems;
    menuCurrentCount = 0;
    while ((menuItems++)->text != NULL)
        menuCurrentCount++;
    menuSelIndex = 0;
    menuPrint();
}

void menuUpdate(FsDeviceOperator* fsOperator) {
    int inputStatus = menuHandleInput();
    if (inputStatus == -1)
        return;
    if (inputStatus || menuHandleGameCardStatus(fsOperator))
        menuPrint();
}
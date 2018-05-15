#include "util.h"
#include "fsext.h"

bool isGameCardInserted(FsDeviceOperator* o) {
    bool inserted;
    if (R_FAILED(fsDeviceOperatorIsGameCardInserted(o, &inserted)))
        return false;
    return inserted;
}
void syncDisplay() {
    gfxFlushBuffers();
    gfxSwapBuffers();
    gfxWaitForVsync();
}
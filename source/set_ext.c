#include <switch/arm/atomics.h>
#include <switch/services/sm.h>
#include <switch/types.h>
#include <stdlib.h>
#include <string.h>

#include "set_ext.h"

static Service g_setcalSrv;
static u64 g_refCntCal;

Result setcalInitialize(void)
{
    atomicIncrement64(&g_refCntCal);
    
    if (serviceIsActive(&g_setcalSrv)) return MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized);
    
    return smGetService(&g_setcalSrv, "set:cal");
}

void setcalExit(void)
{
    if (atomicDecrement64(&g_refCntCal) == 0) serviceClose(&g_setcalSrv);
}

Result setcalGetEticketDeviceKey(void *key)
{
    IpcCommand c;
    ipcInitialize(&c);
    ipcAddRecvBuffer(&c, key, 0x244, 0);
    
    struct {
        u64 magic;
        u64 cmd_id;
    } *raw;
    
    raw = ipcPrepareHeader(&c, sizeof(*raw));
    
    raw->magic = SFCI_MAGIC;
    raw->cmd_id = 21;
    
    Result rc = serviceIpcDispatch(&g_setcalSrv);
    
    if (R_SUCCEEDED(rc))
    {
        IpcParsedCommand r;
        ipcParse(&r);
        
        struct {
            u64 magic;
            u64 result;
        } *resp = r.Raw;
        
        rc = resp->result;
    }
    
    return rc;
}

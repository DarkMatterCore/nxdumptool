#include <switch/arm/atomics.h>
#include <switch/kernel/ipc.h>
#include <switch/services/sm.h>
#include <stdlib.h>
#include <string.h>

#include "es.h"

static Service g_esSrv;
static u64 g_esRefCnt;

Result esInitialize()
{
    atomicIncrement64(&g_esRefCnt);
    
    if (serviceIsActive(&g_esSrv)) return MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized);

    return smGetService(&g_esSrv, "es");
}

void esExit()
{
    if (atomicDecrement64(&g_esRefCnt) == 0) serviceClose(&g_esSrv);
}

Result esCountCommonTicket(u32 *num_tickets)
{
    IpcCommand c;
    ipcInitialize(&c);
    
    struct {
        u64 magic;
        u64 cmd_id;
    } *raw;
    
    raw = ipcPrepareHeader(&c, sizeof(*raw));
    
    raw->magic = SFCI_MAGIC;
    raw->cmd_id = 9;
    
    Result rc = serviceIpcDispatch(&g_esSrv);
    
    if (R_SUCCEEDED(rc))
    {
        IpcParsedCommand r;
        ipcParse(&r);
        
        struct {
            u64 magic;
            u64 result;
            u32 num_tickets;
        } *resp = r.Raw;
        
        rc = resp->result;
        
        if (R_SUCCEEDED(rc)) *num_tickets = resp->num_tickets;
    }
    
    return rc;
}

Result esCountPersonalizedTicket(u32 *num_tickets)
{
    IpcCommand c;
    ipcInitialize(&c);
    
    struct {
        u64 magic;
        u64 cmd_id;
    } *raw;
    
    raw = ipcPrepareHeader(&c, sizeof(*raw));
    
    raw->magic = SFCI_MAGIC;
    raw->cmd_id = 10;
    
    Result rc = serviceIpcDispatch(&g_esSrv);
    
    if (R_SUCCEEDED(rc))
    {
        IpcParsedCommand r;
        ipcParse(&r);
        
        struct {
            u64 magic;
            u64 result;
            u32 num_tickets;
        } *resp = r.Raw;
        
        rc = resp->result;
        
        if (R_SUCCEEDED(rc)) *num_tickets = resp->num_tickets;
    }
    
    return rc;
}

Result esListCommonTicket(u32 *numRightsIdsWritten, FsRightsId *outBuf, size_t bufSize)
{
    IpcCommand c;
    ipcInitialize(&c);
    ipcAddRecvBuffer(&c, outBuf, bufSize, BufferType_Normal);
    
    struct {
        u64 magic;
        u64 cmd_id;
    } *raw;
    
    raw = ipcPrepareHeader(&c, sizeof(*raw));
    
    raw->magic = SFCI_MAGIC;
    raw->cmd_id = 11;
    
    Result rc = serviceIpcDispatch(&g_esSrv);
    
    if (R_SUCCEEDED(rc))
    {
        IpcParsedCommand r;
        ipcParse(&r);
        
        struct {
            u64 magic;
            u64 result;
            u32 num_rights_ids_written;
        } *resp = r.Raw;
        
        rc = resp->result;
        
        if (R_SUCCEEDED(rc))
        {
            if (numRightsIdsWritten) *numRightsIdsWritten = resp->num_rights_ids_written;
        }
    }
    
    return rc;
}

Result esListPersonalizedTicket(u32 *numRightsIdsWritten, FsRightsId *outBuf, size_t bufSize)
{
    IpcCommand c;
    ipcInitialize(&c);
    ipcAddRecvBuffer(&c, outBuf, bufSize, BufferType_Normal);
    
    struct {
        u64 magic;
        u64 cmd_id;
    } *raw;
    
    raw = ipcPrepareHeader(&c, sizeof(*raw));
    raw->magic = SFCI_MAGIC;
    raw->cmd_id = 12;
    
    Result rc = serviceIpcDispatch(&g_esSrv);
    
    if (R_SUCCEEDED(rc))
    {
        IpcParsedCommand r;
        ipcParse(&r);
        
        struct {
            u64 magic;
            u64 result;
            u32 num_rights_ids_written;
        } *resp = r.Raw;
        
        rc = resp->result;
        
        if (R_SUCCEEDED(rc))
        {
            if (numRightsIdsWritten) *numRightsIdsWritten = resp->num_rights_ids_written;
        }
    }
    
    return rc;
}

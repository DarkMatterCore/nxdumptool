#include <switch/arm/atomics.h>
#include <switch/kernel/ipc.h>
#include <switch/services/sm.h>
#include <stdlib.h>
#include <string.h>

#include "es.h"
#include "service_guard.h"

static Service g_esSrv;

NX_GENERATE_SERVICE_GUARD(es);

Result _esInitialize() {
    return smGetService(&g_esSrv, "es");
}

void _esCleanup() {
    serviceClose(&g_esSrv);
}

Result esCountCommonTicket(u32 *num_tickets)
{
    struct {
        u32 num_tickets;
    } out;
    
    Result rc = serviceDispatchOut(&g_esSrv, 9, out);
    if (R_SUCCEEDED(rc) && num_tickets) *num_tickets = out.num_tickets;
    
    return rc;
}

Result esCountPersonalizedTicket(u32 *num_tickets)
{
    struct {
        u32 num_tickets;
    } out;
    
    Result rc = serviceDispatchOut(&g_esSrv, 10, out);
    if (R_SUCCEEDED(rc) && num_tickets) *num_tickets = out.num_tickets;
    
    return rc;
}

Result esListCommonTicket(u32 *numRightsIdsWritten, FsRightsId *outBuf, size_t bufSize)
{
    struct {
        u32 num_rights_ids_written;
    } out;
    
    Result rc = serviceDispatchInOut(&g_esSrv, 11, *numRightsIdsWritten, out,
        .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
        .buffers = { { outBuf, bufSize } },
    );
    
    if (R_SUCCEEDED(rc) && numRightsIdsWritten) *numRightsIdsWritten = out.num_rights_ids_written;
    
    return rc;
}

Result esListPersonalizedTicket(u32 *numRightsIdsWritten, FsRightsId *outBuf, size_t bufSize)
{
    struct {
        u32 num_rights_ids_written;
    } out;
    
    Result rc = serviceDispatchInOut(&g_esSrv, 12, *numRightsIdsWritten, out,
        .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
        .buffers = { { outBuf, bufSize } },
    );
    
    if (R_SUCCEEDED(rc) && numRightsIdsWritten) *numRightsIdsWritten = out.num_rights_ids_written;
    
    return rc;
}

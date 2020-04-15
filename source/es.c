#include <switch/services/sm.h>
#include <stdlib.h>
#include <string.h>

#include "es.h"
#include "service_guard.h"

static Service g_esSrv = {0};

NX_GENERATE_SERVICE_GUARD(es);

Result esCountCommonTicket(s32 *out_count)
{
    struct {
        s32 num_tickets;
    } out;
    
    Result rc = serviceDispatchOut(&g_esSrv, 9, out);
    if (R_SUCCEEDED(rc) && out_count) *out_count = out.num_tickets;
    
    return rc;
}

Result esCountPersonalizedTicket(s32 *out_count)
{
    struct {
        s32 num_tickets;
    } out;
    
    Result rc = serviceDispatchOut(&g_esSrv, 10, out);
    if (R_SUCCEEDED(rc) && out_count) *out_count = out.num_tickets;
    
    return rc;
}

Result esListCommonTicket(s32 *out_entries_written, FsRightsId *out_ids, s32 count)
{
    struct {
        s32 num_rights_ids_written;
    } out;
    
    Result rc = serviceDispatchInOut(&g_esSrv, 11, *out_entries_written, out,
        .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
        .buffers = { { out_ids, count * sizeof(FsRightsId) } }
    );
    
    if (R_SUCCEEDED(rc) && out_entries_written) *out_entries_written = out.num_rights_ids_written;
    
    return rc;
}

Result esListPersonalizedTicket(s32 *out_entries_written, FsRightsId *out_ids, s32 count)
{
    struct {
        s32 num_rights_ids_written;
    } out;
    
    Result rc = serviceDispatchInOut(&g_esSrv, 12, *out_entries_written, out,
        .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
        .buffers = { { out_ids, count * sizeof(FsRightsId) } }
    );
    
    if (R_SUCCEEDED(rc) && out_entries_written) *out_entries_written = out.num_rights_ids_written;
    
    return rc;
}

NX_INLINE Result _esInitialize(void)
{
    return smGetService(&g_esSrv, "es");
}

static void _esCleanup(void)
{
    serviceClose(&g_esSrv);
}

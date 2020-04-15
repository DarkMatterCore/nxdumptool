#pragma once

#ifndef __ES_H__
#define __ES_H__

#include <switch.h>

Result esInitialize(void);
void esExit(void);

Result esCountCommonTicket(s32 *out_count);
Result esCountPersonalizedTicket(s32 *out_count);
Result esListCommonTicket(s32 *out_entries_written, FsRightsId *out_ids, s32 count);
Result esListPersonalizedTicket(s32 *out_entries_written, FsRightsId *out_ids, s32 count);

#endif /* __ES_H__ */

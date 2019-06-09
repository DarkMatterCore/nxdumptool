#pragma once

#ifndef __ES_H__
#define __ES_H__

#include <switch.h>

Result esInitialize();
void esExit();

Result esCountCommonTicket(u32 *num_tickets);
Result esCountPersonalizedTicket(u32 *num_tickets);
Result esListCommonTicket(u32 *numRightsIdsWritten, NcmRightsId *outBuf, size_t bufSize);
Result esListPersonalizedTicket(u32 *numRightsIdsWritten, NcmRightsId *outBuf, size_t bufSize);

#endif

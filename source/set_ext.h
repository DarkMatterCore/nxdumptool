#pragma once

#ifndef __SET_EXT_H__
#define __SET_EXT_H__

#include <switch.h>

Result setcalInitialize(void);
void setcalExit(void);

/**
 * @brief Gets the extended ETicket RSA-2048 Key from CAL0
 * @param key Pointer to 0x244-byte output buffer.
 */
Result setcalGetEticketDeviceKey(void *key);

#endif

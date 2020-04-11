#pragma once

#ifndef __KEYS_H__
#define __KEYS_H__

#define KEYS_FILE_PATH  "sdmc:/switch/prod.keys"    /* Location used by Lockpick_RCM */

bool keysLoadNcaKeyset(void);

const u8 *keysGetNcaHeaderKey(void);
const u8 *keysGetKeyAreaEncryptionKeySource(u8 kaek_index);
const u8 *keysGetEticketRsaKek(void);
const u8 *keysGetTitlekek(u8 key_generation);
const u8 *keysGetKeyAreaEncryptionKey(u8 key_generation, u8 kaek_index);

#endif /* __KEYS_H__ */

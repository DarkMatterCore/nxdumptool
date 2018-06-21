#pragma once

#ifndef __NCMEXT_H__
#define __NCMEXT_H__

#include <switch/types.h>
#include <switch/services/fs.h>

typedef struct {
	Service s;
} ncmContentMetaDatabase;

typedef struct {
	u64 titleID;
	u32 titleVersion;
	u8 type;
	u8 unk1;
	u16 unk2;
} ncmContentMetaKey;

typedef struct {
	ncmContentMetaKey meta_record;
	u64 base_title_id;
} ncmApplicationMetaKey;

// IContentManager
Result ncmInitialize(void);
void ncmExit(void);
Result ncmGetContentMetaDatabase(ncmContentMetaDatabase *out, FsStorageId storage_id);
Result ncmOpenContentMetaDatabase(FsStorageId storage_id);
Result ncmCloseContentMetaDatabase(FsStorageId storage_id);

// IContentMetaDatabase
Result ncmMetaDatabaseListApplication(ncmContentMetaDatabase *md, ncmApplicationMetaKey *buffer, size_t size, u8 filter);

#endif

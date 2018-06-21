#include <switch/arm/atomics.h>
#include <switch/kernel/ipc.h>
#include <stdlib.h>
#include <string.h>

#include "ncmext.h"

static Service g_ncm;
static u64 g_refCnt;

// IContentManager
Result ncmInitialize(void)
{
	atomicIncrement64(&g_refCnt);
	
	if (serviceIsActive(&g_ncm)) return 0;
	
	Result rc = smGetService(&g_ncm, "ncm");
	
	return rc;
}

void ncmExit(void)
{
	if (atomicDecrement64(&g_refCnt) == 0) serviceClose(&g_ncm);
}

Result ncmGetContentMetaDatabase(ncmContentMetaDatabase *out, FsStorageId storage_id)
{
	IpcCommand c;
	ipcInitialize(&c);
	
	struct {
		u64 magic;
		u64 cmd_id;
		u8 storage_id;
	} *raw;
	
	raw = ipcPrepareHeader(&c, sizeof(*raw));
	
	raw->magic = SFCI_MAGIC;
	raw->cmd_id = 5;
	raw->storage_id = storage_id;
	
	Result rc = serviceIpcDispatch(&g_ncm);
	
	if (R_SUCCEEDED(rc))
	{
		IpcParsedCommand r;
		ipcParse(&r);
		
		struct {
			u64 magic;
			u64 result;
		} *resp = r.Raw;
		
		rc = resp->result;
		
		if (R_SUCCEEDED(rc)) serviceCreate(&out->s, r.Handles[0]);
	}
	
	return rc;
}

Result ncmOpenContentMetaDatabase(FsStorageId storage_id)
{
	IpcCommand c;
	ipcInitialize(&c);
	
	struct {
		u64 magic;
		u64 cmd_id;
		u8 storage_id;
	} *raw;
	
	raw = ipcPrepareHeader(&c, sizeof(*raw));
	
	raw->magic = SFCI_MAGIC;
	raw->cmd_id = 11;
	raw->storage_id = storage_id;
	
	Result rc = serviceIpcDispatch(&g_ncm);
	
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

Result ncmCloseContentMetaDatabase(FsStorageId storage_id)
{
	IpcCommand c;
	ipcInitialize(&c);
	
	struct {
		u64 magic;
		u64 cmd_id;
		u8 storage_id;
	} *raw;
	
	raw = ipcPrepareHeader(&c, sizeof(*raw));
	
	raw->magic = SFCI_MAGIC;
	raw->cmd_id = 12;
	raw->storage_id = storage_id;
	
	Result rc = serviceIpcDispatch(&g_ncm);
	
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

// IContentMetaDatabase
Result ncmMetaDatabaseListApplication(ncmContentMetaDatabase *md, ncmApplicationMetaKey *buffer, size_t size, u8 filter)
{
	IpcCommand c;
	ipcInitialize(&c);
	ipcAddRecvBuffer(&c, buffer, size, BufferType_Normal);
	
	struct {
		u64 magic;
		u64 cmd_id;
		u8 filter;
	} *raw;
	
	raw = ipcPrepareHeader(&c, sizeof(*raw));
	
	raw->magic = SFCI_MAGIC;
	raw->cmd_id = 7;
	raw->filter = filter;
	
	Result rc = serviceIpcDispatch(&md->s);
	
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <switch/services/ns.h>
#include <libxml/globals.h>
#include <libxml/xpath.h>

#include "dumper.h"
#include "fsext.h"
#include "ncmext.h"
#include "ui.h"
#include "util.h"

extern int breaks;

extern u64 gameCardSize;
extern u64 gameCardTitleID;
extern u32 hfs0_partition_cnt;

const char *nswReleasesXmlPath = "sdmc:/NSWreleases.xml";
const char *nswReleasesRootElement = "releases";
const char *nswReleasesChildren = "release";
const char *nswReleasesChildrenImageSize = "imagesize";
const char *nswReleasesChildrenTitleID = "titleid";
const char *nswReleasesChildrenImgCrc = "imgcrc";
const char *nswReleasesChildrenReleaseName = "releasename";
const char *nswReleasesChildrenCard = "card";

bool isGameCardInserted(FsDeviceOperator* o)
{
    bool inserted;
	if (R_FAILED(fsDeviceOperatorIsGameCardInserted(o, &inserted))) return false;
    return inserted;
}

void syncDisplay()
{
    gfxFlushBuffers();
    gfxSwapBuffers();
    gfxWaitForVsync();
}

void delay(u8 seconds)
{
	if (!seconds) return;
	
	time_t timer;
	time(&timer);
	
	while(time(NULL) < (timer + seconds)) syncDisplay();
}

bool getGameCardTitleID(u64 *titleID)
{
	bool success = false;
	
	Result result;
	ncmContentMetaDatabase ncmDb;
	
	ncmApplicationMetaKey *appList = (ncmApplicationMetaKey*)malloc(sizeof(ncmApplicationMetaKey));
	if (appList)
	{
		memset(appList, 0, sizeof(ncmApplicationMetaKey));
		
		if (R_SUCCEEDED(result = ncmOpenContentMetaDatabase(FsStorageId_GameCard)))
		{
			if (R_SUCCEEDED(result = ncmGetContentMetaDatabase(&ncmDb, FsStorageId_GameCard)))
			{
				if (R_SUCCEEDED(result = ncmMetaDatabaseListApplication(&ncmDb, appList, sizeof(ncmApplicationMetaKey), 0)))
				{
					*titleID = appList->meta_record.titleID;
					success = true;
				} else {
					uiStatusMsg("getGameCardTitleID: MetaDatabaseListApplication failed! (0x%08x)", result);
				}
			} else {
				uiStatusMsg("getGameCardTitleID: GetContentMetaDatabase failed! (0x%08x)", result);
			}
		} else {
			uiStatusMsg("getGameCardTitleID: OpenContentMetaDatabase failed! (0x%08x)", result);
		}
		
		// Seems to cause problems
		//if (R_FAILED(result = ncmCloseContentMetaDatabase(FsStorageId_GameCard))) uiStatusMsg("getGameCardTitleID: CloseContentMetaDatabase failed! (0x%08x)", result);
		
		free(appList);
	} else {
		uiStatusMsg("getGameCardTitleID: Unable to allocate memory for the NCM service operations.");
	}
	
	return success;
}

bool getGameCardControlNacp(u64 titleID, char *nameBuf, int nameBufSize, char *authorBuf, int authorBufSize, char *versionBuf, int versionBufSize)
{
	if (titleID == 0) return false;
	
	bool success = false;
	Result result;
	size_t outsize = 0;
	NsApplicationControlData *buf = NULL;
	NacpLanguageEntry *langentry = NULL;
	
	buf = (NsApplicationControlData*)malloc(sizeof(NsApplicationControlData));
	if (buf)
	{
		memset(buf, 0, sizeof(NsApplicationControlData));
		
		if (R_SUCCEEDED(result = nsGetApplicationControlData(1, titleID, buf, sizeof(NsApplicationControlData), &outsize)))
		{
			if (outsize >= sizeof(buf->nacp))
			{
				if (R_SUCCEEDED(result = nacpGetLanguageEntry(&buf->nacp, &langentry)))
				{
					strncpy(nameBuf, langentry->name, nameBufSize - 1);
					strncpy(authorBuf, langentry->author, authorBufSize - 1);
					strncpy(versionBuf, buf->nacp.version, versionBufSize - 1);
					
					success = true;
				} else {
					uiStatusMsg("getGameCardControlNacp: GetLanguageEntry failed! (0x%08x)", result);
				}
			} else {
				uiStatusMsg("getGameCardControlNacp: Control.nacp buffer size (%u bytes) is too small! Expected: %u bytes", outsize, sizeof(buf->nacp));
			}
		} else {
			uiStatusMsg("getGameCardControlNacp: GetApplicationControlData failed! (0x%08x)", result);
		}
		
		free(buf);
	} else {
		uiStatusMsg("getGameCardControlNacp: Unable to allocate memory for the NS service operations.");
	}
	
	return success;
}

int getSdCardFreeSpace(u64 *out)
{
	struct statvfs st;
	int rc;
	
	rc = statvfs("sdmc:/", &st);
	if (rc != 0)
	{
		uiStatusMsg("getSdCardFreeSpace: Unable to get SD card filesystem stats! statvfs: %d (%s).", errno, strerror(errno));
	} else {
		*out = (u64)(st.f_bsize * st.f_bfree);
	}
	
	return rc;
}

#define KiB (1024.0)
#define MiB (1024.0 * KiB)
#define GiB (1024.0 * MiB)

void convertSize(u64 size, char *out, int bufsize)
{
	char buffer[16];
	double bytes = (double)size;
	
	if (bytes < 1000.0)
	{
		snprintf(buffer, sizeof(buffer), "%.0lf B", bytes);
	} else
	if (bytes < 10.0*KiB)
	{
		snprintf(buffer, sizeof(buffer), "%.2lf KiB", floor((bytes*100.0)/KiB)/100.0);
	} else
	if (bytes < 100.0*KiB)
	{
		snprintf(buffer, sizeof(buffer), "%.1lf KiB", floor((bytes*10.0)/KiB)/10.0);
	} else
	if (bytes < 1000.0*KiB)
	{
		snprintf(buffer, sizeof(buffer), "%.0lf KiB", floor(bytes/KiB));
	} else
	if (bytes < 10.0*MiB)
	{
		snprintf(buffer, sizeof(buffer), "%.2lf MiB", floor((bytes*100.0)/MiB)/100.0);
	} else
	if (bytes < 100.0*MiB)
	{
		snprintf(buffer, sizeof(buffer), "%.1lf MiB", floor((bytes*10.0)/MiB)/10.0);
	} else
	if (bytes < 1000.0*MiB)
	{
		snprintf(buffer, sizeof(buffer), "%.0lf MiB", floor(bytes/MiB));
	} else
	if (bytes < 10.0*GiB)
	{
		snprintf(buffer, sizeof(buffer), "%.2lf GiB", floor((bytes*100.0)/GiB)/100.0);
	} else
	if (bytes < 100.0*GiB)
	{
		snprintf(buffer, sizeof(buffer), "%.1lf GiB", floor((bytes*10.0)/GiB)/10.0);
	} else {
		snprintf(buffer, sizeof(buffer), "%.0lf GiB", floor(bytes/GiB));
	}
	
	snprintf(out, bufsize, "%s", buffer);
}

void getCurrentTimestamp(char *out, int bufsize)
{
	time_t timer = time(NULL);
	struct tm *timeinfo = localtime(&timer);
	
	char buffer[32] = {'\0'};
	strftime(buffer, sizeof(buffer) / sizeof(buffer[0]), "%Y%m%d-%H%M%S", timeinfo);
	
	snprintf(out, bufsize, "%s", buffer);
}

void waitForButtonPress()
{
	uiDrawString("Press any button to continue", 0, breaks * 8, 255, 255, 255);
	
	syncDisplay();
	
	while(true)
	{
		hidScanInput();
		
		u32 keysDown = hidKeysDown(CONTROLLER_P1_AUTO);
		if (keysDown && !(keysDown & KEY_TOUCH)) break;
	}
}

bool isDirectory(char *path)
{
	DIR* dir = opendir(path);
	if (!dir) return false;
	
	closedir(dir);
	return true;
}

void addString(char **filenames, int *filenamesCount, char **nextFilename, const char *string)
{
	filenames[(*filenamesCount)++] = *nextFilename;
	strcpy(*nextFilename, string);
	*nextFilename += strlen(string) + 1;
}

static int sortAlpha(const void* a, const void* b)
{
	return strcasecmp(*((const char**)a), *((const char**)b));
}

void getDirectoryContents(char *filenameBuffer, char **filenames, int *filenamesCount, const char *directory, bool skipParent)
{
	struct dirent *ent;
	int i, maxFilenamesCount = *filenamesCount;
	char *nextFilename = filenameBuffer;
	
	char *slash = (char*)malloc(strlen(directory) + 2);
	memset(slash, 0, strlen(directory) + 2);
	snprintf(slash, strlen(directory) + 2, "%s/", directory);
	
	*filenamesCount = 0;
	
	if (!skipParent) addString(filenames, filenamesCount, &nextFilename, "..");
	
	DIR* dir = opendir(slash);
	if (dir)
	{
		for(i = 0; i < maxFilenamesCount; i++)
		{
			ent = readdir(dir);
			if (!ent) break;
			
			if ((strlen(ent->d_name) == 1 && !strcmp(ent->d_name, ".")) || (strlen(ent->d_name) == 2 && !strcmp(ent->d_name, ".."))) continue;
			
			addString(filenames, filenamesCount, &nextFilename, ent->d_name);
		}
		
		closedir(dir);
	}
	
	free(slash);
	
	// ".." should stay at the top
	qsort(filenames + 1, (*filenamesCount) - 1, sizeof(char*), &sortAlpha);
}

bool parseNSWDBRelease(xmlDocPtr doc, xmlNodePtr cur, u32 crc, u32 cnt, char *releaseName, int bufsize)
{
	xmlChar *key;
	xmlNodePtr node = cur;
	
	u8 imageSize = (u8)(gameCardSize / GAMECARD_SIZE_1GiB);
	u8 card = (hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT ? 1 : 2);
	
	u8 xmlImageSize = 0;
	u64 xmlTitleID = 0;
	u32 xmlCrc = 0;
	u8 xmlCard = 0;
	char xmlReleaseName[256] = {'\0'};
	
	bool found = false;
	char strbuf[512] = {'\0'};
	
	while (node != NULL)
	{
		if ((!xmlStrcmp(node->name, (const xmlChar *)nswReleasesChildrenImageSize)))
		{
			key = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
			
			xmlImageSize = (u8)atoi((const char*)key);
			
			xmlFree(key);
		} else
		if ((!xmlStrcmp(node->name, (const xmlChar *)nswReleasesChildrenTitleID)))
		{
			key = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
			
			xmlTitleID = strtoull((const char*)key, NULL, 16);
			
			xmlFree(key);
		} else
		if ((!xmlStrcmp(node->name, (const xmlChar *)nswReleasesChildrenImgCrc)))
		{
			key = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
			
			xmlCrc = strtoul((const char*)key, NULL, 16);
			
			xmlFree(key);
		} else
		if ((!xmlStrcmp(node->name, (const xmlChar *)nswReleasesChildrenReleaseName)))
		{
			key = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
			
			snprintf(xmlReleaseName, sizeof(xmlReleaseName) / sizeof(xmlReleaseName[0]), "%s", (char*)key);
			
			xmlFree(key);
		} else
		if ((!xmlStrcmp(node->name, (const xmlChar *)nswReleasesChildrenCard)))
		{
			key = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
			
			xmlCard = (u8)atoi((const char*)key);
			
			xmlFree(key);
		}
		
		node = node->next;
	}
	
	//snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Cartridge Image Size: %u\nCartridge Title ID: %016lX\nCartridge Image CRC32: %08X\nCartridge Type: %u\n\nXML Image Size: %u\nXML Title ID: %016lX\nXML Image CRC32: %08X\nXML Release Name: %s\nXML Card Type: %u", imageSize, gameCardTitleID, crc, card, xmlImageSize, xmlTitleID, xmlCrc, xmlReleaseName, xmlCard);
	//uiDrawString(strbuf, 0, 0, 255, 255, 255);
	
	if (xmlImageSize == imageSize && xmlTitleID == gameCardTitleID && xmlCrc == crc && xmlCard == card)
	{
		snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Found matching Scene release: \"%s\" (CRC32: %08X). This is a good dump!", xmlReleaseName, xmlCrc);
		uiDrawString(strbuf, 0, breaks * 8, 0, 255, 0);
		
		snprintf(releaseName, bufsize, "%s", xmlReleaseName);
		
		found = true;
	} else {
		snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Dump doesn't match Scene release: \"%s\"! (CRC32: %08X)", xmlReleaseName, xmlCrc);
		uiDrawString(strbuf, 0, breaks * 8, 255, 0, 0);
	}
	
	breaks++;
	
	return found;
}

xmlXPathObjectPtr getNodeSet(xmlDocPtr doc, xmlChar *xpath)
{
	xmlXPathContextPtr context = NULL;
	xmlXPathObjectPtr result = NULL;
	
	context = xmlXPathNewContext(doc);
	result = xmlXPathEvalExpression(xpath, context);
	
	if (xmlXPathNodeSetIsEmpty(result->nodesetval))
	{
		xmlXPathFreeObject(result);
		return NULL;
	}
	
	return result;
}

bool gameCardDumpNSWDBCheck(u32 crc, char *releaseName, int bufsize)
{
	if (!gameCardTitleID || !hfs0_partition_cnt || !crc) return false;
	
	xmlDocPtr doc = NULL;
	bool found = false;
	char strbuf[512] = {'\0'};
	
	doc = xmlParseFile(nswReleasesXmlPath);
	if (doc)
	{
		snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "//%s/%s[.//%s='%016lX']", nswReleasesRootElement, nswReleasesChildren, nswReleasesChildrenTitleID, gameCardTitleID);
		xmlXPathObjectPtr nodeSet = getNodeSet(doc, (xmlChar*)strbuf);
		if (nodeSet)
		{
			snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Found %d %s with Title ID \"%016lX\"", nodeSet->nodesetval->nodeNr, (nodeSet->nodesetval->nodeNr > 1 ? "releases" : "release"), gameCardTitleID);
			uiDrawString(strbuf, 0, breaks * 8, 255, 255, 255);
			breaks++;
			
			u32 i;
			for (i = 0; i < nodeSet->nodesetval->nodeNr; i++)
			{
				xmlNodePtr node = nodeSet->nodesetval->nodeTab[i]->xmlChildrenNode;
				
				found = parseNSWDBRelease(doc, node, crc, i, releaseName, bufsize);
				if (found) break;
			}
			
			if (!found)
			{
				uiDrawString("No matches found in XML document! This could either be a bad dump or an undumped cartridge.", 0, breaks * 8, 255, 0, 0);
			} else {
				breaks--;
			}
			
			xmlXPathFreeObject(nodeSet);
		} else {
			snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: unable to find records with Title ID \"%016lX\" within the XML document!", gameCardTitleID);
			uiDrawString(strbuf, 0, 0, 255, 0, 0);
		}
		
		xmlFreeDoc(doc);
	} else {
		snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to open and/or parse \"%s\"!", nswReleasesXmlPath);
		uiDrawString(strbuf, 0, breaks * 8, 255, 0, 0);
	}
	
	return found;
}

char *RemoveIllegalCharacters(char *name)
{
	u32 i, len = strlen(name);
	for (i = 0; i < len; i++)
	{
		if (memchr("?[]/\\=+<>:;\",*|^", name[i], sizeof("?[]/\\=+<>:;\",*|^") - 1) || name[i] < 0x20 || name[i] > 0x7E) name[i] = '_';
	}
	return name;
}

#include <stdio.h>
#include <malloc.h>
#include <switch.h>
#include <memory.h>

#include "dumper.h"
#include "ncmext.h"
#include "ui.h"
#include "util.h"

FsDeviceOperator fsOperatorInstance;

bool gameCardInserted;

u64 gameCardSize = 0;
char gameCardSizeStr[32] = {'\0'};

char *hfs0_header = NULL;
u64 hfs0_offset = 0, hfs0_size = 0;
u32 hfs0_partition_cnt = 0;

u64 gameCardTitleID = 0;
char gameCardName[0x201] = {'\0'}, gameCardAuthor[0x101] = {'\0'}, gameCardVersion[0x11] = {'\0'};

u32 currentFBWidth, currentFBHeight;
u8 *currentFB;

int main(int argc, char **argv)
{
	gfxInitResolutionDefault();
	gfxInitDefault();
	gfxConfigureAutoResolutionDefault(true);
	
	uiInit();
	
	currentFB = gfxGetFramebuffer(&currentFBWidth, &currentFBHeight);
	
	int ret = 0;
	Result result;
	char strbuf[512] = {'\0'};
	
	if (R_SUCCEEDED(result = fsInitialize()))
	{
		if (R_SUCCEEDED(result = fsOpenDeviceOperator(&fsOperatorInstance)))
		{
			if (R_SUCCEEDED(result = ncmInitialize()))
			{
				if (R_SUCCEEDED(result = nsInitialize()))
				{
					bool exitLoop = false;
					
					while(appletMainLoop())
					{
						currentFB = gfxGetFramebuffer(&currentFBWidth, &currentFBHeight);
						
						uiPrintHeadline();
						
						gameCardInserted = isGameCardInserted(&fsOperatorInstance);
						
						if (gameCardInserted)
						{
							if (hfs0_header == NULL)
							{
								// Don't access the gamecard immediately to avoid conflicts with the fsp-srv, ncm and ns services
								uiPleaseWait();
								
								getRootHfs0Header(&fsOperatorInstance);
								getGameCardTitleID(&gameCardTitleID);
								getGameCardControlNacp(gameCardTitleID, gameCardName, sizeof(gameCardName), gameCardAuthor, sizeof(gameCardAuthor), gameCardVersion, sizeof(gameCardVersion));
								
								uiPrintHeadline();
								uiUpdateStatusMsg();
							}
						} else {
							if (hfs0_header != NULL)
							{
								gameCardSize = 0;
								memset(gameCardSizeStr, 0, sizeof(gameCardSizeStr));
								
								free(hfs0_header);
								hfs0_header = NULL;
								hfs0_offset = hfs0_size = 0;
								hfs0_partition_cnt = 0;
								
								gameCardTitleID = 0;
								
								memset(gameCardName, 0, sizeof(gameCardName));
								memset(gameCardAuthor, 0, sizeof(gameCardAuthor));
								memset(gameCardVersion, 0, sizeof(gameCardVersion));
							}
						}
						
						hidScanInput();
						u32 keysDown = hidKeysDown(CONTROLLER_P1_AUTO);
						
						UIResult result = uiLoop(keysDown);
						switch(result)
						{
							case resultShowMainMenu:
								uiSetState(stateMainMenu);
								break;
							case resultShowXciDumpMenu:
								uiSetState(stateXciDumpMenu);
								break;
							case resultDumpXci:
								uiSetState(stateDumpXci);
								break;
							case resultShowRawPartitionDumpMenu:
								uiSetState(stateRawPartitionDumpMenu);
								break;
							case resultDumpRawPartition:
								uiSetState(stateDumpRawPartition);
								break;
							case resultShowPartitionDataDumpMenu:
								uiSetState(statePartitionDataDumpMenu);
								break;
							case resultDumpPartitionData:
								uiSetState(stateDumpPartitionData);
								break;
							case resultShowViewGameCardFsMenu:
								uiSetState(stateViewGameCardFsMenu);
								break;
							case resultShowViewGameCardFsGetList:
								uiSetState(stateViewGameCardFsGetList);
								break;
							case resultShowViewGameCardFsBrowser:
								uiSetState(stateViewGameCardFsBrowser);
								break;
							case resultViewGameCardFsBrowserCopyFile:
								uiSetState(stateViewGameCardFsBrowserCopyFile);
								break;
							case resultDumpGameCardCertificate:
								uiSetState(stateDumpGameCardCertificate);
								break;
							case resultExit:
								exitLoop = true;
								break;
							default:
								break;
						}
						
						if (exitLoop) break;
						
						syncDisplay();
					}
					
					nsExit();
				} else {
					snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to initialize the ns service! (0x%08x)", result);
					uiDrawString(strbuf, 0, 0, 255, 255, 255);
					delay(5);
					ret = -4;
				}
				
				ncmExit();
			} else {
				snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to initialize the ncm service! (0x%08x)", result);
				uiDrawString(strbuf, 0, 0, 255, 255, 255);
				delay(5);
				ret = -3;
			}
			
			fsDeviceOperatorClose(&fsOperatorInstance);
		} else {
			snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to open device operator! (0x%08x)", result);
			uiDrawString(strbuf, 0, 0, 255, 255, 255);
			delay(5);
			ret = -2;
		}
		
		fsExit();
	} else {
		snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to initialize the fsp-srv service! (0x%08x)", result);
		uiDrawString(strbuf, 0, 0, 255, 255, 255);
		delay(5);
		ret = -1;
	}
	
	if (hfs0_header != NULL) free(hfs0_header);
	
	uiDeinit();
	
    gfxExit();
	
    return ret;
}

#include <stdio.h>
#include <malloc.h>
#include <switch.h>
#include <memory.h>
#include <switch/services/ns.h>

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
	
	if (R_SUCCEEDED(ncmInitialize()))
	{
		if (R_SUCCEEDED(nsInitialize()))
		{
			if (R_SUCCEEDED(fsOpenDeviceOperator(&fsOperatorInstance)))
			{
				bool exitLoop = false;
				
				while(appletMainLoop())
				{
					currentFB = gfxGetFramebuffer(&currentFBWidth, &currentFBHeight);
					
					gameCardInserted = isGameCardInserted(&fsOperatorInstance);
					
					if (gameCardInserted)
					{
						if (hfs0_header == NULL)
						{
							// Don't access the gamecard immediately to avoid conflicts with the fs-srv, ncm and ns services
							delay(1);
							
							getRootHfs0Header(&fsOperatorInstance);
							
							getGameCardTitleID(&gameCardTitleID);
							
							getGameCardControlNacp(gameCardTitleID, gameCardName, sizeof(gameCardName), gameCardAuthor, sizeof(gameCardAuthor), gameCardVersion, sizeof(gameCardVersion));
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
				
				fsDeviceOperatorClose(&fsOperatorInstance);
			} else {
				uiDrawString("Failed to open device operator.", 0, 0, 255, 255, 255);
				delay(5);
				ret = -3;
			}
			
			nsExit();
		} else {
			uiDrawString("Failed to initialize the NS service.", 0, 0, 255, 255, 255);
			delay(5);
			ret = -2;
		}
		
		ncmExit();
	} else {
		uiDrawString("Failed to initialize the NCM service.", 0, 0, 255, 255, 255);
		delay(5);
		ret = -1;
	}
	
	if (hfs0_header != NULL) free(hfs0_header);
	
	uiDeinit();
	
    gfxExit();
	
    return ret;
}

# gcdumptool
Nintendo Switch Game Card Dump Tool

Main features
--------------

* Generates full cartridge image dumps (XCI) with optional certificate removal and optional trimming.
* Generates installable packages (NSP) from cartridge applications.
    - You'll need to retrieve the full NCA keyset beforehand, using Lockpick. It must be stored in "sdmc:/switch/prod.keys".
* Supports multigame carts.
* CRC32 checksum calculation for XCI/NSP dumps.
* Full XCI dump verification using XML database from NSWDB.COM (NSWreleases.xml).
* XML database and in-app update via libcurl.
* Precise HFS0 raw partition dumping, using the root HFS0 header from the game card.
* Partition filesystem data dumping.
* Partition filesystem browser with manual file dump support.
* Manual game card certificate dump.
* Free SD card space checks in place.
* File splitting support for all operations.
* Game card metadata retrieval using NCM and NS services.
* Dump speed, ETA calculation and progress bar.

Thanks to
--------------

* MCMrARM, for creating the original application.
* RSDuck, for their vba-next-switch port. It's UI menu code was taken as a basis for this application.
* Foen, for giving me some pretty good hints about how to use the NCM service.
* Yellows8, for helping me fix a silly bug in my implementation of some NCM service IPC calls.
* SciresM, for hactool. It's AES cipher handling and external keys file parsing code is used during the NSP dump process.
* Björn Samuelsson, for his public domain CRC32 checksum calculation code for C (crc32_fast.c).
* AnalogMan, for his constant support and ideas.
* The folks from ReSwitched, for working towards the creation of a good homebrew ecosystem.

Changelog
--------------

**v1.0.8:**

* Added proper metadata reading from multigame carts.
* Added gamecard -> NSP dump option:
    - Compatible with file splitting (for FAT32 support). The same layout from splitNSP.py is used: a directory with numbered part files (00, 01, etc.). The archive bit is enabled right away in this directory to allow HOS to treat it as if it were a whole file. This way, it can be used with any application with NSP-handling capabilities.
    - Compatible with CRC32 checksum calculation. Disclaimer: NSP dumps can't be verified against the XML database.
    - Output NSPs contain a metadata XML file based on the information from the CNMT NCA for the application, which is decrypted using code from hactool. The necessary keyset is loaded from "sdmc:/switch/prod.keys", which can be generated using Lockpick.
    - If a multigame cart is used, you'll be able to choose which application to dump from the menu.
* Dump verification process tweaked for multigame carts: it'll now look for a possible checksum match using the Title IDs from all bundled applications.
* Improved error reporting in dumper.c when a write operation fails. Furthermore, if a write error is produced when trying to write data to an offset past the FAT32 file size limit (0xFFFFFFFF bytes), the application will suggest the user to enable the file splitting option.
* Tweaked part sizes for splitted dumps: XCI/raw partition/manual file dump part size now matches the one used by XCI-Cutter, while the NSP part size matches the one used by splitNSP.py.
* Minor fixes to the UI code.

**v1.0.7:**

* Fixed a segmentation fault when trying to free an invalid XML node data pointer when a Scene release from NSWReleases.xml with a matching Title ID misses data related to that node.
* Added a message suggesting the user to restart the application after a successful update.

**v1.0.6:**

* Updated application codebase in order to make it compatible with the latest devkitA64 and libnx releases.
* Removed some fs-srv service functions from fsext.c/h that have been included in libnx (and fixed the ones that haven't).
* Revamped the GFX code to replace the 8x8 ASCII font with the shared system font, using the pl service and FreeType.
* Enabled (and fixed) the in-app update option. HTTPS compatibility is achieved through the mbedtls portlib.
* Disabled screen dimming and auto sleep.
* Added file counter to partition browser.
* Changed the naming convention for split gamecard dumps to *.xc[part number], in order to make them compatible with SX OS and other tools right away.
* Increased the delay after inserting a new gamecard by 1 second.
* Added a gamecard detection thread to monitor gamecard state changes in a better way. This thread is hooked to a gamecard detection kernel handle retrieved through an IEventNotifier object.
* Replaced partition filesystem mounting through fs-srv service calls with manual HFS0 partition header parsing. This should fix issues when browsing the Logo partition from type 0x02 gamecards.
* Blocked HOME button presses when running as a regular/system application instead of an applet. A warning message will be displayed whenever any operation is started if the application is running as an applet.
* Added detection for bundled FW versions 6.0.0 - 8.0.0.

**v1.0.5:**

* Fixed game card version reading (now using the ncm service instead of retrieving it from the cached Control.nacp).
* Added ability to read and identify FW update versions bundled with game cards.
* In case an error occurs while reading the game card Title ID, the application will also display the FW version update bundled with it along with an explanation.
* Removed output XCI dump renaming based on the XML database from nswdb.com.
* Output naming scheme changed. Characters out of the ASCII range are replaced with underscores:
	- XCI dump: "sdmc:/[GameName] v[GameVersion] ([TitleID]).xci".
	- Raw partition dump: "sdmc:/[GameName] v[GameVersion] ([TitleID]) - Partition [PartitionIndex] ([PartitionName]).hfs0".
	- Partition data dump (directory): "sdmc:/[GameName] v[GameVersion] ([TitleID]) - Partition [PartitionIndex] ([PartitionName])/".
	- Certificate dump: "sdmc:/[GameName] v[GameVersion] ([TitleID]) - Certificate ([CRC32]).bin".
* Manual file dumps will now be saved to their corresponding directory instead of the SD card root.
* Added a XML database update option in the main menu.
* Added an update application option in the main menu. It isn't working at this moment because libcurl has problems dealing with secure connections, and as such the option has been disabled (pressing A on it does nothing). Nonetheless, the code to parse JSON responses from the GitHub API is pretty much ready, so it's just a matter of time.

**v1.0.4:**

* exFAT mode turned on by default.
* Replaced padding option with a trim output dump option (same as XCI-Cutter).
* Added dump speed and ETA calculation.
* Added XCI dump verification using XML database from nswdb.com (NSWreleases.xml). The file must be saved to the SD card root directory. Also, keep in mind that dump verification is only performed if you choose to create a full dump (with or without cert), not a trimmed one.
* Made CRC32 checksum calculation + XCI dump verification a configurable option.
* Output XCI dumps will get renamed to their corresponding Scene release if a match is found using the XML database from nswdb.com (e.g. "sdmc:/0100000000010000_20180625-234930.xci" -> "sdmc:/Super.Mario.Odyssey.NSW-BigBlueBox.xci").

**v1.0.3:**

* Made the 0xFF padding feature a configurable option.
* Added CRC32 checksum calculation for XCI dumps.

**v1.0.2:**

* Fixed a silly bug in the file splitting code.

**v1.0.1:**

* Minor UI fixes and tweaks.
* Added some missing Title ID checks in uiLoop().
* All calls to uiStatusMsg() are now properly identified.
* Increased wait time to 2 seconds when a new gamecard is detected.

**v1.0.0:**

Initial release.

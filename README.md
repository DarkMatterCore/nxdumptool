# gcdumptool
Nintendo Switch Game Card Dump Tool

Main features
--------------

* Generates full Cartridge Image dumps (XCI) with optional certificate removal and optional trimming.
* Generates installable Nintendo Submission Packages (NSP) from base applications, updates and DLCs stored in the inserted game card.
* Compatible with multigame carts.
* CRC32 checksum calculation for XCI/NSP dumps.
* Full XCI dump verification using XML database from NSWDB.COM (NSWreleases.xml).
* XML database and in-app update via libcurl.
* Precise HFS0 raw partition dumping, using the root HFS0 header from the game card.
* HFS0 partition file data dumping.
* HFS0 partition file browser with manual file dump support.
* RomFS section file data dumping.
* RomFS section file browser with manual file dump support.
* Manual game card certificate dump.
* Free SD card space checks in place.
* File splitting support for all operations.
* Game card metadata retrieval using NCM and NS services.
* Dump speed calculation, ETA calculation and progress bar.

Thanks to
--------------

* MCMrARM, for creating the original application.
* RSDuck, for vba-next-switch port. It's UI menu code was taken as a basis for this application.
* Foen, for giving me some pretty good hints about how to use the NCM service.
* Yellows8, for helping me fix a silly bug in my implementation of some NCM service IPC calls.
* SciresM, for hactool. It's NCA content handling procedure is reproduced during the NSP dump process.
* The-4n, for 4NXCI and hacPack. The NCA content patching procedure used in 4NXCI is replicated in the application, as well as the NACP XML generation from hacPack.
* shchmue, for Lockpick. It was used as a reference for the key-collection algorithm needed for the NSP dump and RomFS dump/browse procedures.
* Björn Samuelsson, for his public domain CRC32 checksum calculation code for C (crc32_fast.c).
* AnalogMan, for his constant support and ideas.
* RattletraPM, for the awesome icon used in the application.
* The GNOME project, from which the high contrast directory/file icons for the filebrowser modes were retrieved.
* The folks from ReSwitched, for working towards the creation of a good homebrew ecosystem.

Changelog
--------------

**v1.1.0:**
* Replaced the application icon with a new, stylish one made by RattletraPM. Thanks a lot!
* Gamecard base application icons are now retrieved and displayed in the menu.
* L/ZL/R/ZR buttons can now be used to change the displayed base application info if a multigame cart is inserted, instead of displaying everything right away.
* The Nintendo Extension shared font is now used to display bitmaps representing controller buttons and sticks instead of just using text to reference them.
* Replaced the mbedtls-based AES and SHA-256 implementations with functions from the hardware accelerated cryptography API from libnx.
* Added an option to generate split XCI dumps using a directory with the archive bit set, just like split NSP dumps. It will only appear if "Split output dump" is enabled.
* Fixed ETA calculation.
* Enabled ETA calculation in full HFS0 partition data dumps.
* Fixed CRC32 checksum calculation for gamecard certificate dumps.
* Added Program NCA RomFS section parser:
    - Supports filesystem dumping, filesystem browsing, manual file dumping and file splitting. Enjoy datamining your gamecards!
    - Compatible with multigame carts. You'll be able to choose which base application RomFS will be dumped/browsed from a submenu.
    - Output files will be saved to: "sdmc:/[GameName] v[GameVersion] ([TitleID]) (RomFS)/".
* Added high contrast directory/file icons from GNOME project to file browsing modes (HFS0 / RomFS).
* Fixed the NSP generation code (based on 4NXCI / hacPack):
    - Delta Fragment NCAs are now discarded.
    - The SHA-256 checksum is recalculated for every NCA content after being modified, resulting in new NCA IDs.
    - The ACID public key is replaced in the NPDM section from the Program NCA. All the related NCA/PFS0 Superblock SHA-256 hashes are recalculated.
        - The NPDM signature in the Program NCA header is now replaced as well.
    - The content records from the Application CNMT are updated with proper SHA-256 hashes and new NCA IDs. All the related NCA/PFS0 Superblock hashes are recalculated.
    - NACP XMLs are now generated as well.
    - Because of all these changes, the CRC32 checksum can't be calculated until the dump procedure is complete.
        - If this option is enabled, the application will take extra time after the NSP dump has been completed to calculate the CRC32 checksum. Nonetheless, you'll be able to cancel this procedure.
        - A warning message will appear in the NSP dump menu if CRC32 checksum calculation is enabled to inform the user about this extra step.
        - Furthermore, the output CRC32 checksum will be different on each new dump. This is because the NPDM signature in the Program NCA header uses a random seed.
    - This effectively makes the generated NSPs only need ES patches to work. ACID patches shouldn't be needed anymore.
* Added NSP dumping support for Patch and AddOnContent title types with gamecards that include bundled Updates/DLCs:
    - The information displayed in the main menu now shows how many Updates/DLCs are bundled in the inserted gamecard (per application and in total).
    - If a bundled gamecard update features a populated Rights ID bitfield, both its Ticket and Certificate will get added to the output NSP.
    - Additionally, the NSP dump menu has been divided in three subcategories: base application, update and DLC.
        - Each submenu will only appear if the inserted gamecard holds at least one title belonging to the category it represents.
        - If only the base application is included, like most gamecards, choosing the NSP dump option in the main menu will take you right to the base application dump menu.
        - Once you enter a submenu, you'll be able to choose exactly which title to dump belonging to that category.
    - Output update NSPs will not be modified in any way. Thus, unlike NSPs from base applications and DLCs, their CRC32 checksums will always be the same.
* Fixed the minimum system version field size in the extended CNMT header struct. Thanks to @0Liam !
* Changed the naming convention for output NSP dumps:
	- Base application: "sdmc:/[GameName] v[GameVersion] ([TitleID]) (BASE).nsp".
	- Update: "sdmc:/[GameName] v[UpdateVersion] ([UpdateTitleID]) (UPD).nsp".
        - If a matching base application isn't found: "sdmc:/[UpdateTitleID] v[UpdateVersion] (UPD).nsp".
	- DLC: "sdmc:/[GameName] v[DLCVersion] ([DLCTitleID]) (DLC).nsp".
        - If a matching base application isn't found: "sdmc:/[DLCTitleID] v[DLCVersion] (DLC).nsp".
* The application is now able to retrieve the NCA header key and perform NCA key area decryption at runtime, using the SPL services. Thus, is isn't needed to run Lockpick beforehand anymore to dump NSPs (nor to dump/browse RomFS data).
* If the inserted gamecard includes a bundled update, its version number will now be used in the output filename for XCI, HFS0 and gamecard certificate dumps.
* Minor improvements to the file splitting code.
    - Additionally, the filename for the current part will now be displayed and updated for all operations if file splitting is enabled.
* The application update feature will now use the launch path from argv if it's available. Otherwise, it defaults to "sdmc:/switch/gcdumptool.nro".
* Cosmetic fixes to the UI layout.
* NCM service resources are now properly closed.
* Removed unnecessary service (de)initializations.

Big thanks to PatrickD85, unvaluablespace, wartutor and Slim45 for testing these changes!

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

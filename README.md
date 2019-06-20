# nxdumptool
Nintendo Switch Dump Tool

Main features
--------------

* Generates full Cartridge Image dumps (XCI) with optional certificate removal and optional trimming.
* Generates installable Nintendo Submission Packages (NSP) from base applications, updates and DLCs stored in the inserted game card, SD card and eMMC storage devices.
* Compatible with multigame carts.
* CRC32 checksum calculation for XCI/NSP dumps.
* Full XCI dump verification using XML database from NSWDB.COM (NSWreleases.xml).
* XML database and in-app update via libcurl.
* Precise HFS0 raw partition dumping, using the root HFS0 header from the game card.
* HFS0 partition file data dumping.
* HFS0 partition file browser with manual file dump support.
* Program NCA ExeFS section file data dumping.
* Program NCA ExeFS section file browser with manual file dump support.
* Program NCA RomFS section file data dumping.
* Program NCA RomFS section file browser with manual file dump support.
* Manual game card certificate dump.
* Free SD card space checks in place.
* File splitting support for all operations.
* Game card metadata retrieval using NCM and NS services.
* Dump speed calculation, ETA calculation and progress bar.

Operations related to installed SD/eMMC titles require a keys file located at "sdmc:/switch/prod.keys". Use [Lockpick_RCM](https://github.com/shchmue/Lockpick_RCM) to generate it.

Thanks to
--------------

* [MCMrARM](https://github.com/MCMrARM), for creating the original application.
* [RSDuck](https://github.com/RSDuck), for vba-next-switch port. It's UI menu code was taken as a basis for this application.
* [foen](https://github.com/foen), for giving me some pretty good hints about how to use the NCM service.
* [yellows8](https://github.com/yellows8), for helping me fix a silly bug in my implementation of some NCM service IPC calls.
* [SciresM](https://github.com/SciresM), for [hactool](https://github.com/SciresM/hactool) (licensed under [ISC](https://github.com/SciresM/hactool/blob/master/LICENSE)). It's NCA content handling procedure is reproduced during the NSP dump process.
* [The-4n](https://github.com/The-4n), for [4NXCI](https://github.com/The-4n/4NXCI) (licensed under [ISC](https://github.com/The-4n/4NXCI/blob/master/LICENSE)) and [hacPack](https://github.com/The-4n/hacPack) (licensed under [GPLv2](https://github.com/The-4n/hacPack/blob/master/LICENSE)). The NCA content patching procedure used in 4NXCI is replicated in the application, as well as the NACP XML generation from hacPack.
* [shchmue](https://github.com/shchmue), for [Lockpick](https://github.com/shchmue/Lockpick) (licensed under [GPLv2](https://github.com/shchmue/Lockpick/blob/master/LICENSE)). It is used as a reference for the runtime key-collection algorithm needed for the NSP dump, ExeFS dump/browse and RomFS dump/browse procedures.
* Björn Samuelsson, for his [public domain CRC32 checksum calculation C-code](http://home.thep.lu.se/~bjorn/crc).
* [Adubbz](https://github.com/Adubbz), for [Tinfoil](https://github.com/Adubbz/Tinfoil) (licensed under [MIT](https://github.com/Adubbz/Tinfoil/blob/master/LICENSE)). Its wrappers for ES service IPC calls are used in the application.
* ChaN, for the [FatFs module](http://elm-chan.org/fsw/ff/00index_e.html) (licensed under [FatFs license](http://elm-chan.org/fsw/ff/doc/appnote.html#license)). It is used to read ES savedata files from the BIS System partition.
* [AnalogMan](https://github.com/AnalogMan151), for his constant support and ideas.
* [RattletraPM](https://github.com/RattletraPM), for the awesome icon used in the application.
* The GNOME project, from which the high contrast directory/file icons for the filebrowser modes were retrieved.
* The folks from ReSwitched, for working towards the creation of a good homebrew ecosystem.
* The Comfy Boyes, for being both awesome and supportive. You know who you are.

Donate
--------------

If you like my work and you'd like to support me in any way, it's not necessary, but a donation would be greatly appreciated!

[![Donate](https://img.shields.io/static/v1.svg?label=PayPal&message=Donate&color=blue&style=flat&logo=paypal)](https://paypal.me/DarkMatterCore)

Changelog
--------------

**v1.1.2:**
* Delta fragment NCAs are now included in update NSPs dumped from SD/eMMC if the "Generate ticket-less dump" option is disabled.
* It is now possible to generate ticket-less NSP dumps from bundled updates in gamecards. Please bear in mind that this option requires the external "sdmc:/switch/prod.keys" file.
* UI tweaks:
    * The application now keeps track of the selected title in SD/eMMC and "orphan" content modes when entering a menu and then going back to the list.
    * After selecting a title in the SD/eMMC menu, information about content already dumped related to the selected title will now be displayed (BASE / UPD / DLC).
        * Likewise, after selecting a title in the "orphan" title list (Y button), an additional line will now display if the selected title has been dumped or not.
        * This also informs the user if the dumps contain console-specific data.
    * Three additional entries will now be displayed in the "orphan" title list.
    * Upwards and downwards arrows will now be displayed for lists that exceed the max element count.
        * Because of this change, max element count for the SD/eMMC title list had to be reduced from 4 to 3.
    * Leftwards and rightwards arrowheads are now displayed in menus with options.
    * A "hint" message is now displayed in the "orphan" content mode to let the user know they'll be able to find gamecard updates in that section.
* If a file has been already dumped, the application will display a prompt asking the user if they want to proceed anyway or not. This doesn't apply to full HFS0/ExeFS/RomFS data dumps.
* It is now possible to jump from the first list element to the last one and viceversa using the D-Pad Up/Down and Left Stick Up/Down. The Right Stick is still used exclusively for fast scrolling and won't be affected by this change.
* Fixed a bug where NSP/ExeFS/RomFS dumping would fail if the written entry count returned by `ncmContentMetaDatabaseListApplication()` didn't match the total entry count for the selected NSP dump type.
* Fixed a bug where NSP/ExeFS/RomFS dumping would fail if an invalid title index was used with `ncmContentMetaDatabaseGet()`.

Thanks to [Maschell](https://github.com/Maschell), [DuIslingr](https://github.com/DuIslingr) and MUXI from PSXTools forums for reporting these bugs and providing with testing!

**v1.1.1:**
* Project name changed to `nxdumptool`. This is no longer a gamecard-only tool.
* Added ExeFS dumping/browsing support. This feature, along with the already available RomFS options, makes the application an excellent tool for modders!
* Added compatibility with FS process memory layout in the key retrieval procedure while using emuMMC. Thanks to [shchmue](https://github.com/shchmue)!
* Due to public demand, NSP dumping, ExeFS dumping/browsing and RomFS dumping/browsing support has been added for base applications, updates and DLCs available in both SD card and eMMC!
    * Now it's possible to select the source storage device (gamecard, SD card / eMMC) for any operation right after launching the application.
        * The gamecard submenu works exactly like the main menu has worked up to this point (except for the update options, which are now displayed in the new main menu).
        * The SD card / eMMC submenu shows installed base applications along with their icons. Upon selecting a title, a submenu with NSP and RomFS options will show up. It's also possible to dump updates/DLCs for an installed base application this way.
        * If there's installed content (updates/DLCs) with missing base application titles, pressing Y on the SD card / eMMC submenu will display this "orphan" content list and let you dump titles from it nonetheless.
    * It is possible to generate console-specific NSP dumps, dumps with modified tickets to remove console-specific data, and ticket-less dumps with standard NCA key area crypto.
        * Two new options are available in the NSP dump submenus for SD/eMMC titles: "Remove console specific data" and "Generate ticket-less dump". The latter won't appear if the former isn't enabled.
            * "Remove console specific data" cleans console specific data fields from a "personalized" ticket and replaces its RSA titlekey block with a 16-byte encrypted titlekey, essentially converting it to a "common" ticket. This option has no effect if the title already uses a "common" ticket.
            * "Generate ticket-less dump" goes another step ahead by cleaning up the Rights ID field in every NCA content file that includes it, stores the decrypted titlekey in the NCA key area and then encrypts this area using standard crypto, removing the need for a tik/cert combination.
        * Console-specific NSP dumps and dumps with modified tickets include both `tik` and `cert` files.
        * All NSP dumps generated from installed SD/eMMC titles include both `.cnmt.xml` and `.nacp.xml` files whenever possible.
    * Sadly, due to limitations in the methods currently used to perform key retrieval/derivation at runtime, NSP dumping, ExeFS dumping/browsing and RomFS dumping/browsing for SD/eMMC titles require the "sdmc:/switch/prod.keys" file. Specifically, these are the needed keys:
        * `eticket_rsa_kek`.
        * `titlekek_##` (varies from `00` to `1F`).
    * Additionally, ticket-less NSP dumps for SD/eMMC titles also require the following keys:
        * `key_area_key_application_##` (varies from `00` to `1F`).
        * `key_area_key_ocean_##` (varies from `00` to `1F`).
        * `key_area_key_system_##` (varies from `00` to `1F`).
    * All gamecard-related operations can still be performed without the need for a keys file!
* Output data generated by the application will now be saved to its corresponding subdirectory in "sdmc:/nxdumptool/":
    * XCI dumps: "sdmc:/nxdumptool/XCI/".
    * NSP dumps: "sdmc:/nxdumptool/NSP/".
    * HFS0 data: "sdmc:/nxdumptool/HFS0/".
    * ExeFS data: "sdmc:/nxdumptool/ExeFS/".
    * RomFS data: "sdmc:/nxdumptool/RomFS/".
    * Certificate dumps: "sdmc:/nxdumptool/Certificate/".
* The location for the NSWDB.COM XML database has been moved to "sdmc:/nxdumptool/NSWreleases.xml".
* Tickets from updates with titlekey crypto dumped from gamecards are now converted to regular "common" tickets before being written to the output NSP dump.
* The content distribution type for updates dumped from custom XCIs mounted through SX OS is now set to "download".
* Fixed a NCM service handle exhaustion bug if an error ocurred while reading the RomFS section entry from the Program NCA for any base application.
* Changed the application icon yet again. Big thanks to RattletraPM!
* Minor changes and other various general fixes.

Thanks to [simontime](https://github.com/simontime) for helping me out with the RSA certificate chain retrieval process! Also thanks to MUXI from PSXTools forums for providing with testing!

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
* Fixed the minimum system version field size in the extended CNMT header struct. Thanks to [0Liam](https://github.com/0Liam)!
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

# gcdumptool
Nintendo Switch Game Card Dump Tool

Main features
--------------

* Generates XCI cartridge dumps (with optional certificate removal and optional trimming).
* CRC32 checksum calculation for XCI dumps.
* Full XCI dump verification using XML database from nswdb.com (NSWreleases.xml).
* XML database update via libcurl.
* Precise HFS0 raw partition dumping (using the root HFS0 header from the game card).
* Partition filesystem data dumping.
* Partition filesystem browser (with manual file dump support).
* Manual game card certificate dump.
* Free SD card space checks in place.
* File splitting support for all operations, using 2 GiB parts.
* Game card Title ID and Control.nacp retrieval support using NCM and NS services.
* Dump speed and ETA calculation.

Thanks to
--------------

* MCMrARM, for creating the original application.
* RSDuck, for their vba-next-switch port. It's UI menu code was taken as a basis for this application.
* Foen, for giving me some pretty good hints about how to use the NCM service.
* Yellows8, for helping me fix a silly bug in my implementation of some NCM service IPC calls.
* Björn Samuelsson, for his public domain CRC32 checksum calculation code for C (crc32_fast.c).
* AnalogMan, for his constant support and ideas.
* The folks from ReSwitched, for working towards the creation of a good homebrew ecosystem.

Changelog
--------------

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

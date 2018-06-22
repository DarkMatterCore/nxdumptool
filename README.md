# gcdumptool
Nintendo Switch Game Card Dump Tool

Main features
--------------

* Generates full XCI cartridge dumps (with optional certificate removal). All dumps are padded with 0xFF to match the full game card size.
* Precise HFS0 raw partition dumping (using the root HFS0 header from the game card).
* Partition filesystem data dumping.
* Partition filesystem browser (with manual file dump support).
* Manual game card certificate dump.
* Free SD card space checks in place.
* File splitting support for all operations, using 2 GiB parts.
* Game card Title ID and Control.nacp retrieval support using NCM and NS services.

Thanks to
--------------

* MCMrARM, for creating the original application.
* RSDuck, for their vba-next-switch port. It's UI menu code was taken as a basis for this application.
* Foen, for giving me some pretty good hints about how to use the NCM service.
* Yellows8, for helping me fix a silly bug in my implementation of some NCM service IPC calls.
* The folks from ReSwitched, for working towards the creation of a good homebrew ecosystem.

Changelog
--------------

**v1.0.2:**

* Fixed a silly bug in the file splitting code.

**v1.0.1:**

* Minor UI fixes and tweaks.
* Added some missing Title ID checks in uiLoop().
* All calls to uiStatusMsg() are now properly identified.
* Increased wait time to 2 seconds when a new gamecard is detected.

**v1.0.0:**

Initial release.

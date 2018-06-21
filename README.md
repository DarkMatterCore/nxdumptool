# gcdumptool
Nintendo Switch Game Card Dump Tool

Main features
--------------

* Generates full XCI cartridge dumps (with optional certificate removal). All dumps are padded with 0xFF up to the actual game card size.
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
* Foen, for giving me some pretty good hints about how to use the NCM service.
* Yellows8, for helping me fix a silly bug in my implementation of some NCM service IPC calls.
* The folks from ReSwitched, for working towards the creation of a good homebrew ecosystem.

# nxdumptool
<img width="200" src="romfs/icon/nxdumptool.jpg">
Nintendo Switch Dump Tool

### Official Discord server: https://discord.gg/SCbbcQx

This branch is used for the development of the ongoing nxdumptool rewrite. Code is highly experimental and lacks a proper UI at this time, but it has been thoroughly tested and should be safe to use. Proof-of-concept (PoC) builds are provided to test different aspects of the codebase.

If a nxdumptool build from the [releases page](https://github.com/DarkMatterCore/nxdumptool/releases) isn't working for whatever you're trying to do (e.g. RomFS dumping), odds are it will work with a PoC build from this particular branch. The [old branch](https://github.com/DarkMatterCore/nxdumptool/tree/main) will not receive any further updates.

A [GitHub workflow](https://github.com/DarkMatterCore/nxdumptool/actions) is used to automatically build each new commit -- feel free to check it out. Furthermore, the assets from the [rewrite-prerelease](https://github.com/DarkMatterCore/nxdumptool/releases/tag/rewrite-prerelease) tag are automatically updated by this workflow, so you can always find the latest NRO binary there.

This readme will be updated whenever the rewrite branch is ready for a proper release.

Currently planned changes for this branch include:

* [USB ABI](https://github.com/DarkMatterCore/nxdumptool/tree/rewrite/host) (dump data directly to a PC). :white_check_mark:
* USB Mass Storage device support (FAT, NTFS, EXT) via [libusbhsfs](https://github.com/DarkMatterCore/libusbhsfs). :white_check_mark:
* [NX Card Image (XCI)](https://switchbrew.org/wiki/XCI) gamecard dumps, with optional KeyArea prepending and certificate removal. :white_check_mark:
* [Gamecard header](https://switchbrew.org/wiki/XCI#CardHeader) dumps. :white_check_mark:
* [Gamecard certificate](https://switchbrew.org/wiki/XCI#CertArea) dumps. :white_check_mark:
* Plaintext [gamecard CardInfo area](https://switchbrew.org/wiki/XCI#CardHeaderEncryptedData) dumps. :white_check_mark:
* [Gamecard InitialData](https://switchbrew.org/wiki/XCI#InitialData) area dumps. :white_check_mark:
* [Gamecard CardIdSet](https://switchbrew.org/wiki/Filesystem_services#GameCardIdSet) dumps. :white_check_mark:
* [Gamecard Hash FS partition](https://switchbrew.org/wiki/XCI#PartitionFs) dumps (in both extracted and raw inage forms). :white_check_mark:
* [Lotus ASIC firmware (LAFW) blob](https://switchbrew.org/wiki/Lotus3#User_firmware) dumping from RAM. :white_check_mark:
* Properly detect if an inserted gamecard requires a LAFW update. :white_check_mark:
* Nintendo Submission Package (NSP) dumps for both digital and gamecard-based titles.
* Individual [Ticket](https://switchbrew.org/wiki/Ticket) dumps from digital titles, with support for temporary/volatile tickets with AES-128-CTR crypto data kept in RAM.
* Individual [Nintendo Content Archive (NCA)](https://switchbrew.org/wiki/NCA) dumps from a specific title. :white_check_mark:
* Individual NCA filesystem section ([Partition FS / ExeFS](https://switchbrew.org/wiki/NCA#PFS0), RomFS, Patch RomFS) dumps from a specific NCA belonging to a specific title, in both extracted and raw image forms. :white_check_mark:
* Better support for NCA BucketTree storages (Indirect, AesCtrEx, Sparse, Compressed), as well as better handling of multi-layered reads in combination with game updates. :white_check_mark:
* System title support (e.g. dump qlaunch, shared fonts and much, much more at runtime). :white_check_mark:
* Better memory handling while dealing with compressed NRO binaries. :white_check_mark:
* Event-driven background threads to manage gamecard insertions/ejections, gamecard application metadata parsing (e.g. the game would be properly identified even if its gamecard has never been used on your console), USB ABI sessions and USB Mass Storage devices. :white_check_mark:
* Improved support for multigame gamecards and titles with more than one Program NCA (e.g. SM3DAS). :white_check_mark:
* Control.nacp patching while dumping NSPs (lets you patch screenshot, video, user account and HDCP restrictions). :white_check_mark:
* Partition FS / Hash FS / RomFS browser using custom devoptab wrappers. :white_check_mark:
* Full system update dumps. :x:
* Batch NSP dumps. :x:
* `FsFileSystem` + `FatFs` based eMMC browser using a custom devoptab wrapper (allows copying files protected by the FS sysmodule at runtime). :x:
* New UI using a [customized borealis fork](https://github.com/DarkMatterCore/borealis/tree/nxdumptool-legacy). :warning:

Legend:

* :white_check_mark: - Implemented
* :warning: - Partially implemented
* :x: - Not implemented

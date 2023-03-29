# nxdumptool
<img width="200" src="romfs/icon/nxdumptool.jpg">
Nintendo Switch Dump Tool

Temporary branch used for the development of the nxdumptool rewrite. Highly experimental, use at your own risk.

If a nxdumptool build from the [releases page](https://github.com/DarkMatterCore/nxdumptool/releases) isn't working for whatever you're trying to do (e.g. RomFS dumping), odds are it will work with a experimental proof-of-concept build from this particular branch. The old branch will not receive any further updates.

A [GitHub workflow](https://github.com/DarkMatterCore/nxdumptool/actions) is used to automatically build each new commit -- feel free to check it out. For additional information, please reach us at our [Discord server](https://discord.gg/SCbbcQx).

This readme will be updated whenever the rewrite branch is ready for a proper release.

Currently planned changes for this branch include:

* USB ABI (dump data directly to a PC). :white_check_mark:
* USB Mass Storage device support (FAT, NTFS, EXT). :white_check_mark:
* Gamecard header dumps. :white_check_mark:
* Gamecard CardInfo area dumps. :white_check_mark:
* Gamecard InitialData area dumps. :white_check_mark:
* Individual NCA dumps (select and dump a specific NCA from a title). :white_check_mark:
* Individual NCA FS section dumps (select and dump a specific FS section from a specific NCA belonging to a title, in both extracted and raw image forms) (this includes patched RomFS images). :white_check_mark:
* System title support (e.g. dump qlaunch, shared fonts and much, much more at runtime). :white_check_mark:
* Volatile ticket dumping. :white_check_mark:
* Better memory handling while dealing with compressed NRO binaries. :white_check_mark:
* Event-driven background threads to manage gamecard insertions/ejections, gamecard application metadata parsing (e.g. the game would be properly identified even if it's gamecard has never been used on your console), USB ABI sessions and USB Mass Storage devices. :white_check_mark:
* Improved support for multigame gamecards and titles with more than one Program NCA (e.g. SM3DAS). :white_check_mark:
* Control.nacp patching while dumping NSPs (lets you patch screenshot, video, user account and HDCP restrictions). :white_check_mark:
* Full system update dumps. :x:
* New UI using a [customized borealis fork](https://github.com/DarkMatterCore/borealis/tree/nxdumptool-legacy). :warning:

Legend:

* :white_check_mark: - Implemented
* :warning: - Partially implemented
* :x: - Not implemented

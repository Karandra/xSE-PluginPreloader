*Dates in DD.MM.YYYY*

# Version 0.2.3, 07.10.2020
- Fixed warning message box about missing `DComp.dll` on Windows 7.

# Version 0.2.2, 06.10.2020
- Fixed bug with loading configuration from XML file (see: https://github.com/KerberX/KxFramework/commit/b563edc50e19587c1b9730bddb7465ad09be5ff9).
- Added logging of the system version and some other environment information.

# Version 0.2.1, 01.10.2020
- Better logging of configuration file loading.
- When the config file isn't found or can not be loaded it will be restored to the default one stored inside the DLL file.
- Many pointers and pointer-like values are logged in hex now (0x12abcdef).

# Version 0.2, 01.10.2020
- Rewritten using [KxFramework](https://github.com/KerberX/KxFramework).
- Added more logging and error reporting.
- Added process filter to exclude some processes from preloading plugins.
- Added back support for import address table (IAT) hooking method.
- Config file uses XML format instead of INI.

# Version 0.1.2, 02.10.2018
- Incompatibility with HRTF (or any other mod which uses `X3DAudio1_7.dll`) is resolved. For Fallout 4 'IpHlpAPI.dll' is now used.

# Version 0.1.1, 12.08.2018
- Added config option to specify load method. Default is delayed loading for MO.
- Changed delayed load method to second thread attach. Refer to `Source\DLLMain.cpp` for details.

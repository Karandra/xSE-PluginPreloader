*Dates in DD.MM.YYYY*

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

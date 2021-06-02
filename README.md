# rawpsx

PlayStation re-implementation of the engine used in the game Another World (Out of This World),
based on [rawgl](https://github.com/cyxx/rawgl),
rewritten in C using [PSn00bSDK](https://github.com/Lameguy64/PSn00bSDK).
At the moment rawpsx only supports running the full DOS version of the game,
but eventually I will probably add support for the Amiga version and the DOS demo as well.

The `sw` branch renders the graphics in software mode and uploads the resulting page to VRAM, and
is the primary branch since it looks like the entire game works fine this way, with little to no
performance issues.

The `hw` branch renders everything using the GPU, but has palette swapping issues and is outdated.

## Running pre-built releases

1. Obtain the latest GitHub release, if there are any.
2. Put the data files from the DOS version of Another World/Out of This World into the `data` folder.
   You only need the files `BANKxx` and `MEMLIST.BIN`.
3. [Get mkpsxiso](https://github.com/Lameguy64/mkpsxiso/releases/latest) and ensure it is in `PATH`
   or in the same folder as `rawpsx.exe`.
4. Run `makeiso.bat` or `makeiso.sh`. This will produce `rawpsx.iso`.
5. Write the ISO image to a CD-R and play it on your PlayStation
   using a modchip or some sort of other protection bypass.

## Building

1. [Set up PSn00bSDK](https://github.com/Lameguy64/PSn00bSDK#obtaining-psn00bsdk) and ensure it is in `PATH`.
2. [Get mkpsxiso](https://github.com/Lameguy64/mkpsxiso/releases/latest) and ensure it is in `PATH`.
   You can put it into the `tools/bin` folder of your PSn00bSDK install.
2. Put the data files from the DOS version of Another World/Out of This World into the `data` folder.
   You only need the files `BANKxx` and `MEMLIST.BIN`.
3. Run `make iso`. This will produce `rawpsx.iso` and `rawpsx.exe`.
4. Write the ISO image to a CD-R and play it on your PlayStation using a modchip
   or some sort of other protection bypass.

## Credits
* Lameguy64 for PSn00bSDK;
* cyxx for raw/rawgl;
* Giuseppe Gatta (?) for PSXSDK;
* Fabien Sanglard for his Another World article series.

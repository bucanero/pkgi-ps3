# pkgi

[![Travis CI Build Status][img_travis]][pkgi_travis] [![Downloads][img_downloads]][pkgi_downloads] [![Release][img_latest]][pkgi_latest] [![License][img_license]][pkgi_license]

pkgi allows to install original pkg files on your Vita.

This homebrew allows to download & unpack pkg file directly on Vita together with your [NoNpDrm][] fake license.

# Features

* **easy** way to see list of available downloads, including searching, filter & sorting.
* **standalone**, no PC required, everything happens directly on Vita.
* **automatic** download and unpack, just choose an item, and it will be installed, including bubble in live area.
* **resumes** interrupted download, you can stop download at any time, switch applications, and come back to download
  from place it stopped.

Current limitations:
* **no support for DLC or PSM**.
* **no queuing** up multiple downloads.
* **no background downloads** - if application is closed or Vita is put in sleep then download will stop.

# Download

Get latest version as [vpk file here][pkgi_latest].

# Setup instructions

You need to create `ux0:pkgi/pkgi.txt` file that will contain items available for installation. This file is in very
simple CSV format where each line means one item in the list:

    contentid,flags,name,name2,zrif,url,size,checksum

where:

* *contentid* is full content id of item, for example: `UP2120-PCSE00747_00-TOWERFALLVITA000`.
* *flags* is currently unused number, set it to 0.
* *name* is arbitrary UTF-8 string to display for name.
* *name2* is currently unused alternative name, leave it empty.
* *zrif* is NoNpDrm created fake license in zRIF format, it must match contentid.
* *url* is http url where to download PKG, pkg content id must match the specified contentid.
* *size* is size of pkg in bytes, or 0 if not known.
* *checksum* is sha256 digest of pkg as 32 hex bytes to make sure pkg is not tampered with. Leave empty to skip the check.

Name cannot contain newlines or commas.

To avoid downloading pkg file over network, you can place it in `ux0:pkgi` folder. Keep the name of file same as in http url,
or rename it with same name as contentid. pkgi will first check if pkg file can be read locally, and only if it is missing
then pkgi will download it from http url.

# Usage

Using application is pretty straight forward. Select item you want to install and press X. To sort/filter/search press triangle.
It will open context menu. Press triangle again to confirm choice(s) you make in menu. Or press O to cancel any changes you did.

Press left or right shoulder button to move page up or down.

# Q&A

1. Where to get zRIF string? 

  You must use [NoNpDrm][] plugin to dump existing games you have. Plugin will generate rif file with fake license.
  Then you can use either [web page][zrif_online_converter] or [make_key][pkg_dec] to convert rif file to zRIF string.

2. Where to get pkg URL?

  You can use [PSDLE][] to find pkg URL for games you own. Then either use original URL, or host the file on your own server.

3. Where to remove interrupted/failed downloads to free up the space?

  In `ux0:pkgi` folder - each download will be in separate folder by its title id. Simply delete the folder & resume file.

4. Download speed is too slow!

  Typically you should see speeds ~1-2 MB/s. This is normal for Vita hardware. Of course it also depends on WiFi router you
  have and WiFi signal strength. But sometimes speed will drop down to only few hundred KB/s. This happens for pkg files that
  contains many small files or many folders. Creating a new file or a new folder takes extra time which slows down the download.

# Building

You need to have [Vita SDK][vitasdk] with [libvita2d][] installed.

Run `cmake .` to create debug build, or `cmake -DCMAKE_BUILD_TYPE=Release .` to create optimized release build.

After than run `make` to create vpk file. You can set environment variable `PSVITAIP` (before running cmake) to IP address of
Vita, that will allow to use `make send` for sending eboot.bin file directly to `ux0:app/PKGI00000` folder.

To enable debugging logging pass `-DPKGI_ENABLE_LOGGING=ON` argument to cmake. Then application will send debug messages to
UDP multicast address 239.255.0.100:30000. To receive them you can use [socat][] on your PC:

    $ socat udp4-recv:30000,ip-add-membership=239.255.0.100:0.0.0.0 -

For easer debugging on Windows you can build pkgi in "simulator" mode - use Visual Studio 2017 solution from simulator folder.

# License

This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or distribute this software, either in source code form or as a
compiled binary, for any purpose, commercial or non-commercial, and by any means.

puff.h and puff.c files are under [zlib][] license.

[NoNpDrm]: https://github.com/TheOfficialFloW/NoNpDrm
[zrif_online_converter]: https://rawgit.com/mmozeiko/pkg2zip/online/zrif.html
[pkg_dec]: https://github.com/weaknespase/PkgDecrypt
[pkg_releases]: https://github.com/mmozeiko/pkgi/releases
[vitasdk]: https://vitasdk.org/
[libvita2d]: https://github.com/xerpi/libvita2d
[PSDLE]: https://repod.github.io/psdle/
[socat]: http://www.dest-unreach.org/socat/
[zlib]: https://www.zlib.net/zlib_license.html
[pkgi_travis]: https://travis-ci.org/mmozeiko/pkgi/
[pkgi_downloads]: https://github.com/mmozeiko/pkgi/releases
[pkgi_latest]: https://github.com/mmozeiko/pkgi/releases/latest
[pkgi_license]: https://github.com/mmozeiko/pkgi/blob/master/LICENSE
[img_travis]: https://api.travis-ci.org/mmozeiko/pkgi.svg?branch=master
[img_downloads]: https://img.shields.io/github/downloads/mmozeiko/pkgi/total.svg?maxAge=3600
[img_latest]: https://img.shields.io/github/release/mmozeiko/pkgi.svg?maxAge=3600
[img_license]: https://img.shields.io/github/license/mmozeiko/pkgi.svg?maxAge=2592000

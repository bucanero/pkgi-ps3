# PKGi PS3

[![Downloads][img_downloads]][pkgi_downloads] [![Release][img_latest]][pkgi_latest] [![License][img_license]][pkgi_license]
[![Build package](https://github.com/bucanero/pkgi-ps3/actions/workflows/build.yml/badge.svg)](https://github.com/bucanero/pkgi-ps3/actions/workflows/build.yml)

**PKGi PS3** is a PlayStation 3 port of [pkgi (PSVita)](https://github.com/mmozeiko/pkgi).

The `pkgi-ps3` homebrew app allows to download and install `.pkg` files directly on your PS3.

![image](https://user-images.githubusercontent.com/1153055/71187586-1acaf400-225e-11ea-9531-b18af20be10d.png)

**Comments, ideas, suggestions?** You can contact [me](https://github.com/bucanero/) on [Twitter](https://twitter.com/dparrino) and on [my website](http://www.bucanero.com.ar/).

# Features

* **easy to use:** list available downloads, including searching, filtering, and sorting.
* **standalone:** no PC required, everything happens directly on the PS3.
* **automatic downloads:** just choose an item, and it will be downloaded by the app to your HDD (`direct mode`) or queued for background download (`background mode`) using the internal Download Manager.
* **resumes interrupted downloads:** you can stop a download at any time, switch applications, and come back to resume the download later.
* **content activation:** the app can generate `.rif` files for downloaded content (system must be activated)
* **content updates:** the app can check online for available content updates
* **localization support:** Finnish, French, German, Italian, Polish, Portuguese, Spanish, Turkish

### Notes:
* **queuing** up multiple downloads is only supported when using `background download` mode.
* **background download tasks** will only show up after rebooting your PS3.

# Download

Get the [latest version here][pkgi_latest].

### Changelog

See the [latest changes here](https://github.com/bucanero/pkgi-ps3/blob/master/CHANGELOG.md).

# Setup instructions

You need to create a [`pkgi.txt`](#sample-db-file) file in `/dev_hdd0/game/NP00PKGI3/USRDIR` that contains the items available for installation.
The text database format is user customizable. Check [this section](#user-defined-db-format) to learn how to define your own custom DB format.

## Multiple databases

You can also load additional database files:

- `pkgi_games.txt`
- `pkgi_dlcs.txt`
- `pkgi_themes.txt`
- `pkgi_avatars.txt`
- `pkgi_demos.txt`
- `pkgi_updates.txt`
- `pkgi_emulators.txt`
- `pkgi_apps.txt`
- `pkgi_tools.txt`

Items on each of these files will be auto-categorized to the file content type. **Note:** The app assumes that every database file has the same format, as defined in `dbformat.txt`.

## Online DB update

You can refresh and sync an online database by adding the DB URL(s) to the `config.txt` file in `/dev_hdd0/game/NP00PKGI3/USRDIR`. 

For example:

```
url http://www.mysite.com/mylist.csv
url_demos http://www.demos.com/otherlist.csv
url_emulators http://www.example.com/emulators.csv
```

Using this setup, `pkgi.txt` will be updated with `mylist.csv`, `pgi_demos.txt` with `otherlist.csv` , and `pkgi_emulators.txt` with `emulators.csv`.

Next time you open the app, you'll have an additional menu option ![Triangle](https://github.com/bucanero/pkgi-ps3/raw/master/data/TRIANGLE.png) called **Refresh**. When you select it, the local databases will be syncronized with the defined URLs.

# DB formats

The application needs a text database that contains the items available for installation, and it must follow the [default format definition](#default-db-format), or have a [custom format definition](#user-defined-db-format) file.

## Default DB format

The default database file format uses a very simple CSV format where each line means one item in the list:

```
contentid,type,name,description,rap,url,size,checksum
```

where:

| Column | Description |
|--------|-------------|
| `contentid` | is the full content id of the item, for example: `UP0000-NPXX99999_00-0000112223333000`.
| `type` | is a number for the item's content type. See the [table below](#content-types) for details. (set it to 0 if unknown)
| `name` | is a string for the item's name.
| `description` | is a string for the item's description.
| `rap` | the 16 hex bytes for a RAP file, if needed by the item (`.rap` files will be created on `/dev_hdd0/exdata`). Leave empty to skip the `.rap` file.
| `url` | is the HTTP/HTTPS/FTP/FTPS URL where to download the `.pkg` file.
| `size` | is the size in bytes of the `.pkg` file, or 0 if unknown.
| `checksum` | is a SHA256 digest of the `.pkg` file (as 32 hex bytes) to make sure the file is not tampered with. Leave empty to skip the check.

**Note:** `name` and `description` cannot contain newlines or commas.

### Sample DB file

An example `pkgi.txt` file following the `contentid,type,name,description,rap,url,size,checksum` format:

```
EP0001-FILEMANAG_00-0000000000000000,8,FileManager v1.40,File Manager,,http://github.com/Zarh/ManaGunZ/releases/download/1.40/FileManager_v1.40.pkg,12171120,FAF680636B18AD0B70AA61F48A78C5E42D6972F795F1B82CC434BE3DDE60F00F
UP0001-IRISMAN00_00-VER4880000000000,8,IRISMAN 4.88.1,Backup Manager,,http://github.com/aldostools/IRISMAN/releases/download/4.88/IRISMAN_4.88.pkg,29411984,E6EF607F0002B31BFB148BE4FC9BDBACB4E53110751F0E667C701D40B5290570
EP0001-MANAGUNZ0_00-0000000000000000,8,ManaGunZ v1.40,Backup Manager,,http://github.com/Zarh/ManaGunZ/releases/download/1.40/ManaGunZ_v1.40.pkg,17563040,CE0E4036903E881C08259FD69E777F6BC9CD24E823B471A7B15C88FDDBB2E330
UP0001-PS3SFM001_00-0000000000000000,8,Simple file manager v0.5.2,File Manager,,http://github.com/lmirel/fm_psx/releases/download/v0.5.2/sfm_ps3.pkg,1098800,301F64CC94E9BC442FDAC9199BFB8153AC2430A5E47331C6CF8A25B7881648A6
EP0001-UPDWEBMOD_00-0000000000000000,9,webMAN MOD v1.47.36,Backup Manager,,http://github.com/aldostools/webMAN-MOD/releases/download/1.47.36/webMAN_MOD_1.47.36_Installer.pkg,13580448,
```
### Content types

| Type value |	Content type | DB File |
|------------|--------------|---------|
| 0	| Unknown            |
| 1	| Game               | `pkgi_games.txt`
| 2	| DLC                | `pkgi_dlcs.txt`
| 3	| Theme              | `pkgi_themes.txt`
| 4	| Avatar             | `pkgi_avatars.txt`
| 5	| Demo               | `pkgi_demos.txt`
| 6	| Update             | `pkgi_updates.txt`
| 7	| Emulator           | `pkgi_emulators.txt`
| 8	| Application        | `pkgi_apps.txt`
| 9	| Tool               | `pkgi_tools.txt`

## User-defined DB format

To use a custom database format, you need to create a `dbformat.txt` file, and save it on `/dev_hdd0/game/NP00PKGI3/USRDIR`.

The `dbformat.txt` definition file is a 2-line text file:
* Line 1: the custom delimiter character (e.g.: `;`, `,`, `|`, etc.)
* Line 2: the column names for every column in the custom database, delimited by the proper delimiter defined in line 1

**Note:** For the columns to be properly recognized, use the column tag names defined in the table above.

All the columns are optional. Your database might have more (or less) columns, so any unrecognized column will be skipped.

### Example

Example `dbformat.txt`, for a database using semi-colon (`;`) as separator:

```
;
name;TITLE ID;REGION;description;AUTHOR;TYPE;url;rap;size
```

**Result:** only the `name,description,url,rap,size` fields will be used.

### Example

Example `dbformat.txt`, for a database using character pipe (`|`) as separator:

```
|
REGION|TITLE|name|url|rap|contentid|DATE|PKG FILENAME|size|checksum
```

**Result:** only the `name,url,rap,contentid,size,checksum` fields will be used.

# Usage

Using the application is simple and straight-forward: 

 - Move <kbd>UP</kbd>/<kbd>DOWN</kbd> to select the item you want to download, and press ![X button](https://github.com/bucanero/pkgi-ps3/raw/master/data/CROSS.png).
 - To see the item's details, press ![Square](https://github.com/bucanero/pkgi-ps3/raw/master/data/SQUARE.png).
 - To sort/filter/search press ![Triangle](https://github.com/bucanero/pkgi-ps3/raw/master/data/TRIANGLE.png).
It will open the context menu. Press ![Triangle](https://github.com/bucanero/pkgi-ps3/raw/master/data/TRIANGLE.png) again to confirm the new settings, or press ![O button](https://github.com/bucanero/pkgi-ps3/raw/master/data/CIRCLE.png) to cancel any changes.
- Press left or right trigger buttons <kbd>L1</kbd>/<kbd>R1</kbd> to move pages up or down.
- Press <kbd>L2</kbd>/<kbd>R2</kbd> trigger buttons to switch between categories.

### Notes

- **RAP data:** if the item has `.rap` data, the file will be saved in the `/dev_hdd0/exdata/` folder.


# Q&A

1. Where to get a `rap` string? 
   
   You can use a tool like RIF2RAP to generate a `.rap` from your existing `.rif` files. Then you can use a tool like `hexdump` to get the hex byte string.

2. Where to get `.pkg` links?
   
   You can use [PSDLE][] to find `.pkg` URLs for the games you own. Then either use the original URL, or host the file on your own web server.

3. Where to remove interrupted/failed downloads to free up disk space?
   
   Check the `/dev_hdd0/tmp/pkgi` folder - each download will be in a separate `.pkg` file by its title id. Simply delete the file and start again.

4. Download speed is too slow!
   
   Optimization is still pending. If `direct` download is slow, you can use `background download` mode to download files using the internal PS3 Download Manager.

# Credits

* [Bucanero](http://www.bucanero.com.ar/): Project developer
* [mmozeiko](https://github.com/mmozeiko/): [PS Vita pkgi](https://github.com/mmozeiko/pkgi)

# Building

You need to have installed:

- [PS3 toolchain](https://github.com/bucanero/ps3toolchain)
- [PSL1GHT](https://github.com/ps3dev/PSL1GHT) SDK
- [Tiny3D](https://github.com/wargio/Tiny3D) library
- [YA2D](https://github.com/bucanero/ya2d_ps3) library (an extended version from my repo)
- [PolarSSL](https://github.com/bucanero/ps3libraries/blob/master/scripts/015-polarssl-1.3.9.sh) library (v1.3.9)
- [libcurl](https://github.com/bucanero/ps3libraries/blob/master/scripts/016-libcurl-7.64.1.sh) library (v7.64.1)
- [MikMod](https://github.com/ps3dev/ps3libraries/blob/master/scripts/011-libmikmod-3.1.11.sh) library
- [Mini18n](https://github.com/bucanero/mini18n) library
- [dbglogger](https://github.com/bucanero/dbglogger) library (only required for debug logging)

Run `make` to create a release build. After that, run `make pkg` to create a `.pkg` install file. 

You can also set the environment variable `PS3LOAD=tcp:x.x.x.x` to the PS3's IP address;
that will allow you to use `make run` and send `pkgi-ps3.self` directly to the [PS3LoadX listener](https://github.com/bucanero/ps3loadx).

## Debugging

To enable debug logging, build PKGi PS3 with `make DEBUGLOG=1`. The application will send debug messages to
UDP multicast address `239.255.0.100:30000`. To receive them you can use [socat][] on your PC:

    $ socat udp4-recv:30000,ip-add-membership=239.255.0.100:0.0.0.0 -

# License

`pkgi-ps3` is released under the [MIT License](LICENSE).

[PSDLE]: https://repod.github.io/psdle/
[socat]: http://www.dest-unreach.org/socat/
[pkgi_downloads]: https://github.com/bucanero/pkgi-ps3/releases
[pkgi_latest]: https://github.com/bucanero/pkgi-ps3/releases/latest
[pkgi_license]: https://github.com/bucanero/pkgi-ps3/blob/master/LICENSE
[img_downloads]: https://img.shields.io/github/downloads/bucanero/pkgi-ps3/total.svg?maxAge=3600
[img_latest]: https://img.shields.io/github/release/bucanero/pkgi-ps3.svg?maxAge=3600
[img_license]: https://img.shields.io/github/license/bucanero/pkgi-ps3.svg?maxAge=2592000

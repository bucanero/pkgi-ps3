# Changelog

All notable changes to the `pkgi-ps3` project will be documented in this file. This project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]()

### Added

* Added TTF fonts to support Japanese characters
* Added SSL support (the app can download `https` links)

### Fixed

* Fixed UI issue where texts could go beyond the screen limits

## [v1.0.8](https://github.com/bucanero/pkgi-ps3/releases/tag/v1.0.8) - 2019-12-19

### Added

* Added analog pad support
* Added CPU/RSX temperature status
* Added "Details" screen
* Added automatic download after version update check 

### Fixed

* Improved empty `.pkg` file generation using async IO.
* Improved UI

## [v1.0.5](https://github.com/bucanero/pkgi-ps3/releases/tag/v1.0.5) - 2019-12-14

### Added

* Generic text database format support
* Credits `(SELECT)` and Exit `(START)` confirmation dialogs
* Changelog file

### Fixed

* The app now creates `/dev_hdd0/exdata`, if folder doesn't exists
* Fixed unresponsive `background download` dialog while creating a PKG file
* Fixed a bug when the URL was missing

## [v1.0.0](https://github.com/bucanero/pkgi-ps3/releases/tag/v1.0.0) - 2019-12-11

### Added

* Text search filtering using on-screen keyboard
* Background download task mode (uses internal Download Manager)
* New version check

### Fixed

* Fixed incorrect progress bar information during direct download

## [v0.0.1-beta](https://github.com/bucanero/pkgi-ps3/releases/tag/v0.0.1-beta) - 2019-12-04

### Added

* First public beta release

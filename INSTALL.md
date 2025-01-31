# Installation
* [FreeBSD](#freebsd)
* [Debian](#debian)
* [Fedora](#fedora)
* [Arch](#arch)
* [MacOS](#macos)
* [Windows](#windows)
* [Compiling](#compiling)
  * [Dependencies](#dependencies)
  * [Documentation](#documentation)
  * [OS X Notes](#os-x-notes)
  * [FreeBSD Notes](#freebsd-notes)
* [Notes](#notes)
  * [Compilation variables](#compilation-variables)
  * [Environment variables](#environment-variables)


## FreeBSD
```
pkg install toxic
```

## Debian
```
sudo apt install toxic
```

## Fedora
No dnf repo, install from source

## Arch
```
sudo pacman -S toxic
```

## MacOS
Not in brew, install from source.

## Windows
Not supported.

## Compiling
```
make
sudo make install
```
### Dependencies
| Name                                                 | Needed by                  | Debian package      |
|------------------------------------------------------|----------------------------|---------------------|
| [Tox Core](https://github.com/toktok/c-toxcore)      | BASE                       | libtoxcore-dev      |
| [NCurses](https://www.gnu.org/software/ncurses)      | BASE                       | libncursesw5-dev    |
| [LibConfig](http://www.hyperrealm.com/libconfig)     | BASE                       | libconfig-dev       |
| [GNUmake](https://www.gnu.org/software/make)         | BASE                       | make                |
| [libcurl](http://curl.haxx.se/)                      | BASE                       | libcurl4-openssl-dev|
| [libqrencode](https://fukuchi.org/works/qrencode/)   | QRCODE                     | libqrencode-dev     |
| [OpenAL](http://openal.org)                          | AUDIO, SOUND NOTIFICATIONS | libopenal-dev       |
| [OpenALUT](http://openal.org)                        | SOUND NOTIFICATIONS        | libalut-dev         |
| [LibNotify](https://developer.gnome.org/libnotify)   | DESKTOP NOTIFICATIONS      | libnotify-dev       |
| [X11](https://gitlab.freedesktop.org/xorg/lib/libx11)| VIDEO, DESKTOP FOCUS       | libx11-dev          |
| [Python 3](http://www.python.org/)                   | PYTHON                     | python3-dev         |
| [AsciiDoc](http://asciidoc.org/index.html)           | DOCUMENTATION<sup>1</sup>  | asciidoc            |

<sup>1</sup>: see [Documentation](#documentation)

### Documentation
Run `make doc` in the build directory after editing the asciidoc files to regenerate the manpages.<br />
**Note for developers**: asciidoc files and generated manpages will need to be committed together.<br />
**Note for everyone**: [asciidoc](http://asciidoc.org/index.html) (and this step) is only required for regenerating manpages when you modify them.

### OS X Notes
Using [Homebrew](http://brew.sh):
```
brew install curl qrencode openal-soft freealut libconfig libpng
brew install --HEAD https://raw.githubusercontent.com/Tox/homebrew-tox/master/Formula/libtoxcore.rb
brew install libnotify
export PKG_CONFIG_PATH=/usr/local/opt/openal-soft/lib/pkgconfig
make
```

### FreeBSD Notes
To compile Toxic on FreeBSD you'll need to use `gmake` instead of `make`, and you may need to manually tell `pkg-config` where to find the toxcore library.
In order to build with audio/video support you'll need to install the [v4l_compat](https://www.freshports.org/multimedia/v4l_compat) port.

The following commands should work for a default installation of both toxcore and toxic:
```
export PKG_CONFIG_PATH=$PKG_CONFIG_PATH:/usr/local/lib/pkgconfig
gmake
sudo -E gmake install
```

## Notes

### Compilation variables
* You can add specific flags to the Makefile with `USER_CFLAGS=""` and `USER_LDFLAGS=""` passed as arguments to make, or as environment variables
* Default compile options can be overridden by using special variables:
  * `DISABLE_X11=1` → Disable X11 support (needed for video and window focus tracking)
  * `DISABLE_AV=1` → Disable audio call support
  * `DISABLE_SOUND_NOTIFY=1` → Disable sound notifications support
  * `DISABLE_QRCODE=1` → Disable QR exporting support
  * `DISABLE_QRPNG=1` → Disable support for exporting QR as PNG
  * `DISABLE_DESKTOP_NOTIFY=1` → Disable desktop notifications support
  * `DISABLE_GAMES=1` → Disable support for games
  * `ENABLE_PYTHON=1` → Build toxic with Python scripting support
  * `ENABLE_RELEASE=1` → Build toxic without debug symbols and with full compiler optimizations
  * `ENABLE_ASAN=1` → Build toxic with LLVM Address Sanitizer enabled (reduces performance but increases security)
  * `ENABLE_TOX_EXPERIMENTAL=1` → Build with support for Tox's experimental API functionality

* `DESTDIR=""` Specifies the base install directory for binaries and data files (e.g.: DESTDIR="/tmp/build/pkg")

### Environment variables
* You can use the `CFLAGS` and `LDFLAGS` environment variables to add specific flags to the Makefile
* The `PREFIX` environment variable specifies a base install directory for binaries and data files. This is interchangeable with the `DESTDIR` variable, and is generally used by systems that have the `PREFIX` environment variable set by default.<br />
**Note**: `sudo` does not preserve user environment variables by default on some systems. See the `sudoers` manual for more information.

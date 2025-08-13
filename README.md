# MCServer

Command line interface tool to manage Minecraft Java Edition Vanilla Server instances.

## Why?

When you want to play Minecraft Java Edition multiplayer and want
to self host, keeping up-to-date proved to be a hassle. You launch
the desktop game, find out there was an update, have to re-download
the JAR on your server, and then re-launch the server.
Where you simply wanted to mine, craft and chill with your friends,
you end up doing some uninteresting sysadmin tasks.
In the pure sysadmin spirit, this tool aims to automate that.

## How does it work?

The tool fetches the Minecraft Java Edition version manifest
and depending on the requested action, installs the requested
or latest version of the server JAR; or launches a server
through the installed Java version.

It also verifies the signature of archives and enforces
HTTPS connections to avoid DNS-spoofing, so it provides
basic security when it comes to what it installs.

## How to use it

Install the latest version:
```
mcserver install # Install latest release archive
```

Launch a server for a world named `$(hostname)` on the latest version, installing it if necessary:
```
mcserver launch
```

You can specify an explicit version, even an alpha or a beta:
```
mcserver -version release/1.16.5 install
mcserver -version old_alpha/a1.2.2a install
```

## Dependencies

The tool is written in C and has few dependencies,
the Java Runtime Environment is a runtime dependency
required to launch servers from the tool.

You will need `curl`, `openssl` and `json-c`.
If installing from the debian package, these should install
automatically. Else, refer to your operating system
documentation on how to install these packages.

## Configure, build, install and package

CMake is used to configure, build and install binaires and documentations, version 3.14 minimum is required:

```
cmake -B build -S .
cmake --build build
cmake --install build
```

Or, you can create the Debian package (.deb) archive:
```
cmake -B build -S . -DCPACK_GENERATOR=DEB
cmake --build build --target package
```

## Copying

Jormungandr sources, binaries and documentations are distributed under the Affero GNU Public License version 3.0, see LICENSE.

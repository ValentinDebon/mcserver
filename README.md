# MCServer

Command line tool to download, launch and manage Minecraft vanilla server instances.

## How to use it

MCServer will cache the version manifest, and download on-demand server jar archives.

```{sh}
mcserver install # Install latest release archive
mcserver -world . launch # Launch the latest release in the current working directory
```

You can specify an explicit version, even an alpha or a beta

```{sh}
mcserver -release 1.16.5 install
mcserver -alpha a1.2.2a install
```

## Dependencies

You will need `curl`, `openssl` and `json-c`.
See your distribution on how to install these packages.

## Configure, build and install

CMake is used to configure, build and install binaires and documentations, version 3.14 minimum is required:

```sh
mkdir -p build && cd build
cmake ../
cmake --build .
cmake --install .
```


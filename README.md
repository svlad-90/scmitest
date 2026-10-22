# scmitest

Small SCMI sensor test for Xen guest domains.
Supports generic Linux builds with CMake and Android integration with Soong.
The implementation has no Yocto-specific dependencies.

The tool reads two accelerometer samples from Linux IIO, 100 ms apart, and
prints their values and difference. It defaults to the sensor named `scmi-smoke`,
matching the fake sensor provided by the
[vhost-device SCMI backend](https://github.com/rust-vmm/vhost-device/tree/main/vhost-device-scmi).

## Usage

```text
scmitest [--root IIO_ROOT] [expected-iio-name]
scmitest --list [--root IIO_ROOT]
scmitest --help
scmitest -h
```

| Argument | Description |
|---|---|
| `expected-iio-name` | Optional sensor name to select, read from the device's `name` attribute. Defaults to `scmi-smoke`. This is a name, not a device path. The command fails if no readable accelerometer has that name. |
| `--root IIO_ROOT` | Directory containing the `iio:device*` entries to scan. Defaults to `/sys/bus/iio/devices`. Use the parent directory, not an individual device directory. |
| `--list` | List IIO devices under the selected root, showing each device's path, name, and availability of the three accelerometer attributes. Prints to standard output and exits without reading samples. A positional sensor name is not accepted in this mode. |
| `--help`, `-h` | Print usage and default values to standard output, then exit without testing a sensor. |

Examples:

```sh
# List devices under the default root.
scmitest --list
# Select a sensor named my-sensor.
scmitest my-sensor
# Scan a different IIO root.
scmitest --root /path/to/iio/devices my-sensor
# List devices under a different root without sampling.
scmitest --root /path/to/iio/devices --list
```

The selected sensor must have readable `in_accel_x_raw`, `in_accel_y_raw`, and
`in_accel_z_raw` attributes. Use `--list` to inspect available sensor names.
PASS means both samples were read successfully from the named sensor. It does
not require a particular delta.

Exit status:

| Code | Meaning |
|---|---|
| `0` | Both samples were read successfully, or help/listing was requested. Listing is informational and returns `0` even if the root directory cannot be opened. |
| `1` | Sensor discovery or sample reading failed. |
| `2` | The value after `--root` is missing, more than one positional sensor name was supplied, or a sensor name was combined with `--list`. |

## Build

### CMake

Requires CMake 3.16 or newer and a C++17 compiler and standard library.

```sh
cmake -S . -B build
cmake --build build
./build/scmitest --help
```

Installation uses the standard CMake `CMAKE_INSTALL_PREFIX` and
`CMAKE_INSTALL_BINDIR` settings.

### Android (Soong)

Check out this repository in the Android source tree, add `scmitest` to the
device product's `PRODUCT_PACKAGES`, and add the repository's `sepolicy`
directory to `BOARD_VENDOR_SEPOLICY_DIRS`. The checkout directory can have a
different name; the Soong module installs to `/vendor/bin/scmitest`.

The supplied policy lets the Android shell execute the tool and read IIO sysfs
attributes labeled `sysfs_iio_devices`. The integrating device policy must
define and assign that type to its IIO device tree.

### Yocto Integration

Fetch a fixed Git revision in a recipe, set `S` to the checkout root, and use
`inherit cmake`. Include the resulting package in the guest image. With the
usual Yocto installation paths, the binary installs to `/usr/bin/scmitest`.

## Run

```sh
# Android
/vendor/bin/scmitest
# Linux (when installed under /usr)
/usr/bin/scmitest
```

Requires an IIO accelerometer with readable `in_accel_x_raw`,
`in_accel_y_raw`, and `in_accel_z_raw` sysfs attributes.

## Tests

The native Linux tests use temporary IIO files and require Python 3.10 or
newer. No sensor hardware is needed. Tests are disabled by default.

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
cd build
ctest --output-on-failure
```

GitHub Actions runs the build and tests with GCC and Clang.

## License

Apache License 2.0; see [LICENSE](LICENSE).

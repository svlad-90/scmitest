// SPDX-License-Identifier: Apache-2.0

#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

namespace {

namespace fs = std::filesystem;

constexpr const char *kIioRoot = "/sys/bus/iio/devices";
constexpr const char *kDefaultName = "scmi-smoke";

struct Sample {
    long x;
    long y;
    long z;
};

[[nodiscard]] std::error_code read_first_line(const fs::path &path, std::string &out)
{
    std::ifstream file(path);
    if (!file) {
        return std::make_error_code(std::errc::io_error);
    }

    std::string line;
    if (!std::getline(file, line)) {
        if (file.bad()) {
            return std::make_error_code(std::errc::io_error);
        }
        return std::make_error_code(std::errc::no_message_available);
    }

    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }

    out = std::move(line);
    return {};
}

[[nodiscard]] std::error_code read_accel_value(const fs::path &path, long &value)
{
    std::string text;
    if (auto error = read_first_line(path, text)) {
        return error;
    }

    // Accept leading whitespace and an optional plus sign in sensor values.
    std::string_view number(text);
    const auto start = number.find_first_not_of(" \t\n\r\f\v");
    if (start == std::string_view::npos) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    number.remove_prefix(start);
    if (number.front() == '+') {
        number.remove_prefix(1);
        if (number.empty() || number.front() == '-') {
            return std::make_error_code(std::errc::invalid_argument);
        }
    }
    const auto result = std::from_chars(number.data(), number.data() + number.size(), value);
    if (result.ec != std::errc{}) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    return {};
}

bool has_accel_triplet(const fs::path &dir)
{
    return std::ifstream(dir / "in_accel_x_raw").is_open() &&
           std::ifstream(dir / "in_accel_y_raw").is_open() &&
           std::ifstream(dir / "in_accel_z_raw").is_open();
}

[[nodiscard]] std::error_code read_sample(const fs::path &dir, Sample &sample)
{
    Sample result{};
    if (auto error = read_accel_value(dir / "in_accel_x_raw", result.x)) {
        return error;
    }
    if (auto error = read_accel_value(dir / "in_accel_y_raw", result.y)) {
        return error;
    }
    if (auto error = read_accel_value(dir / "in_accel_z_raw", result.z)) {
        return error;
    }

    sample = result;
    return {};
}

std::string device_name(const fs::path &dir)
{
    std::string name;

    if (read_first_line(dir / "name", name)) {
        return "<unknown>";
    }

    return name;
}

void list_iio_devices(const fs::path &iio_root, std::ostream &output)
{
    std::error_code error;
    fs::directory_iterator root(iio_root, error);
    if (error) {
        std::cerr << "scmitest: cannot open " << iio_root.string()
                  << ": " << error.message() << '\n';
        return;
    }

    for (; root != fs::directory_iterator{}; root.increment(error)) {
        const auto &path = root->path();
        if (path.filename().string().rfind("iio:device", 0) != 0) {
            continue;
        }

        output << "scmitest: iio device " << path.string()
               << " name=" << device_name(path)
               << " accel_triplet=" << (has_accel_triplet(path) ? "yes" : "no") << '\n';
    }
    if (error) {
        std::cerr << "scmitest: cannot scan " << iio_root.string()
                  << ": " << error.message() << '\n';
    }
}

[[nodiscard]] std::error_code find_iio_device(const fs::path &iio_root,
                                              std::string_view expected_name,
                                              fs::path &out)
{
    std::error_code error;
    fs::directory_iterator root(iio_root, error);
    if (error) {
        return error;
    }

    for (; root != fs::directory_iterator{}; root.increment(error)) {
        const auto &path = root->path();
        if (path.filename().string().rfind("iio:device", 0) != 0) {
            continue;
        }

        if (!has_accel_triplet(path)) {
            continue;
        }
        if (device_name(path) == expected_name) {
            out = path;
            return {};
        }
    }

    if (error) {
        return error;
    }
    return std::make_error_code(std::errc::no_such_file_or_directory);
}

std::string format_difference(long after, long before)
{
    const bool negative = after < before;
    // Unsigned subtraction keeps the full magnitude without signed overflow.
    const unsigned long magnitude = negative
                                        ? static_cast<unsigned long>(before) -
                                              static_cast<unsigned long>(after)
                                        : static_cast<unsigned long>(after) -
                                              static_cast<unsigned long>(before);

    return (negative ? "-" : "") + std::to_string(magnitude);
}

void print_usage(std::ostream &output, const char *argv0)
{
    output << "Usage:\n"
           << "  " << argv0 << " [--root IIO_ROOT] [expected-iio-name]\n"
           << "  " << argv0 << " --list [--root IIO_ROOT]\n"
           << "Checks an IIO accelerometer with in_accel_{x,y,z}_raw.\n"
           << "Default IIO root: " << kIioRoot << '\n'
           << "Default expected name: " << kDefaultName << '\n';
}

} // namespace

int main(int argc, char **argv)
{
    std::string_view expected_name = kDefaultName;
    fs::path iio_root = kIioRoot;
    fs::path device;
    Sample first{};
    Sample second{};
    bool list_only = false;
    bool name_set = false;

    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        if (argument == "--list") {
            list_only = true;
        } else if (argument == "--root") {
            if (++i == argc) {
                print_usage(std::cerr, argv[0]);
                return 2;
            }
            iio_root = argv[i];
        } else if (argument == "--help" || argument == "-h") {
            print_usage(std::cout, argv[0]);
            return 0;
        } else if (!name_set) {
            expected_name = argv[i];
            name_set = true;
        } else {
            print_usage(std::cerr, argv[0]);
            return 2;
        }
    }

    if (list_only && name_set) {
        print_usage(std::cerr, argv[0]);
        return 2;
    }

    if (list_only) {
        list_iio_devices(iio_root, std::cout);
        return 0;
    }

    if (auto error = find_iio_device(iio_root, expected_name, device)) {
        std::cerr << "scmitest: no readable SCMI/IIO accel device named "
                  << expected_name << " found under "
                  << iio_root.string() << ": " << error.message() << '\n';
        list_iio_devices(iio_root, std::cerr);
        return 1;
    }

    if (auto error = read_sample(device, first)) {
        std::cerr << "scmitest: failed to read first sample from " << device.string()
                  << ": " << error.message() << '\n';
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    if (auto error = read_sample(device, second)) {
        std::cerr << "scmitest: failed to read second sample from " << device.string()
                  << ": " << error.message() << '\n';
        return 1;
    }

    std::cout << "scmitest: device=" << device.string() << " name=" << device_name(device) << '\n'
              << "scmitest: sample0 x=" << first.x << " y=" << first.y << " z=" << first.z << '\n'
              << "scmitest: sample1 x=" << second.x << " y=" << second.y << " z=" << second.z << '\n'
              << "scmitest: delta x=" << format_difference(second.x, first.x)
              << " y=" << format_difference(second.y, first.y)
              << " z=" << format_difference(second.z, first.z) << '\n'
              << "scmitest: PASS\n";

    return 0;
}

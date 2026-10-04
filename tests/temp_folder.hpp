#pragma once

// A folder of a test's own under the system's temporary folder, gone with
// the test, and files written into it as another program would.
//
//     const TempFolder folder;
//     const auto file = folder.write("scripts/coin.cpp", "int a;\n");

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>

struct TempFolder {
    std::filesystem::path path;

    TempFolder() {
        std::random_device random;
        path = std::filesystem::temp_directory_path() /
               ("fjedit-test-" + std::to_string(random()) + std::to_string(random()));
        std::filesystem::create_directories(path);
    }
    ~TempFolder() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;

    // Writes `bytes` to `name` in the folder, making the folders it is in.
    std::filesystem::path write(const std::filesystem::path& name, std::string_view bytes) const {
        const std::filesystem::path file = path / name;
        std::filesystem::create_directories(file.parent_path());
        std::ofstream(file, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return file;
    }

    // What the file `name` in the folder holds.
    [[nodiscard]] std::string read(const std::filesystem::path& name) const {
        std::ifstream in(path / name, std::ios::binary);
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }
};

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
std::vector<std::uint8_t> read_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return {};
    const auto size = input.tellg();
    if (size <= 0) return {};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    return input ? bytes : std::vector<std::uint8_t>{};
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        std::wcerr << L"Usage: gbfr_msgpack_to_json <input.msg> <output.json>\n";
        return 2;
    }
    const fs::path input_path = fs::absolute(argv[1]);
    const fs::path output_path = fs::absolute(argv[2]);
    const auto bytes = read_file(input_path);
    if (bytes.empty()) {
        std::wcerr << L"Input is empty or unreadable: " << input_path << L'\n';
        return 3;
    }
    try {
        const auto document = json::from_msgpack(bytes);
        if (!output_path.parent_path().empty()) fs::create_directories(output_path.parent_path());
        std::ofstream output(output_path, std::ios::trunc);
        if (!output) {
            std::wcerr << L"Cannot write: " << output_path << L'\n';
            return 4;
        }
        output << document.dump(2) << '\n';
        std::cout << "root_type=" << document.type_name() << " input_bytes=" << bytes.size()
                  << " output_bytes=" << output.tellp() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "MessagePack decode failed: " << error.what() << '\n';
        return 5;
    }
    return 0;
}

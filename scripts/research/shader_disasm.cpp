#include <d3dcompiler.h>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

namespace {
std::vector<unsigned char> read_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return {};
    const auto size = input.tellg();
    if (size <= 0) return {};
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    return input ? bytes : std::vector<unsigned char>{};
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2 && argc != 3) {
        std::wcerr << L"Usage: gbfr_shader_disasm <shader> [output.asm]\n";
        return 2;
    }

    const fs::path input_path = fs::absolute(argv[1]);
    const auto bytes = read_file(input_path);
    if (bytes.empty()) {
        std::wcerr << L"Shader is empty or unreadable: " << input_path << L'\n';
        return 3;
    }

    ID3DBlob* assembly = nullptr;
    const auto status = D3DDisassemble(
        bytes.data(), bytes.size(),
        D3D_DISASM_ENABLE_INSTRUCTION_NUMBERING | D3D_DISASM_ENABLE_INSTRUCTION_OFFSET,
        nullptr, &assembly);
    if (FAILED(status) || !assembly) {
        std::wcerr << L"D3DDisassemble failed for " << input_path << L" (HRESULT 0x"
                   << std::hex << static_cast<unsigned long>(status) << L")\n";
        return 4;
    }

    const auto* text = static_cast<const char*>(assembly->GetBufferPointer());
    auto size = assembly->GetBufferSize();
    if (size && text[size - 1] == '\0') --size;
    bool written = false;
    if (argc == 3) {
        const fs::path output_path = fs::absolute(argv[2]);
        if (!output_path.parent_path().empty()) fs::create_directories(output_path.parent_path());
        std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
        if (output) {
            output.write(text, static_cast<std::streamsize>(size));
            written = output.good();
        }
        if (!written) std::wcerr << L"Cannot write: " << output_path << L'\n';
    } else {
        std::cout.write(text, static_cast<std::streamsize>(size));
        written = std::cout.good();
    }

    assembly->Release();
    return written ? 0 : 5;
}

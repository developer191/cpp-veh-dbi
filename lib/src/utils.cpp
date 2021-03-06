#include <Windows.h>
#include <DbgHelp.h>
#include <Psapi.h>
#include <TlHelp32.h>
#include <strsafe.h>

#include <memory>

#include <utils.hpp>

namespace Utils {
    // Get module name which includes given address.
    std::tuple<bool, std::string> GetModuleNameByAddr(size_t address) {
        std::string res;
        std::wstring wname;

        bool load_module = false;

        HANDLE process = GetCurrentProcess();
        DWORD pid = GetCurrentProcessId();

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
        MODULEENTRY32W entry;
        entry.dwSize = sizeof(entry);

        if (Module32FirstW(snapshot, &entry)) {
            do {
                MODULEINFO info;
                GetModuleInformation(process, entry.hModule, &info, sizeof(info));

                size_t start = reinterpret_cast<size_t>(info.lpBaseOfDll);
                size_t end = start + info.SizeOfImage;

                if (start <= address && address <= end) {
                    wname = entry.szModule;
                    res.assign(wname.begin(), wname.end());

                    size_t dllbase = reinterpret_cast<size_t>(info.lpBaseOfDll);
                    if (SymLoadModule(process, NULL, res.c_str(), 0, dllbase, 0) || !GetLastError()) {
                        load_module = true;
                    }
                    break;
                }

            } while (Module32NextW(snapshot, &entry));
            CloseHandle(snapshot);
        }

        return std::make_tuple(load_module, std::move(res));
    }

    // Get symbol name of given address.
    std::string GetSymbolName(size_t address) {
        std::string name;
        auto buffer = std::make_unique<BYTE[]>(sizeof(IMAGEHLP_SYMBOL) + MAX_SYM_NAME);
        auto symbol = reinterpret_cast<IMAGEHLP_SYMBOL*>(buffer.get());

        std::memset(symbol, 0, sizeof(IMAGEHLP_SYMBOL) + MAX_SYM_NAME);
        symbol->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL);
        symbol->MaxNameLength = MAX_SYM_NAME;

        size_t disp;
        if (SymGetSymFromAddr(GetCurrentProcess(), address, &disp, symbol)) {
            name = symbol->Name;
        }

        return name;
    }

    // Set trap flag.
    void SetSingleStep(PCONTEXT context) {
        context->EFlags |= (1 << 8);
    }

    // Set software breakpoint.
    void SoftwareBP::Set(size_t address) {
        if (auto iter = bp.find(address); iter != bp.end()) {
            auto&[backup, set] = (*iter).second;
            if (!set) {
                set = true;
                *reinterpret_cast<BYTE*>(address) = 0xCC;
            }
        }

        DWORD protect;
        VirtualProtect(reinterpret_cast<LPVOID>(address), 1, PAGE_EXECUTE_READWRITE, &protect);

        BYTE* ptr = reinterpret_cast<BYTE*>(address);
        BYTE backup = *ptr;
        *ptr = 0xCC;

        bp.emplace(address, std::make_tuple(backup, true));
    }

    // Release breakpoint.
    bool SoftwareBP::Recover(size_t address) {
        if (auto iter = bp.find(address); iter != bp.end()) {
            auto&[backup, set] = (*iter).second;
            if (set) {
                set = false;
                *reinterpret_cast<BYTE*>(address) = backup;
            }
            return true;
        }
        return false;
    }
    
    // Get entry point address.
    size_t GetEntryPointAddress() {
        MODULEENTRY32W entry;
        entry.dwSize = sizeof(entry);

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());

        Module32FirstW(snapshot, &entry);
        CloseHandle(snapshot);

        PIMAGE_DOS_HEADER doshdr = reinterpret_cast<PIMAGE_DOS_HEADER>(entry.modBaseAddr);
        PIMAGE_NT_HEADERS nthdr = ImageNtHeader(doshdr);
        PIMAGE_OPTIONAL_HEADER opthdr = &nthdr->OptionalHeader;

        size_t entrypoint = reinterpret_cast<size_t>(entry.modBaseAddr) + opthdr->AddressOfEntryPoint;
        return entrypoint;
    }

    // Get text section address.
    std::tuple<size_t, size_t> GetTextSectionAddress() {
        MODULEENTRY32W entry;
        entry.dwSize = sizeof(entry);

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());

        Module32FirstW(snapshot, &entry);
        CloseHandle(snapshot);

        PIMAGE_DOS_HEADER doshdr = reinterpret_cast<PIMAGE_DOS_HEADER>(entry.modBaseAddr);
        PIMAGE_NT_HEADERS nthdr = ImageNtHeader(doshdr);
        PIMAGE_SECTION_HEADER section = reinterpret_cast<PIMAGE_SECTION_HEADER>(nthdr + 1);

        DWORD entrypoint = nthdr->OptionalHeader.AddressOfEntryPoint;

        DWORD num_sections = nthdr->FileHeader.NumberOfSections;
        for (DWORD i = 0; i < num_sections; ++i) {
            size_t start = section->VirtualAddress;
            size_t end = start + section->SizeOfRawData;

            if (start <= entrypoint && entrypoint <= end) {
                return std::make_tuple(start, end);
            }
            ++section;
        }
        return std::make_tuple(0, 0);    
    }
}

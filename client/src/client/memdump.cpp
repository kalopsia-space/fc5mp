#include "memdump.hpp"
#ifdef _WIN32
#include <windows.h>
#include <cstdio>
#include <cstring>

namespace fc5mp::memdump {

static FILE* g_log = nullptr;

static void mlog(const char* fmt, ...) {
    if (!g_log) {
        wchar_t exepath[MAX_PATH];
        GetModuleFileNameW(nullptr, exepath, MAX_PATH);
        wchar_t* sl = wcsrchr(exepath, L'\\');
        if (sl) wcscpy(sl + 1, L"fc5mp-memdump-log.txt");
        else wcscpy(exepath, L"fc5mp-memdump-log.txt");
        g_log = _wfopen(exepath, L"w");
        if (!g_log) return;
    }
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fflush(g_log);
}

static const char* prot_str(DWORD prot) {
    prot &= 0xFF;
    switch (prot) {
        case PAGE_EXECUTE_READWRITE: return "rwx";
        case PAGE_EXECUTE_READ:     return "r-x";
        case PAGE_READWRITE:        return "rw-";
        case PAGE_READONLY:         return "r--";
        case PAGE_EXECUTE:          return "--x";
        case PAGE_WRITECOPY:        return "rc-";
        case PAGE_EXECUTE_WRITECOPY:return "rcx";
        case PAGE_NOACCESS:         return "---";
        default:                    return "???";
    }
}

void dump_fc_module() {
    HMODULE m = GetModuleHandleW(L"FC_m64.dll");
    if (!m) {
        mlog("FC_m64.dll not loaded, aborting dump\n");
        return;
    }

    auto base = reinterpret_cast<BYTE*>(m);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        mlog("Bad DOS signature at %p\n", base);
        return;
    }
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        mlog("Bad NT signature\n");
        return;
    }

    SIZE_T image_size = nt->OptionalHeader.SizeOfImage;
    WORD num_sections = nt->FileHeader.NumberOfSections;

    mlog("FC_m64.dll base=%p SizeOfImage=0x%zx sections=%u\n",
         base, image_size, num_sections);

    // Log section layout
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < num_sections; i++) {
        char name[9] = {};
        memcpy(name, sec[i].Name, 8);
        mlog("  section[%u] %-8s VA=0x%08x VSize=0x%08x RawOff=0x%08x RawSize=0x%08x Chars=0x%08x\n",
             i, name, sec[i].VirtualAddress, sec[i].Misc.VirtualSize,
             sec[i].PointerToRawData, sec[i].SizeOfRawData, sec[i].Characteristics);
    }

    // Build output path next to the executable
    wchar_t dump_path[MAX_PATH], map_path[MAX_PATH];
    GetModuleFileNameW(nullptr, dump_path, MAX_PATH);
    wchar_t* sl = wcsrchr(dump_path, L'\\');
    if (sl) {
        wcscpy(sl + 1, L"fc5mp-memdump.bin");
        wcscpy(map_path, dump_path);
        sl = wcsrchr(map_path, L'\\');
        wcscpy(sl + 1, L"fc5mp-memdump-map.txt");
    } else {
        wcscpy(dump_path, L"fc5mp-memdump.bin");
        wcscpy(map_path, L"fc5mp-memdump-map.txt");
    }

    FILE* fp = _wfopen(dump_path, L"wb");
    FILE* fm = _wfopen(map_path, L"w");
    if (!fp || !fm) {
        mlog("Failed to open output files\n");
        if (fp) fclose(fp);
        if (fm) fclose(fm);
        return;
    }

    // Walk all memory regions belonging to this module's allocation
    BYTE* addr = base;
    BYTE* end = base + image_size;
    SIZE_T total_committed = 0;
    SIZE_T total_zero_fill = 0;
    int regions = 0;
    BYTE* last_end = base;

    MEMORY_BASIC_INFORMATION mbi;
    while (addr < end && VirtualQuery(addr, &mbi, sizeof(mbi))) {
        BYTE* region_start = (BYTE*)mbi.BaseAddress;
        SIZE_T region_size = mbi.RegionSize;

        // Clamp to image bounds
        if (region_start + region_size > end)
            region_size = end - region_start;

        // Fill gap between regions with zeros
        if (region_start > last_end) {
            SIZE_T gap = region_start - last_end;
            BYTE zero[4096];
            memset(zero, 0, sizeof(zero));
            while (gap > 0) {
                SIZE_T chunk = gap > sizeof(zero) ? sizeof(zero) : gap;
                fwrite(zero, 1, chunk, fp);
                gap -= chunk;
                total_zero_fill += chunk;
            }
        }

        fprintf(fm, "%p  offset=0x%08zx  size=0x%08zx  prot=%s  state=%s\n",
                region_start,
                (size_t)(region_start - base),
                region_size,
                prot_str(mbi.Protect),
                mbi.State == MEM_COMMIT ? "COMMIT" :
                mbi.State == MEM_RESERVE ? "RESERVE" : "FREE");

        if (mbi.State == MEM_COMMIT) {
            // Try to read; some pages may be guarded
            BYTE* buf = (BYTE*)VirtualAlloc(nullptr, region_size,
                                            MEM_COMMIT, PAGE_READWRITE);
            if (buf) {
                SIZE_T copied = 0;
                // Use ReadProcessMemory on ourselves to handle guard pages safely
                if (ReadProcessMemory(GetCurrentProcess(), region_start,
                                      buf, region_size, &copied)) {
                    fwrite(buf, 1, copied, fp);
                    total_committed += copied;
                    // Zero-fill remainder if partial read
                    if (copied < region_size) {
                        BYTE zero[4096];
                        memset(zero, 0, sizeof(zero));
                        SIZE_T rem = region_size - copied;
                        while (rem > 0) {
                            SIZE_T chunk = rem > sizeof(zero) ? sizeof(zero) : rem;
                            fwrite(zero, 1, chunk, fp);
                            rem -= chunk;
                        }
                    }
                } else {
                    // ReadProcessMemory failed — write zeros
                    BYTE zero[4096];
                    memset(zero, 0, sizeof(zero));
                    SIZE_T rem = region_size;
                    while (rem > 0) {
                        SIZE_T chunk = rem > sizeof(zero) ? sizeof(zero) : rem;
                        fwrite(zero, 1, chunk, fp);
                        rem -= chunk;
                    }
                    fprintf(fm, "  (ReadProcessMemory failed, err=%lu)\n", GetLastError());
                }
                VirtualFree(buf, 0, MEM_RELEASE);
            }
        } else {
            // Reserved/free — write zeros
            BYTE zero[4096];
            memset(zero, 0, sizeof(zero));
            SIZE_T rem = region_size;
            while (rem > 0) {
                SIZE_T chunk = rem > sizeof(zero) ? sizeof(zero) : rem;
                fwrite(zero, 1, chunk, fp);
                rem -= chunk;
            }
        }

        last_end = region_start + region_size;
        regions++;

        BYTE* next = (BYTE*)mbi.BaseAddress + mbi.RegionSize;
        if (next <= addr) break;
        addr = next;
    }

    fclose(fp);
    fclose(fm);

    mlog("Dump complete: %d regions, 0x%zx bytes committed, 0x%zx zero-fill\n",
         regions, total_committed, total_zero_fill);
    mlog("Output: fc5mp-memdump.bin + fc5mp-memdump-map.txt\n");

    // Also write a fixed-up PE header as a separate file so Ghidra can load it.
    // Copy the dump and patch section headers: PointerToRawData = VirtualAddress,
    // SizeOfRawData = VirtualSize (same technique as the TCU dump).
    wchar_t pe_path[MAX_PATH];
    wcscpy(pe_path, dump_path);
    sl = wcsrchr(pe_path, L'\\');
    if (sl) wcscpy(sl + 1, L"fc5mp-memdump-fixed.bin");
    else wcscpy(pe_path, L"fc5mp-memdump-fixed.bin");

    // Read back the dump we just wrote
    fp = _wfopen(dump_path, L"rb");
    if (!fp) { mlog("Failed to reopen dump for PE fixup\n"); return; }
    fseek(fp, 0, SEEK_END);
    long dump_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // Only copy the headers + enough to fix up (first 4KB is enough for headers)
    // Actually, just copy the whole thing and patch in place
    BYTE* dump_buf = (BYTE*)malloc(dump_size);
    if (!dump_buf) {
        mlog("malloc failed for %ld bytes\n", dump_size);
        fclose(fp);
        return;
    }
    fread(dump_buf, 1, dump_size, fp);
    fclose(fp);

    // Patch section headers in the buffer
    auto* d_dos = reinterpret_cast<IMAGE_DOS_HEADER*>(dump_buf);
    auto* d_nt = reinterpret_cast<IMAGE_NT_HEADERS*>(dump_buf + d_dos->e_lfanew);
    auto* d_sec = IMAGE_FIRST_SECTION(d_nt);
    for (WORD i = 0; i < d_nt->FileHeader.NumberOfSections; i++) {
        d_sec[i].PointerToRawData = d_sec[i].VirtualAddress;
        d_sec[i].SizeOfRawData = d_sec[i].Misc.VirtualSize;
    }

    // Clear potentially broken data directories (imports are resolved, TLS may
    // point to invalid offsets, etc.)
    auto& dd = d_nt->OptionalHeader.DataDirectory;
    // Keep Export (0) and Exception/pdata (3) — useful for analysis.
    // Clear Import (1), TLS (9), IAT (12), Bound Import (11), Delay Import (13).
    if (d_nt->OptionalHeader.NumberOfRvaAndSizes > 1)
        dd[1] = {};   // Import
    if (d_nt->OptionalHeader.NumberOfRvaAndSizes > 9)
        dd[9] = {};   // TLS
    if (d_nt->OptionalHeader.NumberOfRvaAndSizes > 11)
        dd[11] = {};  // Bound Import
    if (d_nt->OptionalHeader.NumberOfRvaAndSizes > 12)
        dd[12] = {};  // IAT
    if (d_nt->OptionalHeader.NumberOfRvaAndSizes > 13)
        dd[13] = {};  // Delay Import

    FILE* pf = _wfopen(pe_path, L"wb");
    if (pf) {
        fwrite(dump_buf, 1, dump_size, pf);
        fclose(pf);
        mlog("PE-fixed dump written: fc5mp-memdump-fixed.bin (%ld bytes)\n", dump_size);
    } else {
        mlog("Failed to write PE-fixed dump\n");
    }

    free(dump_buf);
    if (g_log) { fclose(g_log); g_log = nullptr; }
}

}  // namespace fc5mp::memdump
#endif

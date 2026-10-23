#pragma once
#ifdef _WIN32
namespace fc5mp::memdump {
// Dump FC_m64.dll's committed memory regions to disk.
// Called from the boot thread after hooks are installed and the game has
// had time to initialize (Denuvo decrypts code during startup).
// Output: fc5mp-memdump.bin  (contiguous image from module base)
//         fc5mp-memdump-map.txt (region map with protections)
void dump_fc_module();
}
#endif

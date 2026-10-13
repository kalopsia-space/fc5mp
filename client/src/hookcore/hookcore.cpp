#include "hookcore.hpp"
#if defined(_WIN32)
#include <MinHook.h>
namespace fc5mp::hook {
bool init(){ return MH_Initialize()==MH_OK; }
bool create(void* t, void* d, void** o){ return MH_CreateHook(t,d,o)==MH_OK; }
bool enable(void* t){ return MH_EnableHook(t)==MH_OK; }
bool enable_all(){ return MH_EnableHook(MH_ALL_HOOKS)==MH_OK; }
void uninit(){ MH_Uninitialize(); }
}
#else
namespace fc5mp::hook {
bool init(){ return false; }
bool create(void*, void*, void**){ return false; }
bool enable(void*){ return false; }
bool enable_all(){ return false; }
void uninit(){}
}
#endif

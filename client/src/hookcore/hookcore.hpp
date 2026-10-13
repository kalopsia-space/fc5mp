#pragma once
namespace fc5mp::hook {
bool init();
bool create(void* target, void* detour, void** original);
bool enable(void* target);
bool enable_all();
void uninit();
}

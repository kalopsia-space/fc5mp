#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <cstdint>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <cwchar>

#define EXPORT extern "C" __declspec(dllexport)


static FILE* g_log = nullptr;
static void stub_log(const char* fmt, ...) {
    if (!g_log) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fflush(g_log);
}

struct UPLAY_OVERLAPPED {
    int32_t is_completed;
    int32_t result;
};

static void complete(UPLAY_OVERLAPPED* ov, int32_t res = 0) {
    if (!ov || (uintptr_t)ov < 0x10000) {
        stub_log("  complete: bad ov=%p, skipping\n", ov);
        return;
    }
    ov->result = res;
    ov->is_completed = 1;
}

static const char g_username[] = "FC5MP_Player";
static const char g_account_id[] = "fe525702-a958-4b13-b052-f4af64e83c7d";
static const char g_ticket[] = "fc5mp_stub_ticket";
static const uint32_t GAME_ID = 4311;

static wchar_t g_save_dir[512];

static void init_save_dir() {
    wchar_t pf86[MAX_PATH] = {};
    if (SHGetFolderPathW(NULL, CSIDL_PROGRAM_FILESX86, NULL, 0, pf86) != S_OK)
        wcscpy(pf86, L"C:\\Program Files (x86)");
    _snwprintf(g_save_dir, 511,
        L"%s\\Ubisoft\\Ubisoft Game Launcher\\savegames\\%hs\\%u",
        pf86, g_account_id, GAME_ID);
    g_save_dir[511] = 0;
    SHCreateDirectoryExW(NULL, g_save_dir, NULL);
}

// ========== Core ==========
// Signatures verified against Detanup01/UplayLoader upc_r1_loader

EXPORT int UPLAY_Start(unsigned int uplay_id, unsigned int flags) {
    init_save_dir();
    stub_log("UPLAY_Start(id=%u, flags=%u) -> 1\n", uplay_id, flags);
    return 1;
}

static int g_update_count = 0;
EXPORT bool UPLAY_Update() {
    g_update_count++;
    if (g_update_count <= 3 || (g_update_count % 1000) == 0)
        stub_log("UPLAY_Update() [call #%d]\n", g_update_count);
    return true;
}
EXPORT bool UPLAY_Quit() { stub_log("UPLAY_Quit()\n"); return true; }

static int g_event_count = 0;
EXPORT bool UPLAY_GetNextEvent(void* event) {
    g_event_count++;
    if (g_event_count <= 3 || (g_event_count % 1000) == 0)
        stub_log("UPLAY_GetNextEvent(event=%p) [call #%d]\n", event, g_event_count);
    return false;
}

EXPORT bool UPLAY_GetLastError(void* out_error_string) {
    stub_log("UPLAY_GetLastError(out=%p)\n", out_error_string);
    return false;
}

EXPORT bool UPLAY_HasOverlappedOperationCompleted(UPLAY_OVERLAPPED* ov) {
    stub_log("UPLAY_HasOverlappedOperationCompleted(ov=%p, completed=%d)\n",
             ov, ov ? ov->is_completed : -1);
    return ov && ov->is_completed != 0;
}

EXPORT bool UPLAY_GetOverlappedOperationResult(UPLAY_OVERLAPPED* ov, unsigned int* out_result) {
    stub_log("UPLAY_GetOverlappedOperationResult(ov=%p)\n", ov);
    if (!ov) return false;
    if (out_result) *out_result = (unsigned int)ov->result;
    return true;
}

// ========== User ==========

EXPORT const char* UPLAY_USER_GetNameUtf8() {
    stub_log("UPLAY_USER_GetNameUtf8() -> %s\n", g_username);
    return g_username;
}

EXPORT const char* UPLAY_USER_GetAccountIdUtf8() {
    stub_log("UPLAY_USER_GetAccountIdUtf8() -> %s\n", g_account_id);
    return g_account_id;
}

EXPORT const char* UPLAY_USER_GetTicketUtf8() {
    stub_log("UPLAY_USER_GetTicketUtf8() -> %s\n", g_ticket);
    return g_ticket;
}

EXPORT bool UPLAY_USER_IsOwned(unsigned int uplay_id) {
    stub_log("UPLAY_USER_IsOwned(id=%u) -> true\n", uplay_id);
    return true;
}

EXPORT bool UPLAY_USER_IsInOfflineMode() {
    stub_log("UPLAY_USER_IsInOfflineMode() -> false\n");
    return false;
}

EXPORT bool UPLAY_USER_SetGameSession(unsigned int appid, void* session, unsigned int flags) {
    stub_log("UPLAY_USER_SetGameSession(appid=%u)\n", appid);
    return true;
}

EXPORT bool UPLAY_USER_ClearGameSession() {
    stub_log("UPLAY_USER_ClearGameSession()\n");
    return true;
}

EXPORT bool UPLAY_USER_GetConsumableItems(void* out_list) {
    stub_log("UPLAY_USER_GetConsumableItems(out=%p)\n", out_list);
    return true;
}

// ========== Installer ==========

EXPORT bool UPLAY_INSTALLER_Init(unsigned int flags) {
    stub_log("UPLAY_INSTALLER_Init(flags=%u)\n", flags);
    return true;
}

EXPORT const char* UPLAY_INSTALLER_GetLanguageUtf8() {
    stub_log("UPLAY_INSTALLER_GetLanguageUtf8() -> en-US\n");
    return "en-US";
}

struct ChunkIdList {
    uint64_t count;
    void* ids;
};

EXPORT bool UPLAY_INSTALLER_GetChunks(void* out_chunk_list) {
    stub_log("UPLAY_INSTALLER_GetChunks(out=%p)\n", out_chunk_list);
    if (!out_chunk_list) return false;
    auto* heap = GetProcessHeap();
    auto* list = (ChunkIdList*)HeapAlloc(heap, HEAP_ZERO_MEMORY, sizeof(ChunkIdList));
    if (list) {
        list->count = 1;
        list->ids = HeapAlloc(heap, HEAP_ZERO_MEMORY, 64);
    }
    memcpy(out_chunk_list, &list, sizeof(void*));
    stub_log("  -> chunks list=%p\n", list);
    return true;
}

EXPORT bool UPLAY_INSTALLER_AreChunksInstalled(void* chunk_ids, unsigned int count) {
    stub_log("UPLAY_INSTALLER_AreChunksInstalled(count=%u) -> true\n", count);
    return true;
}

EXPORT bool UPLAY_INSTALLER_ReleaseChunkIdList(void* list) {
    stub_log("UPLAY_INSTALLER_ReleaseChunkIdList(%p)\n", list);
    return true;
}

// ========== Save ==========
// Real Uplay R1 layout (from Mini_Uplay_API_Emu reverse engineering):
//   GameList header: { uint64_t count; void** item_ptrs; }
//   Each item:       { uint32_t slot_id; [pad4]; char* name_ptr; uint32_t size; }
//   item_ptrs is an array of pointers, each pointing to one item.

struct UPLAY_SAVE_Game {
    uint32_t slot_id;
    uint32_t _pad;
    char* name;
    uint32_t size;
};

struct UPLAY_SAVE_GameList {
    uint64_t count;
    UPLAY_SAVE_Game** items;
};

struct SaveHandle {
    HANDLE hFile;
    uint32_t slot_id;
};

static void save_path(wchar_t* out, size_t max_len, uint32_t slot_id) {
    _snwprintf(out, max_len, L"%s\\%u.save", g_save_dir, slot_id);
    out[max_len - 1] = 0;
}

EXPORT bool UPLAY_SAVE_GetSavegames(void* out, UPLAY_OVERLAPPED* ov) {
    stub_log("UPLAY_SAVE_GetSavegames(out=%p, ov=%p)\n", out, ov);
    auto** out_list = (UPLAY_SAVE_GameList**)out;
    if (!out_list) { complete(ov, -1); return false; }

    wchar_t pattern[512];
    _snwprintf(pattern, 511, L"%s\\*.save", g_save_dir);
    pattern[511] = 0;

    uint32_t slot_ids[16];
    uint32_t count = 0;

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (count >= 16) break;
            slot_ids[count] = (uint32_t)wcstoul(fd.cFileName, nullptr, 10);
            count++;
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    auto* heap = GetProcessHeap();
    auto* list = (UPLAY_SAVE_GameList*)HeapAlloc(heap, HEAP_ZERO_MEMORY,
                                                   sizeof(UPLAY_SAVE_GameList));
    if (list && count > 0) {
        list->count = count;
        list->items = (UPLAY_SAVE_Game**)HeapAlloc(heap, HEAP_ZERO_MEMORY,
                                                     count * sizeof(UPLAY_SAVE_Game*));
        for (uint32_t i = 0; i < count && list->items; i++) {
            auto* g = (UPLAY_SAVE_Game*)HeapAlloc(heap, HEAP_ZERO_MEMORY,
                                                    sizeof(UPLAY_SAVE_Game));
            if (g) {
                g->slot_id = slot_ids[i];
                char* nm = (char*)HeapAlloc(heap, HEAP_ZERO_MEMORY, 64);
                if (nm) snprintf(nm, 64, "Save %u", slot_ids[i]);
                g->name = nm;
                g->size = 0;
            }
            list->items[i] = g;
        }
    }
    *out_list = list;
    stub_log("  -> %u saves, list=%p, items=%p\n", count, list, list ? list->items : nullptr);
    complete(ov);
    return true;
}

EXPORT bool UPLAY_SAVE_ReleaseGameList(void* list) {
    stub_log("UPLAY_SAVE_ReleaseGameList(%p)\n", list);
    if (list) {
        auto* gl = (UPLAY_SAVE_GameList*)list;
        auto* heap = GetProcessHeap();
        if (gl->items) {
            for (uint64_t i = 0; i < gl->count; i++) {
                if (gl->items[i]) {
                    if (gl->items[i]->name) HeapFree(heap, 0, gl->items[i]->name);
                    HeapFree(heap, 0, gl->items[i]);
                }
            }
            HeapFree(heap, 0, gl->items);
        }
        HeapFree(heap, 0, gl);
    }
    return true;
}

EXPORT bool UPLAY_SAVE_Open(unsigned int slot_id, unsigned int mode,
                             void* out_handle, UPLAY_OVERLAPPED* ov) {
    stub_log("UPLAY_SAVE_Open(slot=%u, mode=%u)\n", slot_id, mode);
    auto** out_h = (SaveHandle**)out_handle;
    if (!out_h) { complete(ov, -1); return false; }

    wchar_t path[512];
    save_path(path, 512, slot_id);

    DWORD access, creation;
    if (mode == 0) {
        access = GENERIC_READ;
        creation = OPEN_EXISTING;
    } else {
        SHCreateDirectoryExW(NULL, g_save_dir, NULL);
        access = GENERIC_WRITE;
        creation = CREATE_ALWAYS;
    }

    HANDLE hFile = CreateFileW(path, access, FILE_SHARE_READ, NULL,
                               creation, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        *out_h = nullptr;
        complete(ov, -1);
        return false;
    }

    auto* h = (SaveHandle*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SaveHandle));
    h->hFile = hFile;
    h->slot_id = slot_id;
    *out_h = h;
    complete(ov);
    return true;
}

EXPORT bool UPLAY_SAVE_Read(void* handle, unsigned int bytes_to_read,
                             unsigned int offset, void* out_buf,
                             unsigned int out_bytes_read, UPLAY_OVERLAPPED* ov) {
    stub_log("UPLAY_SAVE_Read(handle=%p, bytes=%u, offset=%u)\n", handle, bytes_to_read, offset);
    auto* h = (SaveHandle*)handle;
    if (!h || h->hFile == INVALID_HANDLE_VALUE) { complete(ov, -1); return false; }

    LARGE_INTEGER li;
    li.QuadPart = offset;
    SetFilePointerEx(h->hFile, li, NULL, FILE_BEGIN);

    DWORD nread = 0;
    BOOL ok = ReadFile(h->hFile, out_buf, bytes_to_read, &nread, NULL);
    complete(ov, ok ? 0 : -1);
    return ok != FALSE;
}

EXPORT bool UPLAY_SAVE_Write(void* handle, unsigned int bytes_to_write,
                              void* buf, UPLAY_OVERLAPPED* ov) {
    stub_log("UPLAY_SAVE_Write(handle=%p, bytes=%u)\n", handle, bytes_to_write);
    auto* h = (SaveHandle*)handle;
    if (!h || h->hFile == INVALID_HANDLE_VALUE) { complete(ov, -1); return false; }

    DWORD written = 0;
    BOOL ok = WriteFile(h->hFile, buf, bytes_to_write, &written, NULL);
    complete(ov, ok ? 0 : -1);
    return ok != FALSE;
}

EXPORT bool UPLAY_SAVE_Close(void* handle) {
    stub_log("UPLAY_SAVE_Close(%p)\n", handle);
    auto* h = (SaveHandle*)handle;
    if (h) {
        if (h->hFile != INVALID_HANDLE_VALUE) CloseHandle(h->hFile);
        HeapFree(GetProcessHeap(), 0, h);
    }
    return true;
}

EXPORT bool UPLAY_SAVE_Remove(unsigned int slot_id, UPLAY_OVERLAPPED* ov) {
    stub_log("UPLAY_SAVE_Remove(slot=%u)\n", slot_id);
    wchar_t path[512];
    save_path(path, 512, slot_id);
    DeleteFileW(path);
    complete(ov);
    return true;
}

EXPORT bool UPLAY_SAVE_SetName(void* handle, const char* name) {
    stub_log("UPLAY_SAVE_SetName(%p, %s)\n", handle, name ? name : "(null)");
    return true;
}

// ========== Additional core ==========

EXPORT int UPLAY_Init(unsigned int uplay_id, unsigned int flags) {
    init_save_dir();
    stub_log("UPLAY_Init(id=%u, flags=%u) -> 1\n", uplay_id, flags);
    return 1;
}

EXPORT int UPLAY_StartCN(unsigned int uplay_id, unsigned int flags) {
    init_save_dir();
    stub_log("UPLAY_StartCN(id=%u, flags=%u) -> 1\n", uplay_id, flags);
    return 1;
}

EXPORT int UPLAY_Startup(unsigned int uplay_id, unsigned int flags) {
    init_save_dir();
    stub_log("UPLAY_Startup(id=%u, flags=%u) -> 1\n", uplay_id, flags);
    return 1;
}

EXPORT bool UPLAY_Release() {
    stub_log("UPLAY_Release()\n");
    return true;
}

EXPORT bool UPLAY_PeekNextEvent(void* event) {
    return false;
}

EXPORT bool UPLAY_SetGameSession(unsigned int appid, void* session, unsigned int flags) {
    stub_log("UPLAY_SetGameSession(appid=%u)\n", appid);
    return true;
}

EXPORT bool UPLAY_ClearGameSession() {
    stub_log("UPLAY_ClearGameSession()\n");
    return true;
}

EXPORT bool UPLAY_SetLanguage(const char* lang) {
    stub_log("UPLAY_SetLanguage(%s)\n", lang ? lang : "(null)");
    return true;
}

// ========== User (additional) ==========

EXPORT bool UPLAY_USER_IsConnected() {
    stub_log("UPLAY_USER_IsConnected() -> true\n");
    return true;
}

EXPORT void* UPLAY_USER_GetAccountId() {
    stub_log("UPLAY_USER_GetAccountId()\n");
    return nullptr;
}

EXPORT const char* UPLAY_USER_GetUsername() {
    stub_log("UPLAY_USER_GetUsername() -> %s\n", g_username);
    return g_username;
}

EXPORT const char* UPLAY_USER_GetUsernameUtf8() {
    stub_log("UPLAY_USER_GetUsernameUtf8() -> %s\n", g_username);
    return g_username;
}

EXPORT const char* UPLAY_USER_GetCdKeyUtf8() {
    stub_log("UPLAY_USER_GetCdKeyUtf8()\n");
    return "FC5MP-STUB-CDKEY";
}

EXPORT bool UPLAY_USER_GetCdKeys(void* out_list, UPLAY_OVERLAPPED* ov) {
    stub_log("UPLAY_USER_GetCdKeys()\n");
    if (out_list) *(void**)out_list = nullptr;
    complete(ov);
    return true;
}

EXPORT bool UPLAY_USER_ReleaseCdKeyList(void* list) {
    stub_log("UPLAY_USER_ReleaseCdKeyList()\n");
    return true;
}

struct UPLAY_USER_Credentials {
    const char* account_id;
    const char* username;
    const char* password;
    const char* ticket;
};

static UPLAY_USER_Credentials g_credentials = {
    g_account_id, g_username, "", g_ticket
};

EXPORT bool UPLAY_USER_GetCredentials(void* out_creds, UPLAY_OVERLAPPED* ov) {
    stub_log("UPLAY_USER_GetCredentials(out=%p)\n", out_creds);
    if (out_creds) *(UPLAY_USER_Credentials**)out_creds = &g_credentials;
    complete(ov);
    return true;
}

EXPORT const char* UPLAY_USER_GetEmail() {
    stub_log("UPLAY_USER_GetEmail()\n");
    return "fc5mp@local";
}

EXPORT const char* UPLAY_USER_GetEmailUtf8() {
    stub_log("UPLAY_USER_GetEmailUtf8()\n");
    return "fc5mp@local";
}

EXPORT const char* UPLAY_USER_GetPassword() {
    stub_log("UPLAY_USER_GetPassword()\n");
    return "";
}

EXPORT const char* UPLAY_USER_GetPasswordUtf8() {
    stub_log("UPLAY_USER_GetPasswordUtf8()\n");
    return "";
}

EXPORT bool UPLAY_USER_GetProfile(void* out_profile, UPLAY_OVERLAPPED* ov) {
    stub_log("UPLAY_USER_GetProfile()\n");
    if (out_profile) *(void**)out_profile = nullptr;
    complete(ov);
    return true;
}

EXPORT bool UPLAY_USER_ReleaseProfile(void* profile) {
    stub_log("UPLAY_USER_ReleaseProfile()\n");
    return true;
}

EXPORT bool UPLAY_USER_ConsumeItem(void* item_id, unsigned int quantity,
                                    void* out_result, UPLAY_OVERLAPPED* ov) {
    stub_log("UPLAY_USER_ConsumeItem()\n");
    complete(ov);
    return true;
}

EXPORT bool UPLAY_USER_ReleaseConsumeItemResult(void* result) {
    stub_log("UPLAY_USER_ReleaseConsumeItemResult()\n");
    return true;
}

EXPORT unsigned int UPLAY_USER_GetCPUScore() {
    stub_log("UPLAY_USER_GetCPUScore() -> 100\n");
    return 100;
}

EXPORT unsigned int UPLAY_USER_GetGPUScore() {
    stub_log("UPLAY_USER_GetGPUScore() -> 100\n");
    return 100;
}

EXPORT unsigned int UPLAY_USER_GetGPUScoreConfidenceLevel() {
    stub_log("UPLAY_USER_GetGPUScoreConfidenceLevel() -> 1\n");
    return 1;
}

// ========== Achievements (additional) ==========

EXPORT bool UPLAY_ACH_EarnAchievement(unsigned int id, UPLAY_OVERLAPPED* ov) {
    stub_log("UPLAY_ACH_EarnAchievement(id=%u)\n", id);
    complete(ov);
    return true;
}

EXPORT bool UPLAY_ACH_GetAchievements(unsigned int filter, const char* account,
                                       void* out_list, UPLAY_OVERLAPPED* ov) {
    stub_log("UPLAY_ACH_GetAchievements()\n");
    if (out_list) *(void**)out_list = nullptr;
    complete(ov);
    return true;
}

EXPORT bool UPLAY_ACH_ReleaseAchievementList(void* list) {
    stub_log("UPLAY_ACH_ReleaseAchievementList()\n");
    return true;
}

EXPORT bool UPLAY_ACH_GetAchievementImage(unsigned int id, void* out, UPLAY_OVERLAPPED* ov) {
    stub_log("UPLAY_ACH_GetAchievementImage(id=%u)\n", id);
    complete(ov);
    return true;
}

EXPORT bool UPLAY_ACH_ReleaseAchievementImage(void* img) {
    stub_log("UPLAY_ACH_ReleaseAchievementImage()\n");
    return true;
}

EXPORT bool UPLAY_ACH_Write(UPLAY_OVERLAPPED* ov) {
    stub_log("UPLAY_ACH_Write()\n");
    complete(ov);
    return true;
}

// ========== Avatar ==========

EXPORT bool UPLAY_AVATAR_Get(const char* acct, int size, void* out, UPLAY_OVERLAPPED* ov) {
    stub_log("UPLAY_AVATAR_Get()\n");
    complete(ov);
    return true;
}

EXPORT bool UPLAY_AVATAR_GetAvatarIdForCurrentUser(void* out_id) {
    stub_log("UPLAY_AVATAR_GetAvatarIdForCurrentUser()\n");
    return false;
}

EXPORT bool UPLAY_AVATAR_GetBitmap(unsigned int id, void* out, UPLAY_OVERLAPPED* ov) {
    stub_log("UPLAY_AVATAR_GetBitmap(id=%u)\n", id);
    complete(ov);
    return true;
}

EXPORT bool UPLAY_AVATAR_Release(void* avatar) {
    stub_log("UPLAY_AVATAR_Release()\n");
    return true;
}

// ========== Chat ==========

EXPORT bool UPLAY_CHAT_Init(unsigned int flags) {
    stub_log("UPLAY_CHAT_Init(flags=%u)\n", flags);
    return true;
}

EXPORT bool UPLAY_CHAT_GetHistory(void* out, UPLAY_OVERLAPPED* ov) {
    stub_log("UPLAY_CHAT_GetHistory()\n");
    complete(ov);
    return true;
}

EXPORT bool UPLAY_CHAT_ReleaseHistoryList(void* list) {
    stub_log("UPLAY_CHAT_ReleaseHistoryList()\n");
    return true;
}

EXPORT bool UPLAY_CHAT_SendMessage(const char* msg) {
    stub_log("UPLAY_CHAT_SendMessage()\n");
    return true;
}

EXPORT bool UPLAY_CHAT_SetMessagesRead(const char* account) {
    stub_log("UPLAY_CHAT_SetMessagesRead(account=%p)\n", account);
    return true;
}

// ========== Friends ==========

EXPORT bool UPLAY_FRIENDS_Init(unsigned int flags) {
    stub_log("UPLAY_FRIENDS_Init(flags=%u)\n", flags);
    return true;
}

EXPORT bool UPLAY_FRIENDS_GetFriendList(void* out_list, UPLAY_OVERLAPPED* ov) {
    stub_log("UPLAY_FRIENDS_GetFriendList()\n");
    if (out_list) *(void**)out_list = nullptr;
    complete(ov);
    return true;
}

EXPORT bool UPLAY_FRIENDS_AddPlayedWith(const char* account) {
    stub_log("UPLAY_FRIENDS_AddPlayedWith()\n");
    return true;
}

EXPORT bool UPLAY_FRIENDS_AddToBlackList(const char* account) { return true; }
EXPORT bool UPLAY_FRIENDS_RemoveFromBlackList(const char* account) { return true; }
EXPORT bool UPLAY_FRIENDS_IsBlackListed(const char* account) { return false; }
EXPORT bool UPLAY_FRIENDS_IsFriend(const char* account) { return false; }
EXPORT bool UPLAY_FRIENDS_RemoveFriendship(const char* account) { return true; }
EXPORT bool UPLAY_FRIENDS_RequestFriendship(const char* account) { return true; }
EXPORT bool UPLAY_FRIENDS_DisableFriendMenuItem(const char* account, int item) { return true; }
EXPORT bool UPLAY_FRIENDS_EnableFriendMenuItem(const char* account, int item) { return true; }
EXPORT bool UPLAY_FRIENDS_InviteToGame(const char* account) { return true; }
EXPORT bool UPLAY_FRIENDS_RespondToGameInvite(const char* account, bool accept) { return true; }
EXPORT bool UPLAY_FRIENDS_ShowFriendSelectionUI() { return true; }
EXPORT bool UPLAY_FRIENDS_ShowInviteFriendsToGameUI() { return true; }

// ========== Installer (additional) ==========

EXPORT bool UPLAY_INSTALLER_GetChunkIdsFromTag(const char* tag, void* out) {
    stub_log("UPLAY_INSTALLER_GetChunkIdsFromTag(tag=%s)\n", tag ? tag : "(null)");
    return true;
}

EXPORT bool UPLAY_INSTALLER_UpdateInstallOrder(void* chunk_ids, unsigned int count) {
    stub_log("UPLAY_INSTALLER_UpdateInstallOrder(count=%u)\n", count);
    return true;
}

// ========== Metadata ==========

EXPORT bool UPLAY_METADATA_SetSingleEventTag(const char* tag, const char* val) {
    stub_log("UPLAY_METADATA_SetSingleEventTag(tag=%s)\n", tag ? tag : "(null)");
    return true;
}

EXPORT bool UPLAY_METADATA_SetContinuousTag(const char* tag, const char* val) {
    stub_log("UPLAY_METADATA_SetContinuousTag(tag=%s)\n", tag ? tag : "(null)");
    return true;
}

EXPORT bool UPLAY_METADATA_ClearContinuousTag(const char* tag) {
    stub_log("UPLAY_METADATA_ClearContinuousTag(tag=%s)\n", tag ? tag : "(null)");
    return true;
}

// ========== Options ==========

EXPORT bool UPLAY_OPTIONS_Open(void* out_handle) {
    stub_log("UPLAY_OPTIONS_Open()\n");
    return true;
}

EXPORT bool UPLAY_OPTIONS_Close(void* handle) {
    stub_log("UPLAY_OPTIONS_Close()\n");
    return true;
}

EXPORT bool UPLAY_OPTIONS_Enumerate(void* handle, void* out_list) {
    stub_log("UPLAY_OPTIONS_Enumerate()\n");
    return true;
}

EXPORT bool UPLAY_OPTIONS_Get(void* handle, const char* key, void* out_val) {
    stub_log("UPLAY_OPTIONS_Get(key=%s)\n", key ? key : "(null)");
    return false;
}

EXPORT bool UPLAY_OPTIONS_Set(void* handle, const char* key, const char* val) {
    stub_log("UPLAY_OPTIONS_Set(key=%s)\n", key ? key : "(null)");
    return true;
}

EXPORT bool UPLAY_OPTIONS_Apply(void* handle) {
    stub_log("UPLAY_OPTIONS_Apply()\n");
    return true;
}

EXPORT bool UPLAY_OPTIONS_ReleaseKeyValueList(void* list) {
    stub_log("UPLAY_OPTIONS_ReleaseKeyValueList()\n");
    return true;
}

EXPORT bool UPLAY_OPTIONS_SetInGameState(bool in_game) {
    stub_log("UPLAY_OPTIONS_SetInGameState(%d)\n", in_game);
    return true;
}

// ========== Overlay ==========

EXPORT bool UPLAY_OVERLAY_SetShopUrl(const char* url, UPLAY_OVERLAPPED* ov) {
    stub_log("UPLAY_OVERLAY_SetShopUrl()\n");
    complete(ov);
    return true;
}

EXPORT bool UPLAY_OVERLAY_Show(void* section, UPLAY_OVERLAPPED* ov) {
    stub_log("UPLAY_OVERLAY_Show()\n");
    complete(ov);
    return true;
}

EXPORT bool UPLAY_OVERLAY_ShowBrowserUrl(const char* url) { return true; }
EXPORT bool UPLAY_OVERLAY_ShowFacebookAuthentication() { return true; }

EXPORT bool UPLAY_OVERLAY_ShowNotification(unsigned int notification_id) {
    stub_log("UPLAY_OVERLAY_ShowNotification(id=%u)\n", notification_id);
    return true;
}

EXPORT bool UPLAY_OVERLAY_ShowShopUrl(const char* url) { return true; }

EXPORT bool UPLAY_OVERLAY_ShowMicroApp(const char* app_id, UPLAY_OVERLAPPED* ov) {
    stub_log("UPLAY_OVERLAY_ShowMicroApp(app=%s)\n", app_id ? app_id : "(null)");
    complete(ov);
    return true;
}

// ========== Party ==========

EXPORT bool UPLAY_PARTY_Init(unsigned int flags) {
    stub_log("UPLAY_PARTY_Init(flags=%u)\n", flags);
    return true;
}

EXPORT bool UPLAY_PARTY_InviteToParty(const char* account) { return true; }
EXPORT bool UPLAY_PARTY_InvitePartyToGame() { return true; }
EXPORT bool UPLAY_PARTY_SetUserData(void* data, unsigned int size) { return true; }
EXPORT bool UPLAY_PARTY_SetGuest(const char* guest) { return true; }
EXPORT bool UPLAY_PARTY_DisablePartyMemberMenuItem(const char* account, int item) { return true; }
EXPORT bool UPLAY_PARTY_EnablePartyMemberMenuItem(const char* account, int item) { return true; }
EXPORT bool UPLAY_PARTY_ShowGameInviteOverlayUI() { return true; }
EXPORT bool UPLAY_PARTY_IsInParty() { return false; }
EXPORT bool UPLAY_PARTY_IsPartyLeader() { return true; }
EXPORT bool UPLAY_PARTY_PromoteToLeader(const char* account) { return true; }
EXPORT bool UPLAY_PARTY_RespondToGameInvite(const char* account, bool accept) { return true; }

EXPORT bool UPLAY_PARTY_GetId(void* out_id) {
    stub_log("UPLAY_PARTY_GetId()\n");
    if (out_id) *(void**)out_id = nullptr;
    return false;
}

EXPORT bool UPLAY_PARTY_GetFullMemberList(void* out_list) {
    stub_log("UPLAY_PARTY_GetFullMemberList()\n");
    if (out_list) *(void**)out_list = nullptr;
    return true;
}

EXPORT bool UPLAY_PARTY_GetInGameMemberList(void* out_list) {
    stub_log("UPLAY_PARTY_GetInGameMemberList()\n");
    if (out_list) *(void**)out_list = nullptr;
    return true;
}

// ========== Presence ==========

EXPORT bool UPLAY_PRESENCE_SetPresence(unsigned int presence_id, void* tokens) {
    stub_log("UPLAY_PRESENCE_SetPresence(id=%u)\n", presence_id);
    return true;
}

// ========== Product ==========

EXPORT bool UPLAY_PRODUCT_GetProductList(void* out_list, UPLAY_OVERLAPPED* ov) {
    stub_log("UPLAY_PRODUCT_GetProductList(out=%p)\n", out_list);
    if (out_list) *(void**)out_list = nullptr;
    complete(ov);
    return true;
}

EXPORT bool UPLAY_PRODUCT_ReleaseProductList(void* list) {
    stub_log("UPLAY_PRODUCT_ReleaseProductList(%p)\n", list);
    return true;
}

// ========== Store ==========

EXPORT bool UPLAY_STORE_IsEnabled() {
    stub_log("UPLAY_STORE_IsEnabled() -> false\n");
    return false;
}

EXPORT bool UPLAY_STORE_GetProducts(void* out_list, UPLAY_OVERLAPPED* ov) {
    stub_log("UPLAY_STORE_GetProducts()\n");
    if (out_list) *(void**)out_list = nullptr;
    complete(ov);
    return true;
}

EXPORT bool UPLAY_STORE_ReleaseProductsList(void* list) { return true; }
EXPORT bool UPLAY_STORE_GetPartner(void* out) { return false; }
EXPORT bool UPLAY_STORE_ShowProductDetails(unsigned int id) { return true; }
EXPORT bool UPLAY_STORE_ShowProducts(void* ids, unsigned int count) { return true; }
EXPORT bool UPLAY_STORE_Checkout(unsigned int id) { return true; }

// ========== Win (rewards/actions) ==========

EXPORT bool UPLAY_WIN_GetActions(void* out_list, UPLAY_OVERLAPPED* ov) {
    stub_log("UPLAY_WIN_GetActions()\n");
    if (out_list) *(void**)out_list = nullptr;
    complete(ov);
    return true;
}

EXPORT bool UPLAY_WIN_GetRewards(void* out_list, UPLAY_OVERLAPPED* ov) {
    stub_log("UPLAY_WIN_GetRewards()\n");
    if (out_list) *(void**)out_list = nullptr;
    complete(ov);
    return true;
}

EXPORT unsigned int UPLAY_WIN_GetUnitBalance() {
    stub_log("UPLAY_WIN_GetUnitBalance() -> 0\n");
    return 0;
}

EXPORT bool UPLAY_WIN_RefreshActions() {
    stub_log("UPLAY_WIN_RefreshActions()\n");
    return true;
}

EXPORT bool UPLAY_WIN_ReleaseActionList(void* list) { return true; }
EXPORT bool UPLAY_WIN_ReleaseRewardList(void* list) { return true; }

EXPORT bool UPLAY_WIN_SetActionsCompleted(void* actions, unsigned int count) {
    stub_log("UPLAY_WIN_SetActionsCompleted(count=%u)\n", count);
    return true;
}

// ========== Bloomberg bypass ==========
// FC_m64.dll's Bloomberg crash reporter has a busy-wait loop checking a flag.
// If the flag is never set (because we don't connect to Ubisoft services),
// a worker thread spins forever and may block other initialization.
// We set the flag directly to pretend Bloomberg is initialized.

static const uintptr_t BLOOMBERG_INIT_FLAG_OFF = 0x51662f8;  // .sbss byte flag
static const uintptr_t BLOOMBERG_INSTANCE_OFF  = 0x5166300;  // .sbss qword ptr
static const uintptr_t BLOOMBERG_FLAG2_OFF     = 0x5166308;  // .sbss byte flag

// The Bloomberg worker thread has a polling loop at offset 0x8068100 in FC_m64.dll.
// At offset 0x8068191 there's a `je` (0F 84 69 FF FF FF) that loops back to the
// top. If we NOP this 6-byte instruction, the thread executes the loop body once
// then falls through and returns, instead of spinning forever at ~100% CPU.
static const uintptr_t BLOOMBERG_LOOP_JE_OFF = 0x8068191;

static void bypass_bloomberg(HMODULE fc_base) {
    if (!fc_base) return;
    auto base = (uintptr_t)fc_base;

    // Set init flags so the main thread thinks Bloomberg is initialized
    auto* flag1 = (uint8_t*)(base + BLOOMBERG_INIT_FLAG_OFF);
    auto* flag2 = (uint8_t*)(base + BLOOMBERG_FLAG2_OFF);
    *flag1 = 1;
    *flag2 = 1;
    stub_log("BLOOMBERG: set init flags at +0x%llx and +0x%llx\n",
             (unsigned long long)BLOOMBERG_INIT_FLAG_OFF,
             (unsigned long long)BLOOMBERG_FLAG2_OFF);

    auto* patch_addr = (uint8_t*)(base + BLOOMBERG_LOOP_JE_OFF);

    // Verify the instruction bytes match what we expect
    if (patch_addr[0] == 0x0F && patch_addr[1] == 0x84 &&
        patch_addr[2] == 0x69 && patch_addr[3] == 0xFF &&
        patch_addr[4] == 0xFF && patch_addr[5] == 0xFF) {

        DWORD old_protect;
        if (VirtualProtect(patch_addr, 6, PAGE_EXECUTE_READWRITE, &old_protect)) {
            memset(patch_addr, 0x90, 6);
            VirtualProtect(patch_addr, 6, old_protect, &old_protect);
            stub_log("BLOOMBERG: patched loop back-edge at base+0x%llx (6 bytes NOP'd)\n",
                     (unsigned long long)BLOOMBERG_LOOP_JE_OFF);
        } else {
            stub_log("BLOOMBERG: VirtualProtect failed err=%u\n", GetLastError());
        }
    } else {
        stub_log("BLOOMBERG: unexpected bytes at loop je (0x%02x %02x %02x %02x %02x %02x), skipping patch\n",
                 patch_addr[0], patch_addr[1], patch_addr[2],
                 patch_addr[3], patch_addr[4], patch_addr[5]);
    }
}

// ========== Diagnostic thread ==========

static DWORD WINAPI heartbeat_thread(LPVOID) {
    for (int i = 0; i < 30; i++) {
        Sleep(500);
        stub_log("HEARTBEAT: tick %d (%dms)\n", i, (i + 1) * 500);
    }
    return 0;
}

static DWORD WINAPI diag_thread(LPVOID) {
    stub_log("DIAG: thread started\n");

    // Take two snapshots 5s apart to see if threads are progressing or stuck
    for (int snap_num = 1; snap_num <= 2; snap_num++) {
    int delay_ms = snap_num == 1 ? 500 : 3000;
    stub_log("DIAG: sleeping %dms for snapshot #%d\n", delay_ms, snap_num);
    Sleep(delay_ms);
    stub_log("\n=== DIAGNOSTIC: Thread IP snapshot #%d ===\n", snap_num);

    DWORD pid = GetCurrentProcessId();
    DWORD myTid = GetCurrentThreadId();
    stub_log("DIAG: pid=%u myTid=%u\n", pid, myTid);

    HANDLE snap = CreateToolhelp32Snapshot(0x00000004 /*TH32CS_SNAPTHREAD*/, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        stub_log("  CreateToolhelp32Snapshot failed err=%u\n", GetLastError());
        return 0;
    }
    stub_log("DIAG: snapshot ok\n");

    THREADENTRY32 te;
    te.dwSize = sizeof(te);
    int count = 0;
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid) continue;
            if (te.th32ThreadID == myTid) continue;
            count++;
            HANDLE ht = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
            if (!ht) {
                stub_log("  TID %5u  OpenThread failed err=%u\n", te.th32ThreadID, GetLastError());
                continue;
            }

            SuspendThread(ht);
            CONTEXT ctx;
            memset(&ctx, 0, sizeof(ctx));
            ctx.ContextFlags = 0x00100001 | 0x00100002; /* CONTEXT_CONTROL | CONTEXT_INTEGER */
            BOOL ok = GetThreadContext(ht, &ctx);
            if (ok) {
                char modname[256] = "???";
                MEMORY_BASIC_INFORMATION mbi;
                HMODULE mod = NULL;
                if (VirtualQuery((void*)ctx.Rip, &mbi, sizeof(mbi))) {
                    mod = (HMODULE)mbi.AllocationBase;
                    GetModuleFileNameA(mod, modname, sizeof(modname));
                }
                uintptr_t offset = ctx.Rip - (uintptr_t)mod;
                char* shortname = strrchr(modname, '\\');
                if (!shortname) shortname = modname; else shortname++;
                stub_log("  TID %5u  RIP=0x%llx  %s+0x%llx",
                         te.th32ThreadID, (unsigned long long)ctx.Rip,
                         shortname, (unsigned long long)offset);
                HMODULE fc_diag = GetModuleHandleA("FC_m64.dll");
                if (mod == fc_diag) {
                    stub_log("  RDI=0x%llx RAX=0x%llx RCX=0x%llx",
                             (unsigned long long)ctx.Rdi,
                             (unsigned long long)ctx.Rax,
                             (unsigned long long)ctx.Rcx);
                }
                stub_log("\n");
            } else {
                stub_log("  TID %5u  GetThreadContext failed err=%u\n", te.th32ThreadID, GetLastError());
            }
            ResumeThread(ht);
            CloseHandle(ht);
        } while (Thread32Next(snap, &te));
    } else {
        stub_log("  Thread32First failed err=%u\n", GetLastError());
    }
    CloseHandle(snap);
    stub_log("DIAG: enumerated %d threads\n", count);
    stub_log("=== END DIAGNOSTIC #%d ===\n", snap_num);
    } // end for snap_num
    return 0;
}

// ========== DllMain ==========

static DWORD WINAPI LoadClientDll(LPVOID param) {
    HINSTANCE h = (HINSTANCE)param;
    wchar_t path[512];
    GetModuleFileNameW(h, path, 511);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) wcscpy(slash + 1, L"fc5mp-client.dll");
    else wcscpy(path, L"fc5mp-client.dll");
    HMODULE client = LoadLibraryW(path);
    stub_log("LoadClientDll: fc5mp-client.dll -> %p\n", client);
    return 0;
}

static LONG WINAPI crash_filter(EXCEPTION_POINTERS* ex) {
    stub_log("!!! UNHANDLED EXCEPTION code=0x%08lx addr=%p !!!\n",
             ex->ExceptionRecord->ExceptionCode,
             ex->ExceptionRecord->ExceptionAddress);
    return EXCEPTION_CONTINUE_SEARCH;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        wchar_t dllpath[512];
        GetModuleFileNameW(h, dllpath, 511);
        wchar_t* slash = wcsrchr(dllpath, L'\\');
        if (slash) wcscpy(slash + 1, L"uplay_stub.log");
        else wcscpy(dllpath, L"uplay_stub.log");
        g_log = _wfopen(dllpath, L"w");
        stub_log("=== uplay_r1_loader64 stub loaded ===\n");

        SetUnhandledExceptionFilter(crash_filter);

        // Bloomberg bypass MUST run before any game code — FC_m64.dll is already
        // loaded (static import of FarCry5.exe) and its Bloomberg thread is
        // spinning. Patch it now, not after a sleep.
        HMODULE fc = GetModuleHandleA("FC_m64.dll");
        if (!fc) fc = GetModuleHandleA("FC_m64");
        if (fc) {
            bypass_bloomberg(fc);
        } else {
            stub_log("BLOOMBERG: FC_m64.dll not loaded yet!\n");
        }

        init_save_dir();
        stub_log("save_dir initialized\n");
        HANDLE t = CreateThread(NULL, 0, LoadClientDll, (LPVOID)h, 0, NULL);
        if (t) CloseHandle(t);
        CreateThread(NULL, 0, diag_thread, NULL, 0, NULL);
        CreateThread(NULL, 0, heartbeat_thread, NULL, 0, NULL);
    }
    if (reason == DLL_PROCESS_DETACH) {
        stub_log("=== uplay_r1_loader64 stub unloading ===\n");
        if (g_log) fclose(g_log);
    }
    return TRUE;
}

#endif

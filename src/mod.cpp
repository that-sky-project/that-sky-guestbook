// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 that-sky-project
//
// that-sky-guestbook - multi-slot Sky guest (Local) account manager.
//
// Sky.exe build 426499 facts used here:
//   AccountServerClient::AddRequest     VA 0x1419d6930
//   AccountLoginRequest vtable          VA 0x141f723e0
//   AccountLoginRequest.type offset     +0x68   (0 = Local, 12 = Steam)
//   login request auth fields: user +0x90 (16), device +0xa0 (16), key +0xc0 (32)
//   authinfo deserializer               VA 0x140e8bc30
//     target layout: user +0x00 (16), device +0x10 (16), key +0x20 (32)
//
// The client never persists Local authinfo by itself, so this mod stores the
// parsed authinfo per slot and injects it into the next Local login request.
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include "includes/htmod.h"
#include "imgui.h"

#define MAX_SLOTS 12
#define SLOT_NAME_MAX 32

static HMODULE gSelf = nullptr;
static uintptr_t gBase = 0;
static FILE *gLog = nullptr;

static void logf(const char *fmt, ...);
static bool readable(const void *ptr, size_t len);

struct Slot {
    char name[SLOT_NAME_MAX];
    unsigned char auth[64];
    bool valid;
};

static Slot gSlots[MAX_SLOTS];
static int  gSlotCount = 0;
static int  gSelected = -1;   // -1 = Steam main, -2 = new guest, >=0 = slot index
static int  gPending = -1;    // pending response target (-1 none, -2 new, >=0 slot)
static int  gLoginSeen = 0;
static int  gLastType = -1;
static bool gLastChanged = false;
static char gStatus[256] = "idle";
static char gDataDir[MAX_PATH] = {0};
static char gSlotsPath[MAX_PATH] = {0};
static char gLegacyAuthPath[MAX_PATH] = {0};
static char gSettingsPath[MAX_PATH] = {0};

static bool gChinese = false;
static bool gFontTried = false;
static ImFont *gCjkFont = nullptr;

static HTHandle gKeyToggle = nullptr;
static bool gShowWindow = true;
static bool gKeyWasDown = false;

static const char *T(const char *en, const char *zh) {
    return gChinese ? zh : en;
}

/* ---------------------------------------------------------------- logging */

static void logf(const char *fmt, ...) {
    if (!gLog) {
        char path[MAX_PATH];
        const char *tmp = getenv("TEMP");
        snprintf(path, sizeof(path), "%s\\that-sky-guestbook.log", tmp ? tmp : "C:\\Windows\\Temp");
        gLog = fopen(path, "a");
        if (!gLog) return;
    }
    va_list ap; va_start(ap, fmt);
    vfprintf(gLog, fmt, ap);
    va_end(ap);
    fflush(gLog);
}

static bool readable(const void *ptr, size_t len) {
    if (!ptr || !len) return false;
    uintptr_t start = (uintptr_t)ptr, end = start + len;
    while (start < end) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery((LPCVOID)start, &mbi, sizeof(mbi))) return false;
        if (mbi.State != MEM_COMMIT) return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
        start = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    }
    return true;
}

static bool allZero(const void *p, size_t n) {
    if (!readable(p, n)) return true;
    const unsigned char *b = (const unsigned char *)p;
    for (size_t i = 0; i < n; i++) if (b[i]) return false;
    return true;
}

static void log_stack(const char *tag) {
    void *frames[32];
    USHORT n = RtlCaptureStackBackTrace(0, 32, frames, nullptr);
    logf("  [%s] stack (%u):\n", tag, (unsigned)n);
    for (USHORT i = 0; i < n; i++) {
        uintptr_t a = (uintptr_t)frames[i];
        if (a >= gBase && a < gBase + 0x3100000)
            logf("    #%02u sky+0x%llx\n", i, (unsigned long long)(a - gBase));
        else
            logf("    #%02u %llx\n", i, (unsigned long long)a);
    }
}

/* -------------------------------------------------------------- slot file */

static void initPaths(void) {
    const char *app = getenv("APPDATA");
    snprintf(gDataDir, sizeof(gDataDir), "%s\\guest-login", app ? app : ".");
    CreateDirectoryA(gDataDir, nullptr);
    snprintf(gSlotsPath, sizeof(gSlotsPath), "%s\\slots.bin", gDataDir);
    snprintf(gLegacyAuthPath, sizeof(gLegacyAuthPath), "%s\\auth.bin", gDataDir);
    snprintf(gSettingsPath, sizeof(gSettingsPath), "%s\\settings.ini", gDataDir);
}

static void loadSettings(void) {
    if (!gSettingsPath[0]) initPaths();
    FILE *f = fopen(gSettingsPath, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "lang=zh")) gChinese = true;
        else if (strstr(line, "lang=en")) gChinese = false;
    }
    fclose(f);
}

static void saveSettings(void) {
    if (!gSettingsPath[0]) initPaths();
    FILE *f = fopen(gSettingsPath, "w");
    if (!f) return;
    fprintf(f, "lang=%s\n", gChinese ? "zh" : "en");
    fclose(f);
}

// ImGui 1.92 supports runtime font loading. Load a CJK font once so Chinese
// labels render instead of '?'.
static void ensureCjkFont(void) {
    if (gFontTried) return;
    gFontTried = true;
    ImGuiIO &io = ImGui::GetIO();
    const char *paths[] = {
        "C:\\Windows\\Fonts\\msyh.ttc",
        "C:\\Windows\\Fonts\\msyh.ttf",
        "C:\\Windows\\Fonts\\msyhbd.ttf",
        "C:\\Windows\\Fonts\\simhei.ttf",
        "C:\\Windows\\Fonts\\simsun.ttc",
        "C:\\Windows\\Fonts\\Deng.ttf",
    };
    for (const char *p : paths) {
        if (GetFileAttributesA(p) == INVALID_FILE_ATTRIBUTES) continue;
        ImFont *f = io.Fonts->AddFontFromFileTTF(
            p, 16.0f, nullptr,
            io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        if (f) {
            gCjkFont = f;
            logf("loaded CJK font %s\n", p);
            break;
        }
    }
    if (!gCjkFont) logf("no CJK font could be loaded\n");
}

static void saveSlots(void) {
    if (!gSlotsPath[0]) initPaths();
    FILE *f = fopen(gSlotsPath, "wb");
    if (!f) return;
    unsigned int magic = 0x544c5347; // "GSLT"
    int version = 1;
    fwrite(&magic, sizeof(magic), 1, f);
    fwrite(&version, sizeof(version), 1, f);
    fwrite(&gSlotCount, sizeof(gSlotCount), 1, f);
    fwrite(&gSelected, sizeof(gSelected), 1, f);
    for (int i = 0; i < gSlotCount; i++) {
        fwrite(gSlots[i].name, 1, SLOT_NAME_MAX, f);
        unsigned char v = gSlots[i].valid ? 1 : 0;
        fwrite(&v, 1, 1, f);
        fwrite(gSlots[i].auth, 1, sizeof(gSlots[i].auth), f);
    }
    fclose(f);
}

static void loadSlots(void) {
    if (!gSlotsPath[0]) initPaths();
    FILE *f = fopen(gSlotsPath, "rb");
    if (!f) {
        // Migrate the single auth.bin saved by the previous version.
        FILE *a = fopen(gLegacyAuthPath, "rb");
        if (a) {
            unsigned char blob[64];
            if (fread(blob, 1, sizeof(blob), a) == sizeof(blob)) {
                snprintf(gSlots[0].name, SLOT_NAME_MAX, "Guest 1");
                memcpy(gSlots[0].auth, blob, sizeof(blob));
                gSlots[0].valid = true;
                gSlotCount = 1;
                gSelected = 0;
                saveSlots();
                logf("migrated legacy auth.bin into slot 1\n");
            }
            fclose(a);
        }
        return;
    }
    unsigned int magic = 0;
    int version = 0;
    int count = 0, selected = -1;
    if (fread(&magic, sizeof(magic), 1, f) != 1 || magic != 0x544c5347) { fclose(f); return; }
    if (fread(&version, sizeof(version), 1, f) != 1 || version != 1) { fclose(f); return; }
    if (fread(&count, sizeof(count), 1, f) != 1) { fclose(f); return; }
    if (fread(&selected, sizeof(selected), 1, f) != 1) { fclose(f); return; }
    if (count < 0) count = 0;
    if (count > MAX_SLOTS) count = MAX_SLOTS;
    for (int i = 0; i < count; i++) {
        if (fread(gSlots[i].name, 1, SLOT_NAME_MAX, f) != SLOT_NAME_MAX) break;
        gSlots[i].name[SLOT_NAME_MAX - 1] = 0;
        unsigned char v = 0;
        if (fread(&v, 1, 1, f) != 1) break;
        if (fread(gSlots[i].auth, 1, sizeof(gSlots[i].auth), f) != sizeof(gSlots[i].auth)) break;
        gSlots[i].valid = v != 0;
        gSlotCount = i + 1;
    }
    fclose(f);
    if (selected >= gSlotCount) selected = gSlotCount ? 0 : -1;
    gSelected = selected;
    logf("loaded %d slot(s), selected=%d\n", gSlotCount, gSelected);
}

static int newSlotFromAuth(const unsigned char *auth) {
    if (gSlotCount >= MAX_SLOTS) return -1;
    int i = gSlotCount++;
    snprintf(gSlots[i].name, SLOT_NAME_MAX,
             gChinese ? "小号 %d" : "Guest %d", i + 1);
    memcpy(gSlots[i].auth, auth, sizeof(gSlots[i].auth));
    gSlots[i].valid = true;
    return i;
}

/* --------------------------------------------------------------- hooks */

// AccountServerClient::AddRequest(client, request) -> bool
typedef char (__fastcall *addreq_t)(void *, void *);
static addreq_t gOrigAddReq = nullptr;

static char __fastcall hook_addreq(void *client, void *req) {
    if (req && readable(req, 8)) {
        void *vt = *(void **)req;
        int type = -1;
        if (readable((char *)req + 0x68, 4))
            type = *(int *)((char *)req + 0x68);

        if (vt == (void *)(gBase + 0x1f723e0)) {
            gLoginSeen++;
            gLastType = type;
            gLastChanged = false;
            gPending = -1;

            logf("=== AddRequest(AccountLoginRequest) #%d type=%d selected=%d ===\n",
                 gLoginSeen, type, gSelected);
            log_stack("addreq_caller");
            logf("  fields: user=%d device=%d key_b0=%d key_c0=%d\n",
                 !allZero((char *)req + 0x90, 16),
                 !allZero((char *)req + 0xa0, 16),
                 !allZero((char *)req + 0xb0, 16),
                 !allZero((char *)req + 0xc0, 32));

            if (gSelected >= 0 && gSelected < gSlotCount && gSlots[gSelected].valid) {
                // Existing guest slot: force Local + inject saved authinfo.
                gPending = gSelected;
                if (readable((char *)req + 0x68, 4)) {
                    *(int *)((char *)req + 0x68) = 0;
                    gLastChanged = (type != 0);
                }
                memcpy((char *)req + 0x90, gSlots[gSelected].auth + 0x00, 16);
                memcpy((char *)req + 0xa0, gSlots[gSelected].auth + 0x10, 16);
                memcpy((char *)req + 0xc0, gSlots[gSelected].auth + 0x20, 32);
                logf("  -> slot %d: type 0 + injected authinfo\n", gSelected);
            } else if (gSelected == -2) {
                // New guest: force Local, leave authinfo empty.
                gPending = -2;
                if (readable((char *)req + 0x68, 4)) {
                    *(int *)((char *)req + 0x68) = 0;
                    gLastChanged = (type != 0);
                }
                logf("  -> new guest: type 0, no authinfo\n");
            } else {
                // Steam main: do nothing.
                logf("  -> main (Steam): unchanged\n");
            }
            const char *mode = gSelected == -1
                ? T("Main", "大号")
                : (gSelected == -2
                    ? T("New guest", "新建小号")
                    : (gSelected < gSlotCount ? gSlots[gSelected].name : "?"));
            snprintf(gStatus, sizeof(gStatus),
                     gChinese ? "登录 #%d  type=%d  模式=%s"
                              : "login #%d  type=%d  mode=%s",
                     gLoginSeen, type, mode);
        }
    }
    return gOrigAddReq ? gOrigAddReq(client, req) : 0;
}

// AccountLoginRequest::serialize(this, writer, obj)
typedef void (__fastcall *login_ser_t)(void *, void *, void *);
static login_ser_t gOrigLoginSer = nullptr;
static void __fastcall hook_login_ser(void *self, void *b, void *c) {
    if (self && readable((char *)self + 0x68, 4)) {
        int type = *(int *)((char *)self + 0x68);
        logf("=== serialize type=%d user=%d device=%d key_b0=%d key_c0=%d ===\n",
             type,
             !allZero((char *)self + 0x90, 16),
             !allZero((char *)self + 0xa0, 16),
             !allZero((char *)self + 0xb0, 16),
             !allZero((char *)self + 0xc0, 32));
    }
    if (gOrigLoginSer) gOrigLoginSer(self, b, c);
}

// authinfo deserializer: parse_auth(target, ctx, json_object)
typedef char (__fastcall *parse_auth_t)(void *, void *, void *);
static parse_auth_t gOrigParseAuth = nullptr;
static char __fastcall hook_parse_auth(void *target, void *ctx, void *json) {
    char r = gOrigParseAuth ? gOrigParseAuth(target, ctx, json) : 0;
    if (target && readable(target, 64) && !allZero(target, 64)) {
        if (gPending == -2) {
            int i = newSlotFromAuth((const unsigned char *)target);
            if (i >= 0) {
                gSelected = i;
                saveSlots();
                logf("=== created new guest slot %d (%s) ===\n", i, gSlots[i].name);
            }
        } else if (gPending >= 0 && gPending < gSlotCount) {
            memcpy(gSlots[gPending].auth, target, 64);
            gSlots[gPending].valid = true;
            saveSlots();
            logf("=== refreshed slot %d authinfo ===\n", gPending);
        }
    }
    gPending = -1;
    return r;
}

/* ------------------------------------------------------------------- ui */

static void drawPanel(void) {
    ensureCjkFont();

    // Always use the CJK font when available. It also contains Latin glyphs,
    // so English text stays readable while Chinese slot names / the 中文
    // button still render correctly in English mode.
    bool pushed = false;
    if (gCjkFont) {
        ImGui::PushFont(gCjkFont, 0.0f);
        pushed = true;
    }

    ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(T("That Sky Guestbook", "光遇账号簿"), &gShowWindow)) {
        ImGui::End();
        if (pushed) ImGui::PopFont();
        return;
    }

    if (ImGui::Button(gChinese ? "English" : "中文")) {
        gChinese = !gChinese;
        saveSettings();
    }
    ImGui::SameLine();
    if (ImGui::Button(T("Hide", "隐藏")))
        gShowWindow = false;
    ImGui::SameLine();
    const char *keyName = gKeyToggle
        ? HTHotkeyGetName(HTHotkeyBindGet(gKeyToggle))
        : "F3";
    ImGui::TextDisabled("%s: %s", T("hotkey", "快捷键"), keyName ? keyName : "?");
    if (gChinese && !gCjkFont)
        ImGui::SameLine(), ImGui::TextColored(ImVec4(1, 0.5f, 0.2f, 1),
            "%s", "未找到中文字体，中文可能显示为 ?");

    ImGui::Separator();
    ImGui::Text("%s: #%d  type=%d", T("Last login", "上次登录"), gLoginSeen, gLastType);
    ImGui::TextWrapped("%s", gStatus);
    ImGui::Separator();

    // Persist the selection immediately so "Main (Steam)" survives a restart.
    auto choose = [](int idx) {
        if (gSelected != idx) {
            gSelected = idx;
            saveSlots();
        }
    };

    const char *cur = T(" <- current", " <- 当前");
    if (ImGui::Selectable(T("Main (Steam)", "大号（Steam）"), gSelected == -1))
        choose(-1);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", gSelected == -1 ? cur : "");

    for (int i = 0; i < gSlotCount; i++) {
        char label[64];
        snprintf(label, sizeof(label), "%s##slot%d", gSlots[i].name, i);
        if (ImGui::Selectable(label, gSelected == i))
            choose(i);
        ImGui::SameLine();
        char del[48];
        snprintf(del, sizeof(del), "%s##del%d", T("Delete", "删除"), i);
        if (ImGui::SmallButton(del)) {
            for (int j = i; j + 1 < gSlotCount; j++) gSlots[j] = gSlots[j + 1];
            gSlotCount--;
            if (gSelected >= gSlotCount) gSelected = gSlotCount ? gSlotCount - 1 : -1;
            saveSlots();
            break;
        }
        if (gSelected == i) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", cur);
        }
    }

    if (ImGui::Selectable(T("New guest (create on next login)",
                            "新建小号（下次登录创建）"), gSelected == -2))
        choose(-2);

    ImGui::Separator();
    if (gSelected >= 0 && gSelected < gSlotCount) {
        ImGui::InputText(T("Name", "名称"), gSlots[gSelected].name, SLOT_NAME_MAX);
        if (ImGui::Button(T("Save name", "保存名称")))
            saveSlots();
    }
    if (ImGui::Button(T("Save slots", "保存槽位")))
        saveSlots();
    ImGui::SameLine();
    if (ImGui::Button(T("Reload slots", "重新加载槽位")))
        loadSlots();

    ImGui::Separator();
    ImGui::TextDisabled("%s", T("Select an account, then sign out / sign in (or restart and Play).",
                                "选择账号后，退出登录再重新登录（或重启后 Play）。"));
    ImGui::TextDisabled("%s", T("Main keeps Steam login; guest slots force type 0 and inject authinfo.",
                                "大号保持 Steam 登录；小号强制 type=0 并注入已保存的 authinfo。"));
    ImGui::TextDisabled("%s", T("Files: %APPDATA%\\guest-login\\slots.bin",
                                "文件：%APPDATA%\\guest-login\\slots.bin"));
    ImGui::End();

    if (pushed) ImGui::PopFont();
}

/* --------------------------------------------------------------- entry */

extern "C" __declspec(dllexport) HTStatus HTMLAPI HTModOnInit(void *reserved) {
    (void)reserved;
    HTGameStatus st = {0};
    HTGetGameStatus(&st);
    gBase = (uintptr_t)st.baseAddr;
    initPaths();
    loadSettings();
    loadSlots();
    logf("\n===== that-sky-guestbook init base=%llx pid=%lu slots=%d selected=%d lang=%s =====\n",
         (unsigned long long)gBase, (unsigned long)st.pid, gSlotCount, gSelected,
         gChinese ? "zh" : "en");

    void *fnAdd = (void *)(gBase + 0x19d6930);
    void *fnSer = (void *)(gBase + 0x1985f50);
    void *fnParse = (void *)(gBase + 0x0e8bc30);
    HTStatus s1 = HTAsmHookCreateRaw(gSelf, fnAdd, (void *)&hook_addreq, (void **)&gOrigAddReq);
    HTStatus s2 = HTAsmHookCreateRaw(gSelf, fnSer, (void *)&hook_login_ser, (void **)&gOrigLoginSer);
    HTStatus s3 = HTAsmHookCreateRaw(gSelf, fnParse, (void *)&hook_parse_auth, (void **)&gOrigParseAuth);
    HTStatus se = HTAsmHookEnable(gSelf, HT_ALL_HOOKS);
    logf("hook addreq=%d ser=%d parse=%d enableAll=%d\n", s1, s2, s3, se);
    if (se != HT_SUCCESS) {
        HTAsmHookEnable(gSelf, fnAdd);
        HTAsmHookEnable(gSelf, fnSer);
        HTAsmHookEnable(gSelf, fnParse);
        logf("individual enable attempted\n");
    }

    gKeyToggle = HTHotkeyRegister(gSelf, "Toggle That Sky Guestbook", HTKey_F3);
    logf("hotkey toggle handle=%p\n", (void *)gKeyToggle);
    return HT_SUCCESS;
}

extern "C" __declspec(dllexport) HTStatus HTMLAPI HTModOnEnable(void *reserved) {
    (void)reserved; return HT_SUCCESS;
}
extern "C" __declspec(dllexport) HTStatus HTMLAPI HTModOnDisable(void *reserved) {
    (void)reserved; return HT_SUCCESS;
}
extern "C" __declspec(dllexport) void HTMLAPI HTModRenderGui(float t, void *r) {
    (void)t; (void)r;
    if (gKeyToggle) {
        bool down = HTHotkeyPressed(gKeyToggle) != 0;
        if (down && !gKeyWasDown)
            gShowWindow = !gShowWindow;
        gKeyWasDown = down;
    }
    if (gShowWindow)
        drawPanel();
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) gSelf = h;
    return TRUE;
}

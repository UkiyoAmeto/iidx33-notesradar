// notesradar.c - beatmania IIDX 33 (LDJ) "Notes Radar" cap extension
//
// 1) 谱面上限：把 0x180827610 里 3 处 "jae -> 比值夹到 1.0" 改成 NOP，
//    折线最后一段继续外推，谱面雷达可 >200.00（选曲画面 / 副屏）。
// 2) 2dxtra 兼容：hook 2dxtra.dll 的雷达写回函数，写完取 max(旧, 新)，
//    避免它用 sqlite 里封顶过的历史值覆盖游戏本次算出的 >200 值。
// 3) 玩家雷达：上报 "radar_score" 时就地把 6 个 int 夹到 20000。
//
// 关键点：0x180AD72B0 这个函数会从 [rsp+0x80] 读第 5 个参数、并往
// [rsp+0x20]/[rsp+0x18]/[rsp+0x10] 写 shadow space，因此 detour 的过程中
// **绝对不能改变 rsp**。下面用一个手写汇编 thunk：原样保存/恢复 rcx..r9，
// 调完 C 函数后把 rsp 还原，再重放被覆盖的 4 条 mov，最后跳回 entry+20。

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// --- bm2dx build profiles ---------------------------------------------------
// 按 SizeOfImage 识别游戏版本，一个 DLL 同时支持 33 / 34。
typedef struct {
    uint32_t image_size;
    uint32_t cap[3];
    uint32_t addbinary;
    uint32_t recalc;
    uint32_t dx_writer;
    uint32_t dx_ret;
    uint32_t dx_sites[2];      // 2dxtra 写入函数的 call 点，0 = 结束
    const char *name;
} profile_t;

static const profile_t g_profiles[] = {
    // IIDX 33 Sparkle Shower, bm2dx.dll 18,467,992 bytes
    { 196694016u, { 0x827894u, 0x8279C1u, 0x827AEEu }, 0xAD72B0u, 0x952DD0u,
      0x8820u, 0x89A1u, { 0x919Bu, 0u }, "IIDX33" },
    // IIDX 34 ZINRAI 010, bm2dx.dll 18,200,216 bytes (2026-10-20)
    { 207613952u, { 0x813DE4u, 0x813F11u, 0x81403Eu }, 0xABD730u, 0x939F00u,
      0x9DB0u, 0x9F31u, { 0xA49Du, 0xA72Bu }, "IIDX34-010" },
    // IIDX 34 ZINRAI 012, bm2dx.dll 17,097,368 bytes (2026-09-16)
    { 206487552u, { 0x746BC4u, 0x746CF1u, 0x746E1Eu }, 0x9F07D0u, 0x86CDF0u,
      0x9DB0u, 0x9F31u, { 0xA49Du, 0xA72Bu }, "IIDX34-012" },
};

#define ADDBINARY_STOLEN  20            // 4 条 mov
#define PNR_CAP           20000
#define BUILD_TAG         "2026-10-22-dual"

static FILE *g_log;
static wchar_t g_dir[MAX_PATH];
static int g_enable_cap = 1;
static int g_enable_pnr = 1;
static int g_enable_2dx = 0;     // 0=不开 1=诊断(只记录) 2=实验(条件跳过)
static int g_2dx_mode = 0;
static int g_hit_addbinary, g_hit_2dx, g_hit_2dx2;
static int g_enable_recalc_key = 1;

static void logf_(const char *fmt, ...)
{
    if (!g_log) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

static int readable(const void *p, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!p) return 0;
    if (VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi)) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    return ((const uint8_t *)p + n) <= ((const uint8_t *)mbi.BaseAddress + mbi.RegionSize);
}

static uint8_t *alloc_near(uint8_t *base, size_t size)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t gran = si.dwAllocationGranularity;
    for (int dir = 0; dir < 2; dir++) {
        for (int i = 1; i <= 96; i++) {
            intptr_t delta = (intptr_t)i * 0x1000000;
            uintptr_t addr = (uintptr_t)((intptr_t)base + (dir ? -delta : delta));
            addr &= ~(gran - 1);
            void *p = VirtualAlloc((LPVOID)addr, size, MEM_COMMIT | MEM_RESERVE,
                                   PAGE_EXECUTE_READWRITE);
            if (p) return (uint8_t *)p;
        }
    }
    return NULL;
}

static void write_abs_jmp(uint8_t *p, void *target)
{
    p[0] = 0xFF; p[1] = 0x25; *(uint32_t *)(p + 2) = 0;
    *(uint64_t *)(p + 6) = (uint64_t)target;
}

// ---------------------------------------------------------------------------
// 1) 去掉谱面雷达 200.00 上限
// ---------------------------------------------------------------------------
static int patch_caps(uint8_t *base, const profile_t *p)
{
    static const uint8_t  expect[6] = { 0x73, 0x18, 0x0f, 0x2f, 0xd0, 0x72 };
    int done = 0;

    for (int i = 0; i < 3; i++) {
        uint8_t *site = base + p->cap[i];
        if (memcmp(site, expect, sizeof(expect)) != 0) {
            logf_("cap site %d signature mismatch, skipped", i);
            continue;
        }
        DWORD old;
        if (!VirtualProtect(site, 2, PAGE_EXECUTE_READWRITE, &old)) continue;
        site[0] = 0x90; site[1] = 0x90;
        VirtualProtect(site, 2, old, &old);
        FlushInstructionCache(GetCurrentProcess(), site, 2);
        done++;
    }
    logf_("cap removal: %d/3 sites patched", done);
    return done == 3;
}

// ---------------------------------------------------------------------------
// 2) 2dxtra 写入 hook：写完取 max(旧, 新)
// ---------------------------------------------------------------------------
typedef void (*radar_writer_fn)(void *music, int difficulty, void *data);
static radar_writer_fn g_2dx_orig;

static int slot_of(int difficulty)
{
    switch (difficulty) {
        case 3:  return 0;
        case 1:  return 1;
        case 0:  return 2;
        case 2:  return 3;
        case 4:  return 4;
        case 7:  return 6;
        case 6:  return 7;
        case 8:  return 8;
        case 10: return 9;
        default: return -1;
    }
}

static void (*g_2dx_entry)(void *, int, void *);
static uint8_t *g_2dx_base;

static void hook_2dx_writer(void *music, int difficulty, void *data)
{
    int32_t *dst = NULL;
    int slot = -1;
    difficulty &= 0xFF;          // 调用点只设置 dl，高位可能是垃圾

    if (!g_hit_2dx) {
        g_hit_2dx = 1;
        logf_("2dx: first call (music=%p diff=%d mode=%d)",
              music, difficulty, g_2dx_mode);
    }

    // mode 1 = 诊断：只记录，永远原样放行
    // mode 2 = 实验：只在"纯同步"时跳过 2dxtra 的写回
    if (g_2dx_mode >= 2 && music && data && readable((uint8_t *)music + 0x67C, 4)) {
        int id = *(int *)((uint8_t *)music + 0x67C);
        slot = slot_of(difficulty);
        if (id > 0 && id < 100000 && slot >= 0)
            dst = (int32_t *)((uint8_t *)music + 0x4FC + slot * 0x18);

        if (dst && readable(dst, 24) && readable((uint8_t *)data + 0x20, 4)) {
            // 这个函数是 notes + radar 一起写的。切 kichiku/kiraku/all-scratch
            // 时 notes 会变，必须放行让 notes 更新，否则会拿旧物量索引而崩。
            int music_notes = *(int *)((uint8_t *)music + 0x47C + slot * 4);
            int data_notes = *(int *)((uint8_t *)data + 0x20);

            if (music_notes == data_notes && music_notes > 0) {
                const int32_t *v = dst;
                int sane = 1, extended = 0;
                for (int i = 0; i < 6; i++) {
                    if (v[i] < 0 || v[i] > 100000) { sane = 0; break; }
                    if (v[i] > PNR_CAP) extended = 1;
                }
                if (sane && extended) {
                    if (!g_hit_2dx2) {
                        g_hit_2dx2 = 1;
                        logf_("2dx: skip sync (music=%p diff=%d notes=%d v0=%d)",
                              music, difficulty, music_notes, v[0]);
                    }
                    return;
                }
            }
        }
    }

    g_2dx_entry(music, difficulty, data);
}

// 调用点跳板：不改目标函数入口，只替换 "call 0x8820" 这一条 E8
//   sub rsp,8                      ; 保持 16 字节对齐
//   call [rip+0] -> hook_2dx_writer
//   add rsp,8
//   jmp  [rip+0] -> 调用点+5（原返回地址）
static uint8_t *build_call_thunk(void *hook, void *back)
{
    uint8_t *t = alloc_near(g_2dx_base, 0x100);
    if (!t) return NULL;

    // 2dxtra 这段调用方既依赖 r9/r10/r11，又**不保证 16 字节栈对齐**，
    // 所以这里做三件事：保存 r9/r10/r11、把 rsp 强制对齐、call 完原样还原。
    static const uint8_t code[] = {
        0x41,0x51,                              //  0: push r9
        0x41,0x52,                              //  2: push r10
        0x41,0x53,                              //  4: push r11
        0x49,0x89,0xE3,                         //  6: mov r11, rsp
        0x48,0x83,0xE4,0xF0,                    //  9: and rsp, -16
        0xFF,0x15,0x0F,0x00,0x00,0x00,          // 13: call [rip+0x0F] -> 34
        0x4C,0x89,0xDC,                         // 19: mov rsp, r11
        0x41,0x5B,                              // 22: pop r11
        0x41,0x5A,                              // 24: pop r10
        0x41,0x59,                              // 26: pop r9
        0xFF,0x25,0x08,0x00,0x00,0x00,          // 28: jmp  [rip+0x08] -> 42
    };
    memcpy(t, code, sizeof(code));
    *(uint64_t *)(t + 34) = (uint64_t)hook;     // 数据区，不在执行路径上
    *(uint64_t *)(t + 42) = (uint64_t)back;
    FlushInstructionCache(GetCurrentProcess(), t, 50);
    return t;
}

static int install_2dx_hook(const profile_t *prof)
{
    HMODULE mod = GetModuleHandleA("2dxtra.dll");
    if (!mod) { logf_("2dxtra.dll not loaded, skip"); return 0; }

    uint8_t *base = (uint8_t *)mod;
    g_2dx_base = base;
    uint8_t *entry = base + prof->dx_writer;
    static const uint8_t expect[9] = { 0x48,0x85,0xC9, 0x0F,0x84,0x78,0x01,0x00,0x00 };
    if (memcmp(entry, expect, 9) != 0) {
        logf_("2dxtra writer prologue mismatch, skip");
        return 0;
    }
    g_2dx_entry = (void (*)(void *, int, void *))entry;

    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    uint8_t *text = NULL;
    size_t text_size = 0;
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        if (memcmp(sec->Name, ".text", 5) == 0) {
            text = base + sec->VirtualAddress;
            text_size = sec->Misc.VirtualSize;
            break;
        }
    }
    if (!text) { logf_("2dxtra: no .text"); return 0; }

    // 只替换"已确认是真实 call 指令"的位置。
    // 之前扫描整个 .text 找 E8 目标，命中了 0x8F0D —— 那其实是数据里的字节，
    // 改掉以后 2dxtra 读自己的表就崩了。这里改成白名单 + 强校验。
    int patched = 0;
    for (int s = 0; s < 2 && prof->dx_sites[s]; s++) {
        uint32_t rva = prof->dx_sites[s];
        uint8_t *p = base + rva;
        if (p[0] != 0xE8) { logf_("2dxtra site %#x: not E8", rva); continue; }
        int32_t rel = *(int32_t *)(p + 1);
        if (p + 5 + rel != entry) { logf_("2dxtra site %#x: target mismatch", rva); continue; }

        uint8_t *back = p + 5;
        uint8_t *thunk = build_call_thunk((void *)&hook_2dx_writer, back);
        if (!thunk) continue;

        int32_t newrel = (int32_t)(thunk - back);
        DWORD old;
        if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old)) continue;
        *(int32_t *)(p + 1) = newrel;
        VirtualProtect(p, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), p, 5);
        patched++;
        logf_("2dxtra: call site %p -> thunk %p", p, thunk);
    }

    logf_("2dxtra: %d call site(s) patched", patched);
    return patched > 0;
}

// ---------------------------------------------------------------------------
// 3) 玩家雷达：上报时夹到 200.00（rsp-preserving 汇编 thunk）
// ---------------------------------------------------------------------------
static const char kRadarScore[] = "radar_score";

static void clamp_pnr(int32_t *v)
{
    for (int i = 0; i < 6; i++) {
        if (v[i] > PNR_CAP) v[i] = PNR_CAP;
        if (v[i] < 0)       v[i] = 0;
    }
}

// 由 thunk 以 (rcx = name, rdx = data) 调用
static void clamp_helper(const char *name, void *data)
{
    if (!g_enable_pnr || !name || !data) return;
    if (!readable(name, sizeof(kRadarScore))) return;
    if (strcmp(name, kRadarScore) != 0) return;
    if (!readable(data, 6 * sizeof(int32_t))) return;
    clamp_pnr((int32_t *)data);
    if (!g_hit_addbinary) {
        g_hit_addbinary = 1;
        logf_("addBinary radar_score first hit (data=%p)", data);
    }
}

// 生成 rsp 不变的 thunk：
//   sub rsp,0x58 / 保存 rcx..r9 / rcx=name,r8; rdx=data,r9 / call [rip+0]->helper
//   恢复 / add rsp,0x58 / 重放 entry 的 4 条 mov / mov rax,entry+20 / jmp rax
static uint8_t *build_addbinary_thunk(uint8_t *entry)
{
    uint8_t *t = (uint8_t *)VirtualAlloc(NULL, 0x200, MEM_COMMIT | MEM_RESERVE,
                                         PAGE_EXECUTE_READWRITE);
    if (!t) return NULL;

    static const uint8_t code[] = {
        0x48,0x83,0xEC,0x58,                    //  0: sub rsp,0x58
        0x48,0x89,0x4C,0x24,0x20,               //  4: mov [rsp+0x20],rcx
        0x48,0x89,0x54,0x24,0x28,               //  9: mov [rsp+0x28],rdx
        0x4C,0x89,0x44,0x24,0x30,               // 14: mov [rsp+0x30],r8
        0x4C,0x89,0x4C,0x24,0x38,               // 19: mov [rsp+0x38],r9
        0x4C,0x89,0xC1,                         // 24: mov rcx,r8
        0x4C,0x89,0xCA,                         // 27: mov rdx,r9
        0xFF,0x15,0x00,0x00,0x00,0x00,          // 30: call [rip+0]
        0,0,0,0,0,0,0,0,                        // 36:   -> helper（数据）
        0x48,0x8B,0x4C,0x24,0x20,               // 44: mov rcx,[rsp+0x20]
        0x48,0x8B,0x54,0x24,0x28,               // 49: mov rdx,[rsp+0x28]
        0x4C,0x8B,0x44,0x24,0x30,               // 54: mov r8,[rsp+0x30]
        0x4C,0x8B,0x4C,0x24,0x38,               // 59: mov r9,[rsp+0x38]
        0x48,0x83,0xC4,0x58,                    // 64: add rsp,0x58
        0x4C,0x89,0x4C,0x24,0x20,               // 68: mov [rsp+0x20],r9
        0x4C,0x89,0x44,0x24,0x18,               // 73: mov [rsp+0x18],r8
        0x48,0x89,0x54,0x24,0x10,               // 78: mov [rsp+0x10],rdx
        0x48,0x89,0x4C,0x24,0x08,               // 83: mov [rsp+8],rcx
        0x48,0xB8,0,0,0,0,0,0,0,0,              // 88: mov rax,entry+20
        0xFF,0xE0,                              // 98: jmp rax
    };
    memcpy(t, code, sizeof(code));

    // call [rip+0] 的地址字段：FF 15 在 30..35，下一条 = 36，目标地址放 36..43。
    // 这里 call 的返回地址就是 36，所以 36 后续必须是 "mov rcx,[rsp+0x20]"（偏移 44），
    // 即 36..43 必须是把执行流跳过去的指令，不能是纯数据。
    // 处理方式：在 36 放一条短跳 jmp +8 越过数据？
    // 更简单的做法见下：call 的返回地址落在 44 处，所以把 call 换成
    // "push 44; jmp helper" 不成立；因此改为在 36..43 放数据，
    // 但让 call 的目标直接在 44 继续——用 call 到一个"立即返回"的桩。
    // 最终采用：call [rip+0xC]（指向 48 处的数据），数据放在 108 之后。
    {
        // 用干净的布局重建：call 指向尾部数据区，返回地址 = 36 处真实指令
        uint8_t *p = t;
        p[0] = 0x48; p[1] = 0x83; p[2] = 0xEC; p[3] = 0x58;
        p[4] = 0x48; p[5] = 0x89; p[6] = 0x4C; p[7] = 0x24; p[8] = 0x20;
        p[9] = 0x48; p[10] = 0x89; p[11] = 0x54; p[12] = 0x24; p[13] = 0x28;
        p[14] = 0x4C; p[15] = 0x89; p[16] = 0x44; p[17] = 0x24; p[18] = 0x30;
        p[19] = 0x4C; p[20] = 0x89; p[21] = 0x4C; p[22] = 0x24; p[23] = 0x38;
        p[24] = 0x4C; p[25] = 0x89; p[26] = 0xC1;
        p[27] = 0x4C; p[28] = 0x89; p[29] = 0xCA;
        // call qword ptr [rip+0x40]  -> 36 + 0x40 = 100
        p[30] = 0xFF; p[31] = 0x15; p[32] = 0x40; p[33] = 0; p[34] = 0; p[35] = 0;
        p[36] = 0x48; p[37] = 0x8B; p[38] = 0x4C; p[39] = 0x24; p[40] = 0x20;
        p[41] = 0x48; p[42] = 0x8B; p[43] = 0x54; p[44] = 0x24; p[45] = 0x28;
        p[46] = 0x4C; p[47] = 0x8B; p[48] = 0x44; p[49] = 0x24; p[50] = 0x30;
        p[51] = 0x4C; p[52] = 0x8B; p[53] = 0x4C; p[54] = 0x24; p[55] = 0x38;
        p[56] = 0x48; p[57] = 0x83; p[58] = 0xC4; p[59] = 0x58;
        p[60] = 0x4C; p[61] = 0x89; p[62] = 0x4C; p[63] = 0x24; p[64] = 0x20;
        p[65] = 0x4C; p[66] = 0x89; p[67] = 0x44; p[68] = 0x24; p[69] = 0x18;
        p[70] = 0x48; p[71] = 0x89; p[72] = 0x54; p[73] = 0x24; p[74] = 0x10;
        p[75] = 0x48; p[76] = 0x89; p[77] = 0x4C; p[78] = 0x24; p[79] = 0x08;
        p[80] = 0x48; p[81] = 0xB8;                 // mov rax, imm64 (imm @ 82)
        p[90] = 0xFF; p[91] = 0xE0;                 // jmp rax
        *(uint64_t *)(p + 100) = (uint64_t)&clamp_helper;   // 数据区（不在执行路径）
        *(uint64_t *)(p + 82) = (uint64_t)(entry + ADDBINARY_STOLEN);

        FlushInstructionCache(GetCurrentProcess(), t, 116);
        return t;
    }
}

static int install_addbinary_hook(uint8_t *base, const profile_t *p)
{
    uint8_t *entry = base + p->addbinary;
    static const uint8_t expect[20] = {
        0x4c,0x89,0x4c,0x24,0x20,
        0x4c,0x89,0x44,0x24,0x18,
        0x48,0x89,0x54,0x24,0x10,
        0x48,0x89,0x4c,0x24,0x08,
    };
    if (memcmp(entry, expect, 20) != 0) {
        logf_("addBinary prologue mismatch, PNR clamp disabled");
        return 0;
    }

    uint8_t *thunk = build_addbinary_thunk(entry);
    if (!thunk) { logf_("addBinary thunk alloc failed"); return 0; }

    DWORD old;
    if (!VirtualProtect(entry, ADDBINARY_STOLEN, PAGE_EXECUTE_READWRITE, &old)) return 0;
    write_abs_jmp(entry, thunk);            // 14 bytes
    entry[14] = 0x90; entry[15] = 0x90; entry[16] = 0x90;
    entry[17] = 0x90; entry[18] = 0x90; entry[19] = 0x90;
    VirtualProtect(entry, ADDBINARY_STOLEN, old, &old);
    FlushInstructionCache(GetCurrentProcess(), entry, ADDBINARY_STOLEN);

    logf_("addBinary hook installed (thunk=%p entry=%p)", thunk, entry);
    return 1;
}

// ---------------------------------------------------------------------------
static void load_ini(void)
{
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%s\\notesradar.ini", g_dir);
    g_enable_cap = (int)GetPrivateProfileIntW(L"notesradar", L"EnableCap", 1, path);
    g_enable_pnr = (int)GetPrivateProfileIntW(L"notesradar", L"EnablePlayerCap", 1, path);
    g_2dx_mode = (int)GetPrivateProfileIntW(L"notesradar", L"Enable2dxCompat", 0, path);
    g_enable_2dx = (g_2dx_mode > 0) ? 1 : 0;
    g_enable_recalc_key = (int)GetPrivateProfileIntW(L"notesradar", L"EnableRecalcKey", 1, path);
}

// 游戏自己的"全曲雷达重算"入口（启动时用它把 6 个指标写进 music+0x4FC）。
// 手动触发一次，就能把 2dxtra 覆盖掉的封顶值重新算回游戏结果。
// 热键用 Ctrl+Alt+R，避开 spicecfg 占用的 F1~F12。
static void (*g_recalc)(void);

static int combo_recalc_pressed(void)
{
    return (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
           (GetAsyncKeyState(VK_MENU) & 0x8000) &&
           (GetAsyncKeyState('R') & 1);
}

static DWORD WINAPI hotkey_thread(LPVOID unused)
{
    (void)unused;
    for (;;) {
        Sleep(50);
        if (g_recalc && combo_recalc_pressed()) {
            logf_("Ctrl+Alt+R: recalc start");
            g_recalc();
            logf_("Ctrl+Alt+R: recalc done");
        }
    }
    return 0;
}

static void open_log(void)
{
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%s\\notesradar.log", g_dir);
    _wfopen_s(&g_log, path, L"w");
}

static DWORD WINAPI worker(LPVOID unused)
{
    (void)unused;
    HMODULE game = NULL;
    for (int i = 0; i < 600; i++) {
        game = GetModuleHandleA("bm2dx.dll");
        if (game) break;
        Sleep(100);
    }
    if (!game) { logf_("bm2dx.dll not found, aborting"); return 0; }

    uint8_t *base = (uint8_t *)game;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    uint32_t image_size = nt->OptionalHeader.SizeOfImage;

    const profile_t *prof = NULL;
    for (size_t i = 0; i < sizeof(g_profiles) / sizeof(g_profiles[0]); i++) {
        if (g_profiles[i].image_size == image_size) { prof = &g_profiles[i]; break; }
    }
    if (!prof) {
        logf_("unsupported bm2dx (SizeOfImage=%lu), nothing patched", image_size);
        return 0;
    }
    logf_("bm2dx profile %s matched (SizeOfImage=%lu)", prof->name, image_size);

    if (g_enable_cap) patch_caps(base, prof);
    if (g_enable_recalc_key) {
        g_recalc = (void (*)(void))(base + prof->recalc);
        HANDLE hk = CreateThread(NULL, 0, hotkey_thread, NULL, 0, NULL);
        if (hk) CloseHandle(hk);
        logf_("recalc hotkey Ctrl+Alt+R armed");
    }
    if (g_enable_pnr) install_addbinary_hook(base, prof);
    logf_("bm2dx stage done");

    if (g_enable_2dx) {
        HMODULE dx = GetModuleHandleA("2dxtra.dll");
        if (!dx) {
            // spice2x 的 -k 会在游戏初始化之前加载 2dxtra，此时 bm2dx 的
            // "+0x550 已初始化" 还是 0，它的 DllMain 必然失败（1114）。
            // 等游戏启动完成后再加载，让它看到一个已初始化的 bm2dx。
            logf_("2dxtra: not preloaded, waiting 35s for game init...");
            Sleep(35000);
            SetLastError(0);
            dx = LoadLibraryA("2dxtra.dll");
            logf_("2dxtra: delayed LoadLibrary -> %p (err=%lu)", dx, GetLastError());
        } else {
            logf_("2dxtra: already loaded by host (may be too early)");
        }
        if (dx) install_2dx_hook(prof);
    }
    logf_("all done");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        GetModuleFileNameW(inst, g_dir, MAX_PATH);
        wchar_t *slash = wcsrchr(g_dir, L'\\');
        if (slash) *slash = 0;
        open_log();
        load_ini();
        logf_("notesradar loaded (build " BUILD_TAG "; cap=%d pnr=%d 2dx=%d)",
          g_enable_cap, g_enable_pnr, g_enable_2dx);
        HANDLE h = CreateThread(NULL, 0, worker, NULL, 0, NULL);
        if (h) CloseHandle(h);
    } else if (reason == DLL_PROCESS_DETACH) {
        if (g_log) { fclose(g_log); g_log = NULL; }
    }
    return TRUE;
}

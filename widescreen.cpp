// widescreen.cpp  -  SimTractor 3.5 (V3.66f): widescreen with resolution set in an .ini file  (.asi plugin)
//
// Requires ORIGINAL (not patched in HxD) SimTractor.exe and RenderEngines\CK2_3D.dll
// (or CK2_3D.dll in the game folder - wherever the game keeps it). Everything is done in memory, no files are changed.
//
// What it does:
//   1. Sets the fullscreen and window resolution (fields of the game settings struct 0x40A078+0x58/0x5C/0x68/0x6C)
//      from widescreen.ini, and resizes the main game window to match.
//   2. Disables FUN_00402B70 in the exe (applies the display mode from the menu settings; same patch as before: 83 EC 10 -> 33 C0 C3),
//      so the game does not revert the resolution.
//   3. CK2_3D.dll: removes the black bars (JZ -> JMP at RVA 0x162CB).
//   v2: text of elements with children (Session/Chat/Players Frame in the multiplayer lobby) is moved together with the element.
//   4. CK2_3D.dll: "Hor+" FOV (replaces FUN_10005F90 - projection matrix): vertical FOV stays as in 4:3, horizontal gets wider.
//
// Build (x86 Native Tools Command Prompt for VS):
//   cl /nologo /LD /O2 /MT widescreen.cpp /Fe:widescreen.asi user32.lib

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#pragma comment(lib, "user32.lib")

// --- addresses / signatures (SimTractor V3.66f) ---
static const DWORD EXE_STAMP   = 0x421651FD;     // IMAGE_FILE_HEADER.TimeDateStamp SimTractor.exe
static const DWORD CK2_3D_STAMP= 0x4315F7F0;     // same for CK2_3D.dll
static const DWORD SETTINGS    = 0x0040A078;     // game settings struct (DAT_0040a078)
static const DWORD FN_APPLYMODE= 0x00402B70;     // FUN_00402b70
static const DWORD RVA_LETTERBOX = 0x162CB;      // CK2_3D: JZ (74) -> JMP (EB)
static const DWORD RVA_PROJ      = 0x5F90;       // CK2_3D: FUN_10005f90 (projection matrix)
static const DWORD RVA_PARENTRECT= 0x8C500;      // CK2_3D: FUN_1008c500 (parent rect of a 2D element)
static const DWORD RVA_PREPARENT = 0x8C5A0;      // CK2_3D: FUN_1008c5a0
static const DWORD RVA_GETPLAYERRC = 0x9249A;    // CK2_3D: FUN_1009249a (CKContext::GetPlayerRenderContext)
static const DWORD RVA_RECTCALC  = 0x8CA40;      // CK2_3D: FUN_1008ca40 (computes the on-screen rect of a 2D element: fields +0x54..+0x60)
static const DWORD RVA_RECTCALL  = 0x8D5E7;      // CK2_3D: the only call to FUN_1008ca40 (in FUN_1008d580), E8 rel32

static char g_dir[MAX_PATH], g_ini[MAX_PATH], g_log[MAX_PATH];
static int  g_w = 1920, g_h = 1080, g_ww = 0, g_wh = 0;
static bool g_letterbox = true, g_horPlus = true, g_logOn = true;
static float g_refAspect = 4.0f / 3.0f;
static float g_uiAspect  = 4.0f / 3.0f;          // aspect of the area the menu/HUD is laid out in (0 = off)
static char  g_anchor[32][64];                   // names of 2D elements that stay at the left edge (ini [UI] LeftAnchor)
static int   g_anchorN   = 0;
static char  g_right[16][64];                    // 2D elements pinned to the right screen edge (ini [UI] RightAnchor)
static int   g_rightN    = 0;
static char  g_txtR[16][64];                     // elements whose TEXT moves to the right edge (ini [UI] TextRight)
static int   g_txtRN     = 0;
static char  g_cs[8][64];                        // elements centered on the SCREEN (ini [UI] CenterScreen), e.g. the Loading box
static int   g_csN       = 0;
static float g_csDelta   = 0.0f, g_csDy = 0.0f;
static bool  g_menuList  = false;                // EXPERIMENTAL: machine selection menu column centered too (clicking gets misaligned)
static char  g_tsL[24][64]; static int g_tsN = 0; static float g_tsMul = 1.0f; static float g_tsY = 1.0f; static bool g_tsRel = false; static float g_tsDx = 0.0f;  // ini [UI] TextScaleList / TextScale
static bool  g_uiCenter  = true;                 // true = menu/HUD area centered; false = at the left edge
static bool  g_logUi     = false;                // [Debug] LogUi=1 - 2D element diagnostics
static BYTE* g_engBase   = NULL;

static void Log(const char* fmt, ...)
{
    if (!g_logOn) return;
    FILE* f = NULL;
    fopen_s(&f, g_log, "a");
    if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(f, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static bool WriteMem(void* addr, const void* data, size_t n)
{
    DWORD old;
    if (!VirtualProtect(addr, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(addr, data, n);
    VirtualProtect(addr, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), addr, n);
    return true;
}

static DWORD PeStamp(HMODULE m)
{
    IMAGE_DOS_HEADER* d = (IMAGE_DOS_HEADER*)m;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)((BYTE*)m + d->e_lfanew);
    return nt->FileHeader.TimeDateStamp;
}

// ------------------------------------------------------------------ config
static void WriteDefaultIni()
{
    FILE* f = NULL;
    fopen_s(&f, g_ini, "w");
    if (!f) return;
    fputs(
"; widescreen.ini - widescreen fix for SimTractor 3.5 (V3.66f)\n"
"; Changes take effect after restarting the game.\n"
"; Requires ORIGINAL game files (no earlier HxD patches).\n"
"\n"
"[Display]\n"
"Width=1920            ; fullscreen resolution\n"
"Height=1080\n"
"WindowWidth=0         ; window size in windowed mode (0 = same as Width)\n"
"WindowHeight=0        ; (0 = same as Height)\n"
"\n"
"[Camera]\n"
"HorPlus=1             ; 1 = Hor+ FOV (wider view on the sides, no stretching); 0 = original FOV\n"
"RemoveLetterbox=1     ; 1 = remove the black bars on the sides\n"
"ReferenceAspect=1.3333; aspect the game was designed for (4:3)\n"
"\n"
"[UI]\n"
"; Menu and HUD (2D elements) are laid out in an area with this aspect (at full screen height),\n"
"; so they do not overlap on a wide screen. 0 = off (original stretch over the whole screen).\n"
"UiAspect=1.3333\n"
"; UiCenter: centers the menu and HUD (graphics, icons, text). The machine selection menu column stays at the left edge (so clicking works correctly).\n"
"; LeftAnchor: menu element names (comma separated) that stay at the left edge, like the left column. LogUi=1 prints the names.\n"
"LeftAnchor=menuInfo,menuInfo.Copy,lobby,menu2,New Object\n"
"; RightAnchor: elements pinned to the RIGHT screen edge (e.g. icons in the top right corner). TextRight: only the element's text moves to the right edge.\n"
"RightAnchor=verrou.Copy,cam+.Copy.Copy,Alarm,verrou\n"
"TextRight=menuInfo.Copy\n"
"; CenterScreen: elements centered on the screen (Loading box); the first one in the list is the reference.\n"
"CenterScreen=Loadmini,LoadMaxi\n"
"; TextScale: 2D text scale multiplier (chat, labels); size = screen_height/480 * TextScale; 0 = off. TextScaleList: elements whose text gets scaled.\n"
"TextScale=1\n"
"TextScaleList=Title Bar Frame*0.45@0.8^1.2,Chat Message Frame*0.5@0.77^1.5,Chat Input Frame*0.4@0.77^1.3,Create 2D Frame,Create 2D Frame.Copy,Create 2D Frame.Copy.Copy,Session Frame,Session Player Count Frame,Player Frame\n"
"CenterMenuList=0      ; EXPERIMENTAL: 1 = center that column too, but clicking in it gets misaligned (not recommended)\n"
"UiCenter=1            ; 1 = menu/HUD area centered on screen, 0 = at the left edge\n"
"\n"
"[Debug]\n"
"Log=1                 ; 1 = write widescreen.log\n"
"LogUi=0               ; 1 = diagnostics: logs menu/HUD 2D elements (for bug hunting)\n"
, f);
    fclose(f);
}

static int IniI(const char* sec, const char* key, int def)
{
    return (int)GetPrivateProfileIntA(sec, key, def, g_ini);
}
static int ParseList(const char* sec, const char* key, const char* def, char out[][64], int maxn)
{
    char b[1024];
    GetPrivateProfileStringA(sec, key, def, b, sizeof(b), g_ini);
    int cnt = 0;
    for (char* p = b; *p && cnt < maxn; ) {
        char* e = strchr(p, ',');
        if (e) *e = 0;
        while (*p == ' ') p++;
        size_t n = strlen(p);
        while (n && p[n - 1] == ' ') p[--n] = 0;
        if (n && n < 64) strcpy_s(out[cnt++], 64, p);
        if (!e) break;
        p = e + 1;
    }
    return cnt;
}
static bool InList(const char* nm, char l[][64], int n)
{
    if (!nm) return false;
    for (int i = 0; i < n; i++) if (!strcmp(nm, l[i])) return true;
    return false;
}

static float IniF(const char* sec, const char* key, float def)
{
    char b[64];
    GetPrivateProfileStringA(sec, key, "", b, sizeof(b), g_ini);
    return b[0] ? (float)atof(b) : def;
}

static void LoadConfig()
{
    if (GetFileAttributesA(g_ini) == INVALID_FILE_ATTRIBUTES) WriteDefaultIni();
    g_w  = IniI("Display", "Width", 1920);
    g_h  = IniI("Display", "Height", 1080);
    g_ww = IniI("Display", "WindowWidth", 0);
    g_wh = IniI("Display", "WindowHeight", 0);
    if (g_w  < 320 || g_w  > 16384) g_w  = 1920;
    if (g_h  < 240 || g_h  > 16384) g_h  = 1080;
    if (g_ww < 320 || g_ww > 16384) g_ww = g_w;
    if (g_wh < 240 || g_wh > 16384) g_wh = g_h;
    g_horPlus   = IniI("Camera", "HorPlus", 1) != 0;
    g_letterbox = IniI("Camera", "RemoveLetterbox", 1) != 0;
    g_refAspect = IniF("Camera", "ReferenceAspect", 4.0f / 3.0f);
    if (g_refAspect < 0.5f || g_refAspect > 4.0f) g_refAspect = 4.0f / 3.0f;
    g_uiAspect  = IniF("UI", "UiAspect", 4.0f / 3.0f);
    if (g_uiAspect != 0.0f && (g_uiAspect < 0.5f || g_uiAspect > 4.0f)) g_uiAspect = 4.0f / 3.0f;
    g_uiCenter  = IniI("UI", "UiCenter", 1) != 0;
    g_menuList  = IniI("UI", "CenterMenuList", 0) != 0;
    g_rightN    = ParseList("UI", "RightAnchor", "verrou.Copy,cam+.Copy.Copy,Alarm,verrou", g_right, 16);
    g_txtRN     = ParseList("UI", "TextRight", "menuInfo.Copy", g_txtR, 16);
    g_tsN       = ParseList("UI", "TextScaleList", "Title Bar Frame*0.45@0.8^1.2,Chat Message Frame*0.5@0.77^1.5,Chat Input Frame*0.4@0.77^1.3,Create 2D Frame,Create 2D Frame.Copy,Create 2D Frame.Copy.Copy,Session Frame*0.6@0.8,Session Player Count Frame*0.6@0.8,Player Frame*0.4@0.8^1.3,Sessions Info 2D Frame=0.85,Players Info 2D Frame=0.85>1.5", g_tsL, 24);
    g_tsMul     = IniF("UI", "TextScale", 1.0f);
    for (int q = 0; q < g_tsN; q++) Log("TextScaleList[%d] = '%s'", q, g_tsL[q]);
    g_csN       = ParseList("UI", "CenterScreen", "Loadmini,LoadMaxi", g_cs, 8);
    {
        char b[1024];
        GetPrivateProfileStringA("UI", "LeftAnchor", "menuInfo,menuInfo.Copy,lobby,menu2,New Object", b, sizeof(b), g_ini);
        g_anchorN = 0;
        for (char* p = b; *p && g_anchorN < 32; ) {
            char* e = strchr(p, ',');
            if (e) *e = 0;
            while (*p == ' ') p++;
            size_t n = strlen(p);
            while (n && p[n - 1] == ' ') p[--n] = 0;
            if (n && n < 64) strcpy_s(g_anchor[g_anchorN++], 64, p);
            if (!e) break;
            p = e + 1;
        }
    }
    g_logUi     = IniI("Debug", "LogUi", 0) != 0;
    g_logOn     = IniI("Debug", "Log", 1) != 0;
}

// ------------------------------------------------------------------ FOV Hor+
// Replacement for FUN_10005f90 (thiscall: ecx = 4x4 matrix, stack args: fov, aspect, znear, zfar; ret 0x10).
// Original:  m00 = cot(fov/2),  m11 = m00 * aspect.   Hor+:  m11 = cot(fov/2) * refAspect,  m00 = m11 / aspect.
// For aspect == refAspect the result is identical to the original.
static int __fastcall HookProj(float* m, void* /*edx*/, float fov, float aspect, float zn, float zf)
{
    memset(m, 0, 16 * sizeof(float));
    float h = fov * 0.5f;
    float x = cosf(h) / sinf(h);
    float k = g_refAspect;
    if (aspect <= 0.0001f) aspect = k;
    m[5]  = x * k;               // [1][1]
    m[0]  = x * k / aspect;      // [0][0]
    float q = zf / (zf - zn);
    m[10] = q;                   // [2][2]
    m[11] = 1.0f;                // [2][3]
    m[14] = -(zn * q);           // [3][2]
    return 0;
}

// ------------------------------------------------------------------ menu/HUD layout (2D)
// Replacement for FUN_1008c500 (thiscall: ecx = 2D element, arg: float rect[4], ret 4) - "parent rect".
// 1:1 copy of the original; the only change: for elements without a parent the area width is limited to height * UiAspect.
typedef void* (__thiscall *FnCtx)(void* ctx);
typedef void* (__thiscall *FnV0)(void* self);
typedef void  (__thiscall *FnV1)(void* self, float* r);
typedef void  (__thiscall *FnV2)(void* self, float* r, int a);

static void __fastcall HookParentRect(void* self, void* /*edx*/, float* r)
{
    void** vt = *(void***)self;
    void* ctx = *(void**)((BYTE*)self + 0x10);
    void* rc  = ((FnCtx)(g_engBase + RVA_GETPLAYERRC))(ctx);
    void* parent = ((FnV0)vt[0xE8 / 4])(self);
    if (parent) {
        ((FnV0)(g_engBase + RVA_PREPARENT))(parent);
        ((FnV1)(*(void***)parent)[0x9C / 4])(parent, r);
        return;
    }
    int bg = (int)(INT_PTR)((FnV0)vt[0xE0 / 4])(self);
    if (rc) {
        void** rv = *(void***)rc;
        if (!bg) ((FnV2)rv[0xCC / 4])(rc, r, 0);
        else     ((FnV1)rv[0xE0 / 4])(rc, r);
    }
    float l = r[0], t = r[1];
    r[0] = 0.0f; r[1] = 0.0f; r[2] -= l; r[3] -= t;
    if (g_uiAspect > 0.0f) {
        float w = r[3] * g_uiAspect;
        if (r[2] > w) r[2] = w;
    }
}

// Centering: after the on-screen rect of a 2D element is computed (fields +0x54 left, +0x5C right) we shift it by 'off' pixels.
// The same rect is used for drawing and for click testing (CKRenderContext 2D pick), so the mouse needs no extra handling.
// Children are clipped to the parent rect - while a child is computed we take the offset off the parent, because the child's raw coords are not shifted.
typedef int (__thiscall *FnRectCalc)(void* self, int rc);
typedef const char* (__thiscall *FnGetName)(void* obj);
static FnGetName g_getName = NULL;
static void* g_seenEnt[600]; static int g_seenN = 0;

static void ModOf(void* fn, char* out, size_t n)
{
    HMODULE hm = NULL;
    out[0] = 0;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)fn, &hm) && hm) {
        char full[MAX_PATH]; GetModuleFileNameA(hm, full, MAX_PATH);
        const char* b = strrchr(full, '\\'); b = b ? b + 1 : full;
        _snprintf_s(out, n, _TRUNCATE, "%s+%lX", b, (unsigned long)((BYTE*)fn - (BYTE*)hm));
    } else _snprintf_s(out, n, _TRUNCATE, "%p", fn);
}

// entity render callbacks (+0x3C): pre [0..1], main [3], post [4..5]; entry = {fn, arg, ?}
static bool CbSeen(void* e, DWORD fn, int k)
{
    static struct { void* e; DWORD fn; int k; } seen[800]; static int n = 0;
    for (int i = 0; i < n; i++) if (seen[i].e == e && seen[i].fn == fn && seen[i].k == k) return true;
    if (n < 800) { seen[n].e = e; seen[n].fn = fn; seen[n].k = k; n++; }
    return false;
}

// entity render callbacks (+0x3C): pre [0..1], main [3], post [4..5]; entry = {fn, arg, ?}
// called on every render, but logs each (entity, function) only once - also catches callbacks added later
static void LogCallbacks(void* self)
{
    __try {
        BYTE* cb = *(BYTE**)((BYTE*)self + 0x3C);
        if (!cb) return;
        const char* nm = "?";
        if (g_getName) { __try { nm = g_getName(self); if (!nm) nm = "(null)"; } __except (1) { nm = "?"; } }
        char m[200];
        DWORD* b0 = *(DWORD**)(cb + 0);  DWORD* e0 = *(DWORD**)(cb + 4);
        for (DWORD* p = b0; p && p < e0; p += 3) if (!CbSeen(self, p[0], 0)) { ModOf((void*)p[0], m, sizeof m); Log("UI-CB: '%s' ent=%p PRE  %s arg=%08lX", nm, self, m, (unsigned long)p[1]); }
        DWORD* mn = *(DWORD**)(cb + 0xC);
        if (mn && !CbSeen(self, mn[0], 1)) { ModOf((void*)mn[0], m, sizeof m); Log("UI-CB: '%s' ent=%p MAIN %s arg=%08lX", nm, self, m, (unsigned long)mn[1]); }
        DWORD* b1 = *(DWORD**)(cb + 0x10); DWORD* e1 = *(DWORD**)(cb + 0x14);
        for (DWORD* p = b1; p && p < e1; p += 3) if (!CbSeen(self, p[0], 2)) { ModOf((void*)p[0], m, sizeof m); Log("UI-CB: '%s' ent=%p POST %s arg=%08lX", nm, self, m, (unsigned long)p[1]); }
    } __except (1) {}
}

static void LogEntity(void* self, int rc, int r, float offApplied)
{
    for (int i = 0; i < g_seenN; i++) if (g_seenEnt[i] == self) return;
    if (g_seenN >= 600) return;
    g_seenEnt[g_seenN++] = self;
    BYTE* e = (BYTE*)self;
    const char* nm = "?";
    if (g_getName) { __try { nm = g_getName(self); if (!nm) nm = "(null)"; } __except (1) { nm = "?"; } }
    Log("UI: ent=%p vt=%08lX name='%s' flags=%08lX parent=%p raw=(%.0f,%.0f,%.0f,%.0f) scr=(%.0f,%.0f,%.0f,%.0f) r=%d off=%.0f rc=%dx%d",
        self, (unsigned long)*(DWORD*)e, nm, (unsigned long)*(DWORD*)(e + 0x74), *(void**)(e + 0x78),
        *(float*)(e + 0x40), *(float*)(e + 0x44), *(float*)(e + 0x48), *(float*)(e + 0x4C),
        *(float*)(e + 0x54), *(float*)(e + 0x58), *(float*)(e + 0x5C), *(float*)(e + 0x60),
        r, offApplied, *(int*)(rc + 0x1AC), *(int*)(rc + 0x1B0));
}

struct RawSave { BYTE* ent; float x0, x2, off, y0, y2; };
static RawSave g_rs[64]; static int g_rsN = 0;
typedef int (__thiscall *FnRender)(void* self, int rc);
static FnRender g_origRender = NULL, g_origRenderTxt = NULL;

static void RestoreRaw(void* self)
{
    while (g_rsN > 0 && g_rs[g_rsN - 1].ent == (BYTE*)self) {
        g_rsN--;
        *(float*)(g_rs[g_rsN].ent + 0x40) = g_rs[g_rsN].x0;
        *(float*)(g_rs[g_rsN].ent + 0x48) = g_rs[g_rsN].x2;
        *(float*)(g_rs[g_rsN].ent + 0x44) = g_rs[g_rsN].y0;
        *(float*)(g_rs[g_rsN].ent + 0x4C) = g_rs[g_rsN].y2;
    }
}
// Wrappers for the Render method (vtable +0x110) of 2D classes: restore the element's raw coords after it is drawn.
static int __fastcall RenderWrap(void* self, void*, int rc)    { int r = g_origRender(self, rc);    RestoreRaw(self); return r; }
static int __fastcall RenderWrapTxt(void* self, void*, int rc) { int r = g_origRenderTxt(self, rc); RestoreRaw(self); return r; }

static int __fastcall HookRectCalc(void* self, void* /*edx*/, int rc)
{
    float off = 0.0f;
    if (g_uiCenter && g_uiAspect > 0.0f && rc) {
        float w = (float)*(int*)(rc + 0x1AC);
        float h = (float)*(int*)(rc + 0x1B0);
        off = floorf((w - h * g_uiAspect) * 0.5f);
        if (off < 0.0f) off = 0.0f;
    }
    BYTE* e = (BYTE*)self;
    // The menu highlight bar ("selector") stays where the left column text is (at the edge) - this way
    // the highlight, the text and the click area line up. (With CenterMenuList=1 the text is moved, so the bar is too.)
    if (off != 0.0f && !g_menuList && g_getName) {
        const char* nm = NULL;
        __try { nm = g_getName(self); } __except (1) { nm = NULL; }
        if (nm && !strcmp(nm, "selector")) off = 0.0f;
    }
    // Elements from the LeftAnchor list (ini) stay at the left edge together with the left menu column.
    if (off != 0.0f && g_anchorN && g_getName) {
        const char* nm = NULL;
        __try { nm = g_getName(self); } __except (1) { nm = NULL; }
        if (nm) for (int i = 0; i < g_anchorN; i++) if (!strcmp(nm, g_anchor[i])) { off = 0.0f; break; }
    }
    // Elements from the RightAnchor list move to the right screen edge (offset = all the extra width beyond the 4:3 area).
    if (g_rightN && g_getName && g_uiCenter && g_uiAspect > 0.0f && rc) {
        const char* nm = NULL;
        __try { nm = g_getName(self); } __except (1) { nm = NULL; }
        if (InList(nm, g_right, g_rightN)) {
            float full = floorf((float)*(int*)(rc + 0x1AC) - (float)*(int*)(rc + 0x1B0) * g_uiAspect);
            off = full > 0.0f ? full : 0.0f;
        }
    }
    // CenterScreen: the first element in the list is the reference - its center goes to the screen center, the rest move by the same amount.
    int csIdx = -1; float dy = 0.0f;
    if (g_csN && g_getName && g_uiCenter && rc) {
        const char* nm = NULL;
        __try { nm = g_getName(self); } __except (1) { nm = NULL; }
        if (nm) for (int i = 0; i < g_csN; i++) if (!strcmp(nm, g_cs[i])) { csIdx = i; off = 0.0f; break; }
    }
    BYTE* parent = *(BYTE**)(e + 0x78);
    bool pc = (off != 0.0f) && parent && ((*(DWORD*)(e + 0x74) & 0x1000) != 0);
    if (pc) { *(float*)(parent + 0x54) -= off; *(float*)(parent + 0x5C) -= off; }
    int r = ((FnRectCalc)(g_engBase + RVA_RECTCALC))(self, rc);
    if (pc) { *(float*)(parent + 0x54) += off; *(float*)(parent + 0x5C) += off; }
    if (csIdx >= 0 && r) {
        if (csIdx == 0) {
            g_csDelta = floorf((float)*(int*)(rc + 0x1AC) * 0.5f - (*(float*)(e + 0x54) + *(float*)(e + 0x5C)) * 0.5f);
            g_csDy    = floorf((float)*(int*)(rc + 0x1B0) * 0.5f - (*(float*)(e + 0x58) + *(float*)(e + 0x60)) * 0.5f);
        }
        off = g_csDelta; dy = g_csDy;
        if (dy != 0.0f) { *(float*)(e + 0x58) += dy; *(float*)(e + 0x60) += dy; }
    }
    if (r && off != 0.0f) { *(float*)(e + 0x54) += off; *(float*)(e + 0x5C) += off; }
    if (off != 0.0f || dy != 0.0f) {
        // The raw rect (+0x40/+0x48) is shifted too - the text drawing callbacks (Interface.dll) read it.
        // The render wrapper (RenderWrap) restores it right after this element and its children are drawn.
        if (g_rsN < 64) {
            g_rs[g_rsN].ent = e; g_rs[g_rsN].x0 = *(float*)(e + 0x40); g_rs[g_rsN].x2 = *(float*)(e + 0x48); g_rs[g_rsN].off = off; g_rs[g_rsN].y0 = *(float*)(e + 0x44); g_rs[g_rsN].y2 = *(float*)(e + 0x4C);
            g_rsN++;
            *(float*)(e + 0x40) += off; *(float*)(e + 0x48) += off;
            if (dy != 0.0f) { *(float*)(e + 0x44) += dy; *(float*)(e + 0x4C) += dy; }
        }
    }
    if (g_logUi && rc) {
        LogCallbacks(self);
        LogEntity(self, rc, r, (r ? off : 0.0f));
        // elements with a text callback: log on every change (calc result, rc width, visibility)
        static struct { void* e; int r; int w; DWORD fl; } last[64]; static int ln = 0;
        BYTE* cb = *(BYTE**)(e + 0x3C);
        if (cb && *(DWORD*)(cb + 0x10) != *(DWORD*)(cb + 0x14)) {
            int w = *(int*)(rc + 0x1AC); DWORD fl = *(DWORD*)(e + 0xC);
            int i = 0; for (; i < ln; i++) if (last[i].e == self) break;
            if (i == ln && ln < 64) { last[ln].e = self; last[ln].r = -9; last[ln].w = 0; last[ln].fl = 0; ln++; }
            if (i < 64 && (last[i].r != r || last[i].w != w || last[i].fl != fl)) {
                last[i].r = r; last[i].w = w; last[i].fl = fl;
                const char* nm = "?"; if (g_getName) { __try { nm = g_getName(self); if (!nm) nm = "(null)"; } __except (1) { nm = "?"; } }
                Log("UI-TXT: '%s' r=%d rc.w=%d objflags=%08lX raw=(%.0f,%.0f,%.0f,%.0f) scr=(%.0f,%.0f,%.0f,%.0f) off=%.0f", nm, r, w, (unsigned long)fl,
                    *(float*)(e + 0x40), *(float*)(e + 0x44), *(float*)(e + 0x48), *(float*)(e + 0x4C),
                    *(float*)(e + 0x54), *(float*)(e + 0x58), *(float*)(e + 0x5C), *(float*)(e + 0x60), off);
            }
        }
    }
    return r;
}

// ------------------------------------------------------------------ patches
static bool g_exeDone = false, g_engDone = false;

static void PatchExe()
{
    HMODULE exe = GetModuleHandleA(NULL);
    if ((DWORD_PTR)exe != 0x400000 || PeStamp(exe) != EXE_STAMP) {
        Log("exe: not SimTractor V3.66f (base %p, stamp %08lX) - skipping", (void*)exe, PeStamp(exe));
        return;
    }
    BYTE* fn = (BYTE*)FN_APPLYMODE;
    if (fn[0] == 0x83 && fn[1] == 0xEC && fn[2] == 0x10) {
        const BYTE p[3] = { 0x33, 0xC0, 0xC3 };
        Log("exe: FUN_00402B70 %s", WriteMem(fn, p, 3) ? "disabled (33 C0 C3)" : "WRITE ERROR");
    } else if (fn[0] == 0x33 && fn[1] == 0xC0 && fn[2] == 0xC3) {
        Log("exe: FUN_00402B70 already disabled (exe patched in HxD)");
    } else {
        Log("exe: FUN_00402B70 has unknown bytes %02X %02X %02X - skipping", fn[0], fn[1], fn[2]);
    }

    int* s = (int*)SETTINGS;
    Log("exe: settings before: window %dx%d, fullscreen %dx%d, bpp %d", s[0x58/4], s[0x5C/4], s[0x68/4], s[0x6C/4], s[0x70/4]);
    s[0x58/4] = g_ww;  s[0x5C/4] = g_wh;
    s[0x68/4] = g_w;   s[0x6C/4] = g_h;
    Log("exe: settings after:  window %dx%d, fullscreen %dx%d", g_ww, g_wh, g_w, g_h);
    g_exeDone = true;
}

static void PatchEngine(HMODULE m)
{
    if (PeStamp(m) != CK2_3D_STAMP) {
        Log("CK2_3D.dll: different version (stamp %08lX) - skipping engine patches", PeStamp(m));
        g_engDone = true;
        return;
    }
    BYTE* base = (BYTE*)m;
    g_engBase = base;
    if (g_uiAspect > 0.0f) {
        BYTE* p = base + RVA_PARENTRECT;
        if (p[0] == 0x53 && p[1] == 0x56 && p[2] == 0x57 && p[3] == 0x8B && p[4] == 0xF9) {   // push ebx/esi/edi; mov edi,ecx
            BYTE j[5]; j[0] = 0xE9;
            *(DWORD*)(j + 1) = (DWORD)((BYTE*)&HookParentRect - (p + 5));
            Log("CK2_3D: UI layout in %.4f area: %s", g_uiAspect, WriteMem(p, j, 5) ? "enabled" : "WRITE ERROR");
        } else {
            Log("CK2_3D: FUN_1008c500 has unknown bytes - UI layout skipped");
        }
        if (g_uiCenter || g_logUi) {
            HMODULE ck2 = GetModuleHandleA("CK2.dll");
            if (ck2) g_getName = (FnGetName)GetProcAddress(ck2, "?GetName@CKObject@@QAEPADXZ");
            BYTE* c = base + RVA_RECTCALL;
            DWORD target = (DWORD)(base + RVA_RECTCALC);
            if (c[0] == 0xE8 && (DWORD)(c + 5 + *(int*)(c + 1)) == target) {
                int rel = (int)((BYTE*)&HookRectCalc - (c + 5));
                Log("CK2_3D: UI centering: %s", WriteMem(c + 1, &rel, 4) ? "enabled" : "WRITE ERROR");
            } else {
                Log("CK2_3D: UI centering: unexpected call at RVA %lX - skipped", (unsigned long)RVA_RECTCALL);
            }
            if (g_uiCenter) {
                // vtable CK2dEntity (0xAB6C8), CKSprite (0xAB7F0), CKSpriteText (0xABA20): Render slot = +0x110
                static const DWORD vts[3] = { 0xAB6C8, 0xAB7F0, 0xABA20 };
                for (int i = 0; i < 3; i++) {
                    DWORD* slot = (DWORD*)(base + vts[i] + 0x110);
                    DWORD cur = *slot;
                    if (cur == (DWORD)(base + 0x8D580)) {
                        g_origRender = (FnRender)cur;
                        DWORD n = (DWORD)&RenderWrap;
                        Log("CK2_3D: Render wrapper vtable %lX: %s", (unsigned long)vts[i], WriteMem(slot, &n, 4) ? "ok" : "ERROR");
                    } else if (cur == (DWORD)(base + 0x904B0)) {
                        g_origRenderTxt = (FnRender)cur;
                        DWORD n = (DWORD)&RenderWrapTxt;
                        Log("CK2_3D: Render wrapper vtable %lX: %s", (unsigned long)vts[i], WriteMem(slot, &n, 4) ? "ok" : "ERROR");
                    } else Log("CK2_3D: vtable %lX Render slot = %08lX (unexpected) - skipped", (unsigned long)vts[i], (unsigned long)cur);
                }
            }
        }
    }
    if (g_letterbox) {
        BYTE* p = base + RVA_LETTERBOX;
        if (*p == 0x74) { const BYTE j = 0xEB; Log("CK2_3D: black bars: %s", WriteMem(p, &j, 1) ? "removed (JZ->JMP)" : "WRITE ERROR"); }
        else if (*p == 0xEB) Log("CK2_3D: black bars: already removed (file patched in HxD)");
        else Log("CK2_3D: black bars: unknown byte %02X - skipping", *p);
    }
    if (g_horPlus) {
        BYTE* p = base + RVA_PROJ;
        if (p[0] == 0xD9 && p[1] == 0x44 && p[2] == 0x24 && p[3] == 0x04) {          // original start: fld [esp+4]
            BYTE j[5]; j[0] = 0xE9;
            *(DWORD*)(j + 1) = (DWORD)((BYTE*)&HookProj - (p + 5));
            Log("CK2_3D: FOV Hor+: %s", WriteMem(p, j, 5) ? "enabled" : "WRITE ERROR");
        } else {
            Log("CK2_3D: FOV: function already has different bytes (%02X %02X %02X %02X) - skipping (file patched in HxD?)", p[0], p[1], p[2], p[3]);
        }
    }
    g_engDone = true;
}

// ------------------------------------------------------------------ text callback diagnostics (Interface.dll+0x6300)
typedef int (__cdecl *FnTxtCb)(int rc, void* ent, unsigned id);
static BYTE* g_txtTramp = NULL;

// ---- v3: 2D text font scale (Font Properties: Space +0x10, Scale +0x18, shadow +0x40/+0x44 [confirmed in Interface.dll: Set Font Properties / 2D Text]) ----
typedef void* (__cdecl *FnCKGetObject)(void* ctx, unsigned id);
typedef int   (__thiscall *FnGetInParam)(void* beh, int idx, void* out);
typedef void* (__thiscall *FnGetMgr)(void* ctx, unsigned g1, unsigned g2);
typedef void* (__thiscall *FnGetFont)(void* mgr, int fontId);
static void* FontProps(int rc, unsigned id)
{
    static FnCKGetObject fGet = NULL; static FnGetInParam fIn = NULL; static FnGetMgr fMgr = NULL; static bool init = false;
    if (!init) {
        init = true; HMODULE ck = GetModuleHandleA("CK2.dll");
        if (ck) {
            fGet = (FnCKGetObject)GetProcAddress(ck, "?CKGetObject@@YAPAVCKObject@@PAVCKContext@@I@Z");
            fIn  = (FnGetInParam)GetProcAddress(ck, "?GetInputParameterValue@CKBehavior@@QAEHHPAX@Z");
            fMgr = (FnGetMgr)GetProcAddress(ck, "?GetManagerByGuid@CKContext@@QAEPAVCKBaseManager@@UCKGUID@@@Z");
        }
        Log("v3 TextScale: CKGetObject=%p GetInputParameterValue=%p GetManagerByGuid=%p", fGet, fIn, fMgr);
    }
    if (!fGet || !fIn || !fMgr) return NULL;
    void* r = NULL;
    __try {
        void* ctx = *(void**)(rc + 0x10);
        void* beh = fGet(ctx, id); if (!beh) return NULL;
        void* mgr = fMgr(ctx, 0x64FB5810, 0x73262D3B); if (!mgr) return NULL;
        int fid = 0; fIn(beh, 0, &fid);
        void** vt = *(void***)mgr;
        r = ((FnGetFont)vt[0x94 / 4])(mgr, fid);
    } __except (1) { r = NULL; }
    return r;
}

static int __cdecl HookTextCb(int rc, void* ent, unsigned id)
{
    static struct { void* e; unsigned id; float x0; } seen[300]; static int n = 0;
    if (g_logUi) {
        BYTE* e = (BYTE*)ent;
        int i = 0; for (; i < n; i++) if (seen[i].e == ent && seen[i].id == id) break;
        if (i == n && n < 300) { seen[n].e = ent; seen[n].id = id; seen[n].x0 = -99999.0f; n++; }
        if (i < 300 && seen[i].x0 != *(float*)(e + 0x40)) {
            seen[i].x0 = *(float*)(e + 0x40);
            const char* nm = "?"; if (g_getName) { __try { nm = g_getName(ent); if (!nm) nm = "(null)"; } __except (1) { nm = "?"; } }
            Log("TXT-CB: ent=%p '%s' bb=%u raw=(%.0f,%.0f,%.0f,%.0f) scr=(%.0f,%.0f,%.0f,%.0f) flags=%08lX rc=%dx%d",
                ent, nm, id, *(float*)(e + 0x40), *(float*)(e + 0x44), *(float*)(e + 0x48), *(float*)(e + 0x4C),
                *(float*)(e + 0x54), *(float*)(e + 0x58), *(float*)(e + 0x5C), *(float*)(e + 0x60),
                (unsigned long)*(DWORD*)(e + 0x74), *(int*)(rc + 0x1AC), *(int*)(rc + 0x1B0));
        }
    }
    // "Homogeneous" elements (e.g. menu2) get their raw rect recomputed when the children are rendered (FUN_1008C5A0),
    // so our offset is gone before the text callback runs. If that happened - shift it again while the text is drawn.
    BYTE* e = (BYTE*)ent;
    float off = 0.0f;
    if (g_uiCenter && g_uiAspect > 0.0f && rc) {
        off = floorf(((float)*(int*)(rc + 0x1AC) - (float)*(int*)(rc + 0x1B0) * g_uiAspect) * 0.5f);
        if (off < 0.0f) off = 0.0f;
    }
    // v2: applies to EVERY shifted element (not only with CenterMenuList): an element with children (e.g. 'Session Frame', 'Chat Frame', 'Players Frame')
    // gets its raw rect recomputed when the children render, so its text was drawn 'off' pixels too far left [confirmed in widescreen.log: TXT-CB raw=225 vs scr=385].
    // We shift by EXACTLY the offset the element got in HookRectCalc (g_rs[].off), not the general one.
    bool fix = false;
    if (g_uiCenter && g_uiAspect > 0.0f) {
        for (int i = g_rsN - 1; i >= 0; i--)
            if (g_rs[i].ent == e) {
                if (g_rs[i].off != 0.0f && *(float*)(e + 0x40) == g_rs[i].x0 && *(float*)(e + 0x48) == g_rs[i].x2) { fix = true; off = g_rs[i].off; }
                break;
            }
    }
    // Text of elements from the TextRight list (e.g. the info in the bottom right corner) moves to the right screen edge,
    // while the element itself (e.g. the logo in its left part) stays where it is.
    float ts = 0.0f;
    if (g_txtRN && g_getName && g_uiCenter && g_uiAspect > 0.0f && rc) {
        const char* nm = NULL;
        __try { nm = g_getName(ent); } __except (1) { nm = NULL; }
        if (InList(nm, g_txtR, g_txtRN)) {
            ts = floorf((float)*(int*)(rc + 0x1AC) - (float)*(int*)(rc + 0x1B0) * g_uiAspect);
            if (ts < 0.0f) ts = 0.0f;
        }
    }
    float sh = (fix ? off : 0.0f) + ts;
    if (g_logUi && fix) {
        static struct { void* e; unsigned id; } fx[64]; static int nf = 0;
        int k = 0; for (; k < nf; k++) if (fx[k].e == ent && fx[k].id == id) break;
        if (k == nf && nf < 64) {
            fx[nf].e = ent; fx[nf].id = id; nf++;
            const char* nm = "?"; if (g_getName) { __try { nm = g_getName(ent); if (!nm) nm = "(null)"; } __except (1) { nm = "?"; } }
            Log("TXT-FIX: '%s' bb=%u text shifted by %.0f", nm, id, sh);
        }
    }
    if (sh != 0.0f) { *(float*)(e + 0x40) += sh; *(float*)(e + 0x48) += sh; }
    // v3: text scale
    float* fp = NULL; float sv[6] = {0}; float k = 1.0f; float tdx = 0.0f;
    if (g_tsN && g_tsMul > 0.0f && g_getName && rc) {
        const char* nm = NULL; __try { nm = g_getName(ent); } __except (1) { nm = NULL; }
        // list entry: "Name" or "Name*0.7" (extra multiplier for this element)
        float em = 0.0f; g_tsY = 1.0f; g_tsRel = false; g_tsDx = 0.0f;
        for (int q = 0; q < g_tsN && nm; q++) {
            char nb[64]; strncpy(nb, g_tsL[q], 63); nb[63] = 0; float m = 1.0f;
            float v = 1.0f; float yf = 1.0f; float dx = 0.0f;
            char* gt = strchr(nb, '>'); if (gt) { *gt = 0; dx = (float)atof(gt + 1); }   // '>dx' = shift text right by dx units (480-high space)
            char* ca = strchr(nb, '^'); if (ca) { *ca = 0; yf = (float)atof(ca + 1); if (yf <= 0.0f) yf = 1.0f; }
            char* at = strchr(nb, '@'); if (at) { *at = 0; v = (float)atof(at + 1); if (v <= 0.0f) v = 1.0f; }
            bool rel = false; char* eq = strchr(nb, '='); if (eq) { *eq = '*'; rel = true; }   // 'Name=0.9' = RELATIVE multiplier (no resolution scaling) for texts the game script already scales
            char* st = strchr(nb, '*'); if (st) { *st = 0; m = (float)atof(st + 1); if (m <= 0.0f) m = 1.0f; }
            if (strcmp(nb, nm) == 0) {
                // @v = multiplier at height >=1080 (linear from 1.0 at 720), correction for texts that come out too big at 1080p
                float hh = (float)*(int*)(rc + 0x1B0), t = (hh - 720.0f) / 360.0f; if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f;
                em = m * (1.0f + (v - 1.0f) * t); g_tsY = yf; g_tsRel = rel; g_tsDx = dx; break;
            }
        }
        if (em > 0.0f) {
            if (g_tsDx != 0.0f) { tdx = g_tsDx * (float)*(int*)(rc + 0x1B0) / 480.0f; *(float*)(e + 0x40) += tdx; *(float*)(e + 0x48) += tdx; }
            k = (g_tsRel ? 1.0f : (float)*(int*)(rc + 0x1B0) / 480.0f * g_tsMul) * em;
            if (k > 0.05f && k < 20.0f && (fabsf(k - 1.0f) > 0.001f || g_tsY != 1.0f)) {
                fp = (float*)FontProps(rc, id);
                if (fp) {
                    float sx = fp[6], sy = fp[7];
                    static unsigned lg[128]; static int nlg = 0; bool seenb = false; unsigned key = id ^ ((unsigned)*(int*)(rc + 0x1B0) << 20);
                    for (int q = 0; q < nlg; q++) if (lg[q] == key) seenb = true;
                    if (!seenb && nlg < 128 && g_logUi) { lg[nlg++] = key; Log("TXT-SCALE: '%s' bb=%u font=%p Scale=(%g,%g) k=%.3f", nm ? nm : "?", id, fp, sx, sy, k); }
                    if (!(sx > 0.0001f && sx < 20.0f && sy > 0.0001f && sy < 20.0f)) fp = NULL;   // implausible = bad pointer
                }
                if (fp) { sv[0]=fp[4]; sv[1]=fp[5]; sv[2]=fp[6]; sv[3]=fp[7]; sv[4]=fp[0x10]; sv[5]=fp[0x11];
                          fp[4]*=k; fp[5]*=k; fp[6]*=k; fp[7]*=k*g_tsY; fp[0x10]*=k; fp[0x11]*=k; }
            }
        }
    }
    int r = ((FnTxtCb)g_txtTramp)(rc, ent, id);
    if (tdx != 0.0f) { *(float*)(e + 0x40) -= tdx; *(float*)(e + 0x48) -= tdx; }
    if (fp) { fp[4]=sv[0]; fp[5]=sv[1]; fp[6]=sv[2]; fp[7]=sv[3]; fp[0x10]=sv[4]; fp[0x11]=sv[5]; }
    if (sh != 0.0f) { *(float*)(e + 0x40) -= sh; *(float*)(e + 0x48) -= sh; }
    return r;
}

static void PatchInterface(HMODULE m)
{
    BYTE* f = (BYTE*)m + 0x6300;
    static const BYTE sig[10] = { 0x8B,0x4C,0x24,0x0C, 0x81,0xEC,0x8C,0x00,0x00,0x00 };
    if (memcmp(f, sig, 10) != 0) { Log("Interface.dll: different version of the text function - skipping"); return; }
    BYTE* t = (BYTE*)VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!t) return;
    memcpy(t, sig, 10);
    t[10] = 0xE9; *(int*)(t + 11) = (int)((f + 10) - (t + 15));
    g_txtTramp = t;
    BYTE j[10]; j[0] = 0xE9; *(int*)(j + 1) = (int)((BYTE*)&HookTextCb - (f + 5));
    for (int i = 5; i < 10; i++) j[i] = 0x90;
    Log("Interface.dll: text callback hook: %s", WriteMem(f, j, 10) ? "ok" : "ERROR");
}

static DWORD WINAPI InterfaceWaiter(LPVOID)
{
    for (int i = 0; i < 120000; i++) {
        HMODULE m = GetModuleHandleA("Interface.dll");
        if (m) { PatchInterface(m); return 0; }
        Sleep(1);
    }
    return 0;
}

// ------------------------------------------------------------------ threads
static DWORD WINAPI EngineWaiter(LPVOID)
{
    for (int i = 0; i < 120000 && !g_engDone; i++) {         // up to ~2 min
        HMODULE m = GetModuleHandleA("CK2_3D.dll");
        if (m) { PatchEngine(m); break; }
        Sleep(1);
    }
    if (!g_engDone) Log("CK2_3D.dll was not loaded in time - skipped");
    return 0;
}

static BOOL CALLBACK FindMain(HWND h, LPARAM lp)
{
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    if (pid != GetCurrentProcessId()) return TRUE;
    char cls[64]; GetClassNameA(h, cls, sizeof(cls));
    if (!strcmp(cls, "CustomPlayer")) { *(HWND*)lp = h; return FALSE; }
    return TRUE;
}

// The main game window is created before mods load (with the default size), so we resize it.
static DWORD WINAPI WindowFixer(LPVOID)
{
    HWND w = NULL;
    for (int i = 0; i < 600 && !w; i++) { EnumWindows(FindMain, (LPARAM)&w); if (!w) Sleep(10); }
    if (!w) { Log("window: main game window not found (class CustomPlayer)"); return 0; }
    RECT cur; GetWindowRect(w, &cur);
    RECT r;
    r.left = (GetSystemMetrics(SM_CXSCREEN) - g_ww) / 2;
    r.top  = (GetSystemMetrics(SM_CYSCREEN) - g_wh) / 2;
    r.right = r.left + g_ww;
    r.bottom = r.top + g_wh;
    AdjustWindowRect(&r, 0xC00000, FALSE);
    Log("window: was %ldx%ld, setting %dx%d (outer %ldx%ld)",
        cur.right - cur.left, cur.bottom - cur.top, g_ww, g_wh, r.right - r.left, r.bottom - r.top);
    SetWindowPos(w, NULL, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);     // async, does not block the game thread
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hInst);
        GetModuleFileNameA(hInst, g_dir, MAX_PATH);
        char* sl = strrchr(g_dir, '\\'); if (sl) *(sl + 1) = 0;
        _snprintf_s(g_ini, sizeof(g_ini), _TRUNCATE, "%swidescreen.ini", g_dir);
        _snprintf_s(g_log, sizeof(g_log), _TRUNCATE, "%swidescreen.log", g_dir);
        LoadConfig();
        Log("---- widescreen v3k start ---- %dx%d (window %dx%d), HorPlus=%d, Letterbox=%d, ref=%.4f",
            g_w, g_h, g_ww, g_wh, (int)g_horPlus, (int)g_letterbox, g_refAspect);
        PatchExe();                                   // synchronous: memory write before the game creates the render context
        HMODULE m = GetModuleHandleA("CK2_3D.dll");
        if (m) PatchEngine(m);
        else CreateThread(NULL, 0, EngineWaiter, NULL, 0, NULL);
        if (g_exeDone) CreateThread(NULL, 0, WindowFixer, NULL, 0, NULL);
        if (g_logUi || g_uiCenter || g_tsN) CreateThread(NULL, 0, InterfaceWaiter, NULL, 0, NULL);
    }
    return TRUE;
}

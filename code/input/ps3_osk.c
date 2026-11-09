/* ps3_osk.c -- the XMB on-screen keyboard (OSK) for text the menus ask for.
 *
 * Recipe from CrispyCell (Strife names), itself from Apollo Save Tool: a
 * memory container for the dialog, a sysutil callback on slot 1 (slot 0 is
 * the game's exit callback), oskLoadAsync, and a state machine driven by the
 * callback while the game keeps drawing frames underneath.
 *
 * The UI asks with the console command "ps3_osk <cvar> [title]". The text
 * the player accepts goes into <cvar>, and the cvar ps3_osk_result says how
 * it ended: 1 accepted (maybe empty), 2 canceled, 3 the OSK could not open.
 * The UI polls it (ui_shared.c) and runs the field's action; on 3 it runs it
 * anyway, so there is always a way forward (the save gets its automatic
 * name). While the OSK is up the pad is not read for the game. */

#include <string.h>

#include <ppu-types.h>
#include <sys/memory.h>
#include <sysutil/sysutil.h>
#include <sysutil/osk.h>

#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"
#include "sys/ps3_log.h"

#define OSK_MAX_CHARS       24
#define OSK_CONTAINER_BIG   ( 8 * 1024 * 1024 )     /* CrispyCell, proven */
#define OSK_CONTAINER_SMALL ( 4 * 1024 * 1024 )     /* "typical" per osk.h */

enum { OSK_IDLE, OSK_LOADING, OSK_OPEN, OSK_UNLOADING };

static int osk_state = OSK_IDLE;
static int osk_accepted;
static char osk_cvar[MAX_CVAR_VALUE_STRING];
static sys_mem_container_t osk_container;
static u16 osk_title[64];
static u16 osk_start[OSK_MAX_CHARS + 1];
static u16 osk_text[OSK_MAX_CHARS + 1];
static oskCallbackReturnParam osk_ret;

int ps3_osk_just_closed;    /* ps3_input.c: swallow the buttons still held */

int PS3_OSK_Active(void)
{
    return osk_state != OSK_IDLE;
}

static void osk_ascii_to_utf16(const char *in, u16 *out, int max)
{
    int i;

    for (i = 0; in[i] && i < max; i++)
        out[i] = (u16)(unsigned char)in[i];
    out[i] = 0;
}

/* the full-width panel writes U+FF01..U+FF5E; U+3000 is its space */
static void osk_utf16_to_ascii(const u16 *in, int len, char *out, int size)
{
    int i, n = 0;

    for (i = 0; i < len && in[i] && n < size - 1; i++) {
        u16 c = in[i];

        if (c >= 0xFF01 && c <= 0xFF5E)
            c -= 0xFEE0;
        else if (c == 0x3000)
            c = ' ';
        if (c >= 32 && c < 127 && c != '"' && c != ';' && c != '\\')
            out[n++] = (char)c;
    }
    out[n] = 0;
}

static void osk_finish(int result)
{
    sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT1);
    if (osk_container) {
        sysMemContainerDestroy(osk_container);
        osk_container = 0;
    }
    osk_state = OSK_IDLE;
    ps3_osk_just_closed = 1;
    Cvar_Set("ps3_osk_result", va("%d", result));
}

static void osk_callback(u64 status, u64 param, void *userdata)
{
    (void)param; (void)userdata;

    switch (status) {
    case SYSUTIL_OSK_LOADED:
        osk_state = OSK_OPEN;
        break;
    case SYSUTIL_OSK_INPUT_ENTERED:
        osk_accepted = 1;
        oskGetInputText(&osk_ret);
        break;
    case SYSUTIL_OSK_INPUT_CANCELED:
        osk_accepted = 0;
        oskAbort();
        osk_state = OSK_UNLOADING;
        oskUnloadAsync(&osk_ret);
        break;
    case SYSUTIL_OSK_DONE:
        if (osk_state != OSK_UNLOADING) {
            osk_state = OSK_UNLOADING;
            oskUnloadAsync(&osk_ret);
        }
        break;
    case SYSUTIL_OSK_UNLOADED:
        if (osk_accepted || osk_ret.res == OSK_OK || osk_ret.res == OSK_NO_TEXT) {
            char text[OSK_MAX_CHARS + 1];

            osk_utf16_to_ascii(osk_text, OSK_MAX_CHARS, text, sizeof(text));
            Cvar_Set(osk_cvar, text);
            PS3_Logf("[osk] %s = \"%s\"", osk_cvar, text);
            osk_finish(1);
        } else {
            ps3_log("[osk] canceled");
            osk_finish(2);
        }
        break;
    default:
        break;
    }
}

static void PS3_OSK_f(void)
{
    oskParam param;
    oskInputFieldInfo info;
    const char *title;

    if (Cmd_Argc() < 2) {
        Com_Printf("usage: ps3_osk <cvar> [title]\n");
        return;
    }
    if (osk_state != OSK_IDLE)
        return;

    Q_strncpyz(osk_cvar, Cmd_Argv(1), sizeof(osk_cvar));
    title = Cmd_Argc() > 2 ? Cmd_Argv(2) : "Name";
    Cvar_Set("ps3_osk_result", "0");

    if (sysMemContainerCreate(&osk_container, OSK_CONTAINER_BIG) != 0 &&
        sysMemContainerCreate(&osk_container, OSK_CONTAINER_SMALL) != 0) {
        osk_container = 0;
        ps3_log("[osk] no memory for the keyboard: automatic name");
        Cvar_Set("ps3_osk_result", "3");
        return;
    }

    osk_ascii_to_utf16(title, osk_title, 63);
    osk_ascii_to_utf16(Cvar_VariableString(osk_cvar), osk_start, OSK_MAX_CHARS);
    memset(osk_text, 0, sizeof(osk_text));
    memset(&osk_ret, 0, sizeof(osk_ret));
    osk_ret.len = OSK_MAX_CHARS;
    osk_ret.str = osk_text;
    osk_accepted = 0;

    memset(&param, 0, sizeof(param));
    param.firstViewPanel = OSK_PANEL_TYPE_ALPHABET_FULL_WIDTH;
    param.allowedPanels = OSK_PANEL_TYPE_ALPHABET | OSK_PANEL_TYPE_NUMERAL |
                          OSK_PANEL_TYPE_ENGLISH | OSK_PANEL_TYPE_URL;
    param.prohibitFlags = OSK_PROHIBIT_RETURN;
    param.controlPoint.x = 0;
    param.controlPoint.y = 0;

    memset(&info, 0, sizeof(info));
    info.message = osk_title;
    info.startText = osk_start;
    info.maxLength = OSK_MAX_CHARS;

    oskSetKeyLayoutOption(OSK_10KEY_PANEL | OSK_FULLKEY_PANEL);
    oskAddSupportLanguage(OSK_PANEL_TYPE_ALPHABET | OSK_PANEL_TYPE_ENGLISH);
    oskSetLayoutMode(OSK_LAYOUTMODE_HORIZONTAL_ALIGN_CENTER | OSK_LAYOUTMODE_VERTICAL_ALIGN_CENTER);
    oskSetInitialInputDevice(OSK_DEVICE_PAD);

    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT1, osk_callback, NULL);
    if (oskLoadAsync(osk_container, &param, &info) != 0) {
        ps3_log("[osk] oskLoadAsync failed: automatic name");
        osk_finish(3);
        return;
    }
    osk_state = OSK_LOADING;
    PS3_Logf("[osk] open for %s", osk_cvar);
}

void PS3_OSK_Init(void)
{
    Cvar_Get("ps3_osk_result", "0", 0);
    Cmd_AddCommand("ps3_osk", PS3_OSK_f);
}

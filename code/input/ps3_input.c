/* ps3_input.c -- DS3 pad, USB keyboard and USB mouse for iortcw SP.
 *
 * Keyboard and mouse are IoQuake3-PS3's (Mayo1970) as-is. The pad section is
 * rewritten for RTCW: its own default binds, the d-pad as extra buttons in
 * game and as arrows in menus/console, no rumble, no OSK yet. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <io/pad.h>
#include <io/kb.h>
#include <io/mouse.h>

#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"
#include "client/keycodes.h"
#include "../input/ps3_input.h"
#include "sys/ps3_log.h"

extern int Key_GetCatcher(void);
extern void Key_SetBinding(int keynum, const char *binding);
extern char *Key_GetBinding(int keynum);

/* -- USB Keyboard -- */

/* Raw USB HID usage code (0x04-0x65) → Q3 keynum (0 = unmapped).
 * Letters are always lowercase; shifted chars come via SE_CHAR. */
static const int s_raw_to_q3[256] = {
    [0x04]='a', [0x05]='b', [0x06]='c', [0x07]='d',
    [0x08]='e', [0x09]='f', [0x0A]='g', [0x0B]='h',
    [0x0C]='i', [0x0D]='j', [0x0E]='k', [0x0F]='l',
    [0x10]='m', [0x11]='n', [0x12]='o', [0x13]='p',
    [0x14]='q', [0x15]='r', [0x16]='s', [0x17]='t',
    [0x18]='u', [0x19]='v', [0x1A]='w', [0x1B]='x',
    [0x1C]='y', [0x1D]='z',
    [0x1E]='1', [0x1F]='2', [0x20]='3', [0x21]='4',
    [0x22]='5', [0x23]='6', [0x24]='7', [0x25]='8',
    [0x26]='9', [0x27]='0',
    [0x28]=K_ENTER,     [0x29]=K_ESCAPE,    [0x2A]=K_BACKSPACE,
    [0x2B]=K_TAB,       [0x2C]=K_SPACE,
    [0x2D]='-',  [0x2E]='=',  [0x2F]='[',  [0x30]=']',
    [0x31]='\\', [0x33]=';',  [0x34]='\'', [0x35]='`',
    [0x36]=',',  [0x37]='.',  [0x38]='/',
    [0x39]=K_CAPSLOCK,
    [0x3A]=K_F1,  [0x3B]=K_F2,  [0x3C]=K_F3,  [0x3D]=K_F4,
    [0x3E]=K_F5,  [0x3F]=K_F6,  [0x40]=K_F7,  [0x41]=K_F8,
    [0x42]=K_F9,  [0x43]=K_F10, [0x44]=K_F11, [0x45]=K_F12,
    [0x46]=K_PRINT, [0x47]=K_SCROLLOCK, [0x48]=K_PAUSE,
    [0x49]=K_INS,  [0x4A]=K_HOME, [0x4B]=K_PGUP,
    [0x4C]=K_DEL,  [0x4D]=K_END,  [0x4E]=K_PGDN,
    [0x4F]=K_RIGHTARROW, [0x50]=K_LEFTARROW,
    [0x51]=K_DOWNARROW,  [0x52]=K_UPARROW,
    [0x53]=K_KP_NUMLOCK,    [0x54]=K_KP_SLASH,  [0x55]=K_KP_STAR,
    [0x56]=K_KP_MINUS,      [0x57]=K_KP_PLUS,   [0x58]=K_KP_ENTER,
    [0x59]=K_KP_END,        [0x5A]=K_KP_DOWNARROW, [0x5B]=K_KP_PGDN,
    [0x5C]=K_KP_LEFTARROW,  [0x5D]=K_KP_5,      [0x5E]=K_KP_RIGHTARROW,
    [0x5F]=K_KP_HOME,       [0x60]=K_KP_UPARROW,[0x61]=K_KP_PGUP,
    [0x62]=K_KP_INS,        [0x63]=K_KP_DEL,
    [0x65]=K_MENU,
};

/* KbMkey.mkeys bitmasks (PPU big-endian; l_ctrl is bit 0 per header comment) */
#define MK_LCTRL   0x01u
#define MK_LSHIFT  0x02u
#define MK_LALT    0x04u
#define MK_RCTRL   0x10u
#define MK_RSHIFT  0x20u
#define MK_RALT    0x40u

static KbInfo   s_kb_info;
static qboolean s_kb_connected = qfalse;
static int      s_kb_port = -1;
static uint8_t  s_kb_cur[256];
static uint8_t  s_kb_prev[256];
static int      s_kb_last_seen[256];
static u32      s_kb_mkeys_prev = 0;
static KbLed    s_kb_led;

static void KB_EmitModifier(u32 cur_mk, u32 prev_mk, u32 bits, int q3key)
{
    int cur  = !!(cur_mk  & bits);
    int prev = !!(prev_mk & bits);
    if (cur != prev)
        Com_QueueEvent(0, SE_KEY, q3key, cur ? qtrue : qfalse, 0, NULL);
}

static void KB_ReleaseAll(void)
{
    int k;
    for (k = 1; k < 256; k++) {
        if (s_kb_prev[k]) {
            int q3key = s_raw_to_q3[k];
            if (q3key) Com_QueueEvent(0, SE_KEY, q3key, qfalse, 0, NULL);
        }
    }
    KB_EmitModifier(0, s_kb_mkeys_prev, MK_LCTRL|MK_RCTRL,  K_CTRL);
    KB_EmitModifier(0, s_kb_mkeys_prev, MK_LSHIFT|MK_RSHIFT, K_SHIFT);
    KB_EmitModifier(0, s_kb_mkeys_prev, MK_LALT|MK_RALT,     K_ALT);
    memset(s_kb_prev, 0, sizeof(s_kb_prev));
    s_kb_mkeys_prev = 0;
}

static void KB_Init(void)
{
    ioKbInit(MAX_KB_PORT_NUM);
    memset(s_kb_cur,       0, sizeof(s_kb_cur));
    memset(s_kb_prev,      0, sizeof(s_kb_prev));
    memset(s_kb_last_seen, 0, sizeof(s_kb_last_seen));
    memset(&s_kb_led, 0, sizeof(s_kb_led));
    s_kb_mkeys_prev = 0;
    s_kb_connected = qfalse;
    s_kb_port = -1;
}

static void KB_Shutdown(void) { ioKbEnd(); }

/* If syncOnly, update state without emitting events (for OSK). */
static void KB_Frame(qboolean syncOnly)
{
    KbData data;
    int i, k, found_port, q3key;
    qboolean pressed;
    u16 ascii;
    u32 mk;
    static int s_read_fail_logged = 0;
    static int s_key_logged = 0;

    if (ioKbGetInfo(&s_kb_info) != 0) return;

    if (s_kb_info.connected == 0) {
        if (s_kb_connected) {
            if (!syncOnly) KB_ReleaseAll();
            else { memset(s_kb_prev, 0, sizeof(s_kb_prev)); s_kb_mkeys_prev = 0; }
            s_kb_connected = qfalse;
            s_kb_port = -1;
            s_read_fail_logged = 0;
            s_key_logged = 0;
        }
        return;
    }

    found_port = -1;
    for (i = 0; i < MAX_KB_PORT_NUM; i++) {
        if (s_kb_info.status[i]) { found_port = i; break; }
    }
    if (found_port < 0) return;

    if (!s_kb_connected || found_port != s_kb_port) {
        int rc_ct;
        if (s_kb_connected && !syncOnly) KB_ReleaseAll();
        /* Skip ioKbSetReadMode; it silently breaks codetype. */
        {
            int rc_rm;
            rc_ct = ioKbSetCodeType(found_port, KB_CODETYPE_RAW);
            rc_rm = ioKbSetReadMode(found_port, KB_RMODE_PACKET);
            ioKbClearBuf(found_port);
            printf("[ps3kb] port=%d info=0x%x CodeType(RAW)=0x%x ReadMode(PACKET)=0x%x\n",
                   found_port, (unsigned)s_kb_info.info,
                   (unsigned)rc_ct, (unsigned)rc_rm);
        }
            memset(s_kb_cur,       0, sizeof(s_kb_cur));
        memset(s_kb_prev,      0, sizeof(s_kb_prev));
        memset(s_kb_last_seen, 0, sizeof(s_kb_last_seen));
        s_kb_mkeys_prev = 0;
        s_kb_port = found_port;
        s_kb_connected = qtrue;
        s_read_fail_logged = 0;
        s_key_logged = 0;
    }

    if (ioKbRead(s_kb_port, &data) != 0) {
        s_read_fail_logged++;
        if (s_read_fail_logged <= 5 || (s_read_fail_logged % 600 == 0))
            printf("[ps3kb] ioKbRead fail #%d port=%d\n",
                   s_read_fail_logged, s_kb_port);
        return;
    }
    s_read_fail_logged = 0;

    {
        int now = Sys_Milliseconds();

        if (data.nb_keycode > 0) {
            /* New snapshot: rebuild s_kb_cur from packet. Absent keys = released. */
            memset(s_kb_cur, 0, sizeof(s_kb_cur));
            for (i = 0; i < data.nb_keycode && i < MAX_KEYCODES; i++) {
                u16 raw = data.keycode[i] & ~((u16)(KB_RAWDAT | KB_KEYPAD));
                if (raw > 0 && raw < 256) {
                    s_kb_cur[(int)raw] = 1;
                    s_kb_last_seen[(int)raw] = now;
                }
            }
            if (s_key_logged < 10) {
                printf("[ps3kb] nb=%d raw[0]=0x%04x masked=0x%04x\n",
                       (int)data.nb_keycode, (unsigned)data.keycode[0],
                       (unsigned)(data.keycode[0] & ~((u16)(KB_RAWDAT | KB_KEYPAD))));
                s_key_logged++;
            }
        } else {
            /* nb_keycode==0 means "no keys" AND "read failed" -- SDK won't say which.
             * 2000ms emergency timeout is the only reliable way to release a stuck key. */
            for (i = 1; i < 256; i++) {
                if (s_kb_cur[i] && (now - s_kb_last_seen[i]) > 2000) {
                    printf("[ps3kb] emergency release raw=0x%02x (2000ms idle)\n", i);
                    s_kb_cur[i] = 0;
                }
            }
        }
    }

    if (!syncOnly) {
        for (k = 1; k < 256; k++) {
            if (s_kb_cur[k] == s_kb_prev[k]) continue;
            q3key = s_raw_to_q3[k];
            if (!q3key) continue;
            pressed = s_kb_cur[k] ? qtrue : qfalse;
            Com_QueueEvent(0, SE_KEY, q3key, pressed, 0, NULL);
            if (pressed) {
                ascii = ioKbCnvRawCode(KB_MAPPING_101, data.mkey, s_kb_led, (u16)k);
                if (ascii >= 32 && ascii < 127)
                    Com_QueueEvent(0, SE_CHAR, (int)ascii, 0, 0, NULL);
            }
        }
        mk = data.mkey._KbMkeyU.mkeys;
        KB_EmitModifier(mk, s_kb_mkeys_prev, MK_LCTRL|MK_RCTRL,  K_CTRL);
        KB_EmitModifier(mk, s_kb_mkeys_prev, MK_LSHIFT|MK_RSHIFT, K_SHIFT);
        KB_EmitModifier(mk, s_kb_mkeys_prev, MK_LALT|MK_RALT,     K_ALT);
        s_kb_mkeys_prev = mk;
    } else {
        s_kb_mkeys_prev = data.mkey._KbMkeyU.mkeys;
    }

    s_kb_led = data.led;
    memcpy(s_kb_prev, s_kb_cur, sizeof(s_kb_cur));
}

/* -- USB Mouse -- */

static mouseInfo s_mouse_info;
static qboolean  s_mouse_connected = qfalse;
static u8        s_mouse_btns_prev = 0;

static void Mouse_Init(void)
{
    ioMouseInit(2);
    s_mouse_connected = qfalse;
    s_mouse_btns_prev = 0;
}

static void Mouse_Shutdown(void) { ioMouseEnd(); }

static void Mouse_Frame(qboolean syncOnly)
{
    mouseDataList list;
    mouseData *md;
    u32 i;
    u8 cur, diff;
    int dx = 0, dy = 0;

    if (ioMouseGetInfo(&s_mouse_info) != 0) return;

    if (s_mouse_info.connected == 0) {
        if (s_mouse_connected) {
            if (!syncOnly) {
                u8 held = s_mouse_btns_prev;
                if (held & 0x01) Com_QueueEvent(0, SE_KEY, K_MOUSE1, qfalse, 0, NULL);
                if (held & 0x02) Com_QueueEvent(0, SE_KEY, K_MOUSE2, qfalse, 0, NULL);
                if (held & 0x04) Com_QueueEvent(0, SE_KEY, K_MOUSE3, qfalse, 0, NULL);
            }
            s_mouse_btns_prev = 0;
            s_mouse_connected = qfalse;
        }
        return;
    }

    if (!s_mouse_connected) {
        ioMouseClearBuf(0);
        s_mouse_btns_prev = 0;
        s_mouse_connected = qtrue;
        if (!syncOnly) printf("[ps3] Mouse connected\n");
    }

    if (ioMouseGetDataList(0, &list) != 0) return;
    if (list.count == 0) return;

    for (i = 0; i < list.count && i < MOUSE_MAX_DATA_LIST; i++) {
        md = &list.list[i];
        if (!md->update) continue;

        dx += (int)md->x_axis;
        dy += (int)md->y_axis;

        if (!syncOnly) {
            cur  = md->buttons;
            diff = cur ^ s_mouse_btns_prev;
            if (diff & 0x01) Com_QueueEvent(0, SE_KEY, K_MOUSE1, (cur & 0x01) ? qtrue : qfalse, 0, NULL);
            if (diff & 0x02) Com_QueueEvent(0, SE_KEY, K_MOUSE2, (cur & 0x02) ? qtrue : qfalse, 0, NULL);
            if (diff & 0x04) Com_QueueEvent(0, SE_KEY, K_MOUSE3, (cur & 0x04) ? qtrue : qfalse, 0, NULL);
            if (md->wheel > 0) {
                Com_QueueEvent(0, SE_KEY, K_MWHEELUP,   qtrue,  0, NULL);
                Com_QueueEvent(0, SE_KEY, K_MWHEELUP,   qfalse, 0, NULL);
            } else if (md->wheel < 0) {
                Com_QueueEvent(0, SE_KEY, K_MWHEELDOWN, qtrue,  0, NULL);
                Com_QueueEvent(0, SE_KEY, K_MWHEELDOWN, qfalse, 0, NULL);
            }
            s_mouse_btns_prev = cur;
        } else {
            s_mouse_btns_prev = md->buttons;
        }
    }

    if (!syncOnly && (dx != 0 || dy != 0))
        Com_QueueEvent(0, SE_MOUSE, dx, dy, 0, NULL);
}

/* -- DS3 pad -- */

#define STICK_CENTER       128
#define STICK_DEADZONE     30
#define STICK_RANGE        (128 - STICK_DEADZONE)
#define TRIGGER_THRESHOLD  30

enum {
    PB_CROSS, PB_CIRCLE, PB_SQUARE, PB_TRIANGLE,
    PB_L1, PB_R1, PB_L2, PB_R2, PB_L3, PB_R3,
    PB_START, PB_SELECT,
    PB_UP, PB_DOWN, PB_LEFT, PB_RIGHT,
    /* left stick as a d-pad: only produces keys in menus */
    PB_LS_UP, PB_LS_DOWN, PB_LS_LEFT, PB_LS_RIGHT,
    PB_COUNT
};

/* In game every button is a bindable key. */
static const int pad_game_key[PB_COUNT] = {
    K_JOY1, K_JOY2, K_JOY3, K_JOY4,      /* cross circle square triangle */
    K_JOY5, K_JOY6, K_JOY7, K_JOY8,      /* L1 R1 L2 R2 */
    K_JOY9, K_JOY10,                     /* L3 R3 */
    K_ESCAPE, K_JOY11,                   /* start select */
    K_JOY12, K_JOY13, K_JOY14, K_JOY15,  /* d-pad */
    0, 0, 0, 0,                          /* left stick directions: nothing in game */
};

/* Menu navigation auto-repeat for held arrows (d-pad / left stick). */
#define PAD_REPEAT_DELAY   380
#define PAD_REPEAT_RATE    110
/* Left stick as d-pad, with hysteresis so it does not chatter. */
#define LS_PRESS           72
#define LS_RELEASE         40
/* Right stick pointer (menu units per frame at full tilt). */
#define POINTER_MAX_SPEED  9.0f

static padInfo  ps3_pad_info;
static padData  ps3_pad_data;
static int      ps3_active_pad_port = 0;
static int      pad_down[PB_COUNT];      /* state as last reported to the engine */
static int      pad_sent[PB_COUNT];      /* key sent for the press, released with it */
static int      ps3_axis_prev[4];
static float    ps3_cursor_accum_x, ps3_cursor_accum_y;
static int      select_combo;
static int      select_at, select_hold;     /* SELECT held: its own key */

/* SELECT tapped = K_JOY11 (notebook), held longer = K_JOY16 (binoculars)
 * until released; the menus name it "SELECT (HOLD)" (cl_ui.c) */
#define SELECT_HOLD_MS     300
#define K_SELECT_HOLD      K_JOY16
static int      pad_repeat_at[PB_COUNT]; /* next auto-repeat time, 0 = none */

int PS3_OSK_Active(void);          /* ps3_osk.c */
void PS3_OSK_Init(void);
extern int ps3_osk_just_closed;

static void PS3_SetDefaultBind(int keynum, const char *binding)
{
    char *existing = Key_GetBinding(keynum);
    if (!existing || !existing[0])
        Key_SetBinding(keynum, binding);
}

static void read_buttons(const padData *pd, int out[PB_COUNT])
{
    out[PB_CROSS]    = pd->BTN_CROSS;
    out[PB_CIRCLE]   = pd->BTN_CIRCLE;
    out[PB_SQUARE]   = pd->BTN_SQUARE;
    out[PB_TRIANGLE] = pd->BTN_TRIANGLE;
    out[PB_L1]       = pd->BTN_L1;
    out[PB_R1]       = pd->BTN_R1;
    /* OR with analog pressure so triggers work with pressure mode on or off. */
    out[PB_L2]       = (pd->BTN_L2 || pd->PRE_L2 > TRIGGER_THRESHOLD) ? 1 : 0;
    out[PB_R2]       = (pd->BTN_R2 || pd->PRE_R2 > TRIGGER_THRESHOLD) ? 1 : 0;
    out[PB_L3]       = pd->BTN_L3;
    out[PB_R3]       = pd->BTN_R3;
    out[PB_START]    = pd->BTN_START;
    out[PB_SELECT]   = pd->BTN_SELECT;
    out[PB_UP]       = pd->BTN_UP;
    out[PB_DOWN]     = pd->BTN_DOWN;
    out[PB_LEFT]     = pd->BTN_LEFT;
    out[PB_RIGHT]    = pd->BTN_RIGHT;

    /* left stick directions, dominant axis only, with hysteresis */
    {
        int lx = (int)pd->ANA_L_H - STICK_CENTER;
        int ly = (int)pd->ANA_L_V - STICK_CENTER;
        int horiz = abs(lx) > abs(ly);

        out[PB_LS_UP]    = (!horiz && ly < -(pad_down[PB_LS_UP]    ? LS_RELEASE : LS_PRESS)) ? 1 : 0;
        out[PB_LS_DOWN]  = (!horiz && ly >  (pad_down[PB_LS_DOWN]  ? LS_RELEASE : LS_PRESS)) ? 1 : 0;
        out[PB_LS_LEFT]  = ( horiz && lx < -(pad_down[PB_LS_LEFT]  ? LS_RELEASE : LS_PRESS)) ? 1 : 0;
        out[PB_LS_RIGHT] = ( horiz && lx >  (pad_down[PB_LS_RIGHT] ? LS_RELEASE : LS_PRESS)) ? 1 : 0;
    }
}

/* Key for a new press, depending on who has the input. Menus and the console
 * get Enter/Escape/arrows; the game gets the bindable keys. */
static int pad_key_for(int b, int catchers)
{
    int menu = catchers & (KEYCATCH_UI | KEYCATCH_CGAME | KEYCATCH_CONSOLE | KEYCATCH_MESSAGE);

    /* Controls menu waiting for a key to bind (ui_shared.c sets the cvar):
     * every button is its game key, so any of them can be bound. START
     * still sends Escape, which cancels. */
    if ((catchers & KEYCATCH_UI) && Cvar_VariableIntegerValue("ui_ps3bindwait"))
        return pad_game_key[b];

    if (menu) {
        switch (b) {
        case PB_CROSS:  return K_ENTER;
        case PB_CIRCLE: return K_ESCAPE;
        case PB_SQUARE: return K_MOUSE1;    /* click at the pointer (right stick) */
        case PB_UP:     case PB_LS_UP:    return K_UPARROW;
        case PB_DOWN:   case PB_LS_DOWN:  return K_DOWNARROW;
        case PB_LEFT:   case PB_LS_LEFT:  return K_LEFTARROW;
        case PB_RIGHT:  case PB_LS_RIGHT: return K_RIGHTARROW;
        case PB_L1:     return K_PGUP;      /* console scrollback */
        case PB_R1:     return K_PGDN;
        default: break;
        }
    }
    return pad_game_key[b];
}

static void pad_release_all(void)
{
    int b;

    for (b = 0; b < PB_COUNT; b++) {
        if (pad_down[b] && pad_sent[b])
            Com_QueueEvent(0, SE_KEY, pad_sent[b], qfalse, 0, NULL);
        pad_down[b] = 0;
        pad_sent[b] = 0;
        pad_repeat_at[b] = 0;
    }
    if (select_hold)
        Com_QueueEvent(0, SE_KEY, K_SELECT_HOLD, qfalse, 0, NULL);
    select_hold = 0;
    for (b = 0; b < 4; b++) {
        if (ps3_axis_prev[b])
            Com_QueueEvent(0, SE_JOYSTICK_AXIS, b == 2 ? 4 : b == 3 ? 3 : b, 0, 0, NULL);
        ps3_axis_prev[b] = 0;
    }
}

void PS3_Input_Init(void)
{
    ioPadInit(7);
    /* PRE_L2/PRE_R2 read 0 unless pressure mode is on. */
    ioPadSetPressMode(0, PAD_PRESS_MODE_ON);

    memset(pad_down, 0, sizeof(pad_down));
    memset(pad_sent, 0, sizeof(pad_sent));
    memset(ps3_axis_prev, 0, sizeof(ps3_axis_prev));
    ps3_cursor_accum_x = ps3_cursor_accum_y = 0.0f;

    KB_Init();
    Mouse_Init();

    ps3_log("[input] pad + USB keyboard + USB mouse initialized");
}

void PS3_Input_Shutdown(void)
{
    ioPadEnd();
    KB_Shutdown();
    Mouse_Shutdown();
}

/* Sticky port: keep the current one while it is connected, else take the
 * first connected one. Runs every frame: BT sleep / USB replug re-enumerate. */
static void PS3_SelectActivePadPort(void)
{
    int i;

    if (ps3_pad_info.status[ps3_active_pad_port])
        return;
    for (i = 0; i < 7; i++) {
        if (ps3_pad_info.status[i]) {
            ps3_active_pad_port = i;
            return;
        }
    }
}

void PS3_Input_Frame(void)
{
    int cur[PB_COUNT];
    int catchers, in_menu, b;

    KB_Frame(qfalse);
    Mouse_Frame(qfalse);

    ioPadGetInfo(&ps3_pad_info);
    PS3_SelectActivePadPort();
    if (!ps3_pad_info.status[ps3_active_pad_port]) {
        pad_release_all();
        return;
    }
    /* Keep going on len == 0: ps3_pad_data still holds the last values, and
     * skipping would freeze the menu cursor accumulator. */
    if (ioPadGetData(ps3_active_pad_port, &ps3_pad_data) != 0)
        return;

    read_buttons(&ps3_pad_data, cur);

    /* the XMB keyboard (ps3_osk.c) has the pad; the buttons still held when
     * it closes (the X that accepted) are not new presses for the menu */
    if (PS3_OSK_Active()) {
        pad_release_all();
        return;
    }
    if (ps3_osk_just_closed) {
        ps3_osk_just_closed = 0;
        for (b = 0; b < PB_COUNT; b++) {
            pad_down[b] = cur[b];
            pad_sent[b] = 0;
            pad_repeat_at[b] = 0;
        }
        return;
    }

    catchers = Key_GetCatcher();
    in_menu = (catchers & (KEYCATCH_UI | KEYCATCH_CGAME)) ? 1 : 0;

    /* SELECT is a modifier: SELECT + TRIANGLE opens the console. On its own
     * it acts on release (a tap), so the combo never triggers its binding. */
    if (cur[PB_SELECT] && !pad_down[PB_SELECT]) {
        select_combo = 0;
        select_hold = 0;
        select_at = Sys_Milliseconds();
    }
    if (cur[PB_SELECT] && cur[PB_TRIANGLE] && !pad_down[PB_TRIANGLE]) {
        Com_QueueEvent(0, SE_KEY, K_CONSOLE, qtrue, 0, NULL);
        Com_QueueEvent(0, SE_KEY, K_CONSOLE, qfalse, 0, NULL);
        pad_down[PB_TRIANGLE] = 1;
        pad_sent[PB_TRIANGLE] = 0;      /* swallowed: no release to send */
        select_combo = 1;
    }
    if (cur[PB_SELECT] && pad_down[PB_SELECT] && !select_combo && !select_hold &&
        Sys_Milliseconds() - select_at >= SELECT_HOLD_MS &&
        (!(catchers & (KEYCATCH_UI | KEYCATCH_CGAME | KEYCATCH_CONSOLE | KEYCATCH_MESSAGE)) ||
         ((catchers & KEYCATCH_UI) && Cvar_VariableIntegerValue("ui_ps3bindwait")))) {
        Com_QueueEvent(0, SE_KEY, K_SELECT_HOLD, qtrue, 0, NULL);
        select_hold = 1;
    }
    if (cur[PB_SELECT] != pad_down[PB_SELECT]) {
        if (!cur[PB_SELECT] && select_hold) {
            Com_QueueEvent(0, SE_KEY, K_SELECT_HOLD, qfalse, 0, NULL);
            select_hold = 0;
        } else if (!cur[PB_SELECT] && !select_combo) {
            int key = pad_key_for(PB_SELECT, catchers);
            Com_QueueEvent(0, SE_KEY, key, qtrue, 0, NULL);
            Com_QueueEvent(0, SE_KEY, key, qfalse, 0, NULL);
        }
        pad_down[PB_SELECT] = cur[PB_SELECT];
        pad_sent[PB_SELECT] = 0;
    }

    for (b = 0; b < PB_COUNT; b++) {
        if (b == PB_SELECT || cur[b] == pad_down[b])
            continue;
        if (cur[b]) {
            /* the key is picked at press time and released as the same key,
             * so a menu closing mid-press never leaves a key stuck down */
            pad_sent[b] = pad_key_for(b, catchers);
            if (pad_sent[b])
                Com_QueueEvent(0, SE_KEY, pad_sent[b], qtrue, 0, NULL);
            pad_repeat_at[b] = 0;
            if (pad_sent[b] == K_UPARROW || pad_sent[b] == K_DOWNARROW ||
                pad_sent[b] == K_LEFTARROW || pad_sent[b] == K_RIGHTARROW)
                pad_repeat_at[b] = Sys_Milliseconds() + PAD_REPEAT_DELAY;
        } else if (pad_sent[b]) {
            Com_QueueEvent(0, SE_KEY, pad_sent[b], qfalse, 0, NULL);
            pad_sent[b] = 0;
            pad_repeat_at[b] = 0;
        }
        pad_down[b] = cur[b];
    }

    /* held arrows repeat, like a keyboard */
    {
        int now = Sys_Milliseconds();

        for (b = 0; b < PB_COUNT; b++) {
            if (pad_down[b] && pad_repeat_at[b] && now >= pad_repeat_at[b]) {
                Com_QueueEvent(0, SE_KEY, pad_sent[b], qtrue, 0, NULL);
                pad_repeat_at[b] = now + PAD_REPEAT_RATE;
            }
        }
    }

    {
        int lx = (int)ps3_pad_data.ANA_L_H - STICK_CENTER;
        int ly = (int)ps3_pad_data.ANA_L_V - STICK_CENTER;
        int rx = (int)ps3_pad_data.ANA_R_H - STICK_CENTER;
        int ry = (int)ps3_pad_data.ANA_R_V - STICK_CENTER;
        int axis[4], engine_axis[4] = { 0, 1, 4, 3 };

        if (abs(lx) < STICK_DEADZONE) lx = 0;
        if (abs(ly) < STICK_DEADZONE) ly = 0;
        if (abs(rx) < STICK_DEADZONE) rx = 0;
        if (abs(ry) < STICK_DEADZONE) ry = 0;

        if (in_menu) {
            /* the left stick navigates like the d-pad (see read_buttons);
             * the right stick is a pointer for screens that need one */
            if (rx || ry) {
                float fx = (float)(rx > 0 ? rx - STICK_DEADZONE : rx + STICK_DEADZONE) / (float)STICK_RANGE;
                float fy = (float)(ry > 0 ? ry - STICK_DEADZONE : ry + STICK_DEADZONE) / (float)STICK_RANGE;
                int dx, dy;

                if (!rx) fx = 0.0f;
                if (!ry) fy = 0.0f;
                if (fx > 1.0f) fx = 1.0f;
                if (fx < -1.0f) fx = -1.0f;
                if (fy > 1.0f) fy = 1.0f;
                if (fy < -1.0f) fy = -1.0f;
                /* precise near the center, fast at full tilt; the float
                 * accumulator keeps slow movement from rounding to zero */
                ps3_cursor_accum_x += fx * (0.25f + 0.75f * fabsf(fx)) * POINTER_MAX_SPEED;
                ps3_cursor_accum_y += fy * (0.25f + 0.75f * fabsf(fy)) * POINTER_MAX_SPEED;
                dx = (int)ps3_cursor_accum_x;
                dy = (int)ps3_cursor_accum_y;
                ps3_cursor_accum_x -= (float)dx;
                ps3_cursor_accum_y -= (float)dy;
                if (dx || dy)
                    Com_QueueEvent(0, SE_MOUSE, dx, dy, 0, NULL);
            }
            lx = ly = rx = ry = 0;      /* the player stops moving in menus */
        }

        /* scaled to the engine's +/-32K joystick axis range */
        axis[0] = lx * 256;
        axis[1] = ly * 256;
        axis[2] = rx * 256;
        axis[3] = ry * 256;
        for (b = 0; b < 4; b++) {
            if (axis[b] != ps3_axis_prev[b]) {
                Com_QueueEvent(0, SE_JOYSTICK_AXIS, engine_axis[b], axis[b], 0, NULL);
                ps3_axis_prev[b] = axis[b];
            }
        }
    }
}

/* Called by the renderer's GLimp_Init (after the configs are executed). */
void IN_Init(void *windowData)
{
    (void)windowData;

    PS3_SetDefaultBind(K_JOY1,  "+moveup");     /* cross     jump */
    PS3_SetDefaultBind(K_JOY2,  "+movedown");   /* circle    crouch */
    PS3_SetDefaultBind(K_JOY3,  "+reload");     /* square */
    PS3_SetDefaultBind(K_JOY4,  "+activate");   /* triangle  use */
    PS3_SetDefaultBind(K_JOY5,  "weapprev");    /* L1 */
    PS3_SetDefaultBind(K_JOY6,  "weapnext");    /* R1 */
    /* patch 14 moved alt fire to R3 for the lock-on: move the old defaults
     * of a saved config once (custom binds stay) */
    if (Cvar_VariableIntegerValue("in_ps3binds") < 2) {
        char *b = Key_GetBinding(K_JOY7);
        if (!b || !b[0] || !Q_stricmp(b, "weapalt"))
            Key_SetBinding(K_JOY7, "+lockon");
        b = Key_GetBinding(K_JOY10);
        if (!b || !b[0] || !Q_stricmp(b, "+zoom"))
            Key_SetBinding(K_JOY10, "weapalt");
        Cvar_Get("in_ps3binds", "0", CVAR_ARCHIVE);
        Cvar_Set("in_ps3binds", "2");
    }
    PS3_SetDefaultBind(K_JOY7,  "+lockon");     /* L2        lock-on (cl_input.c) */
    PS3_SetDefaultBind(K_JOY8,  "+attack");     /* R2 */
    PS3_SetDefaultBind(K_JOY9,  "+sprint");     /* L3 */
    PS3_SetDefaultBind(K_JOY10, "weapalt");     /* R3        alt fire / scope */
    PS3_SetDefaultBind(K_JOY11, "notebook");    /* select */
    /* patch 17: the binoculars (without a button since patch 14) go on
     * SELECT held; d-up gets the kick back (patch 16 had put them there) */
    if (Cvar_VariableIntegerValue("in_ps3binds") < 4) {
        char *b = Key_GetBinding(K_JOY12);
        if (!b || !b[0] || !Q_stricmp(b, "+zoom"))
            Key_SetBinding(K_JOY12, "+kick");
        Cvar_Get("in_ps3binds", "0", CVAR_ARCHIVE);
        Cvar_Set("in_ps3binds", "4");
    }
    PS3_SetDefaultBind(K_SELECT_HOLD, "+zoom"); /* select held  binoculars */
    PS3_SetDefaultBind(K_JOY12, "+kick");       /* d-up */
    PS3_SetDefaultBind(K_JOY13, "+useitem");    /* d-down */
    PS3_SetDefaultBind(K_JOY14, "+leanleft");   /* d-left */
    PS3_SetDefaultBind(K_JOY15, "+leanright");  /* d-right */

    Cvar_Set("in_joystick", "1");
    Cvar_Set("j_pitch_axis", "3");
    Cvar_Set("j_yaw_axis", "4");
    Cvar_Set("j_forward_axis", "1");
    Cvar_Set("j_side_axis", "0");
    Cvar_Set("j_pitch", "0.011");
    Cvar_Set("j_yaw", "-0.011");
    Cvar_Set("j_forward", "-0.25");
    Cvar_Set("j_side", "0.25");
    /* the stick look uses "sensitivity" 1..10 (cl_input.c); values from the
     * menu's old 1..50 scale go back to the default */
    if (Cvar_VariableValue("sensitivity") > 10)
        Cvar_Set("sensitivity", "5");

    PS3_OSK_Init();
    ps3_log("IN_Init: default DS3 binds set");
}

void IN_Frame(void) {}

void IN_Shutdown(void) {}

void IN_Restart(void) {}

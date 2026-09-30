/* ps3_net_null.c -- replaces net_ip.c: this is a single-player port.
 *
 * The local client and server talk through the engine's loopback queues
 * (NET_GetLoopPacket / NET_SendLoopPacket in net_chan.c), which need no
 * sockets. Keeping the PS3 net module out saves memory and a whole class of
 * PSL1GHT socket quirks. Anything non-loopback is dropped. */

#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"

void NET_Init(void)
{
    Com_Printf("NET_Init: networking disabled (single player build)\n");
}

void NET_Shutdown(void) {}
void NET_Restart_f(void) {}
void NET_Config(qboolean enableNetworking) { (void)enableNetworking; }
void NET_JoinMulticast6(void) {}
void NET_LeaveMulticast6(void) {}

void NET_Sleep(int msec)
{
    Sys_Sleep(msec);
}

void Sys_SendPacket(int length, const void *data, netadr_t to)
{
    (void)length; (void)data; (void)to;
}

qboolean Sys_StringToAdr(const char *s, netadr_t *a, netadrtype_t family)
{
    (void)s; (void)family;
    Com_Memset(a, 0, sizeof(*a));
    return qfalse;
}

qboolean Sys_IsLANAddress(netadr_t adr)
{
    return adr.type == NA_LOOPBACK;
}

void Sys_ShowIP(void) {}

qboolean NET_CompareBaseAdrMask(netadr_t a, netadr_t b, int netmask)
{
    (void)netmask;
    if (a.type != b.type)
        return qfalse;
    if (a.type == NA_LOOPBACK || a.type == NA_BOT)
        return qtrue;
    return !memcmp(a.ip, b.ip, sizeof(a.ip)) ? qtrue : qfalse;
}

qboolean NET_CompareBaseAdr(netadr_t a, netadr_t b)
{
    return NET_CompareBaseAdrMask(a, b, -1);
}

qboolean NET_CompareAdr(netadr_t a, netadr_t b)
{
    if (!NET_CompareBaseAdr(a, b))
        return qfalse;
    if (a.type == NA_IP || a.type == NA_IP6)
        return a.port == b.port ? qtrue : qfalse;
    return qtrue;
}

qboolean NET_IsLocalAddress(netadr_t adr)
{
    return adr.type == NA_LOOPBACK;
}

const char *NET_AdrToString(netadr_t a)
{
    static char s[NET_ADDRSTRMAXLEN];

    if (a.type == NA_LOOPBACK)
        Com_sprintf(s, sizeof(s), "loopback");
    else if (a.type == NA_BOT)
        Com_sprintf(s, sizeof(s), "bot");
    else
        Com_sprintf(s, sizeof(s), "%i.%i.%i.%i", a.ip[0], a.ip[1], a.ip[2], a.ip[3]);
    return s;
}

const char *NET_AdrToStringwPort(netadr_t a)
{
    return NET_AdrToString(a);
}

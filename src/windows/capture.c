/*
 * capture.c
 * (C) 2018, all rights reserved,
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <windows.h>

#include "capture.h"
#include "cfg.h"
#include "log.h"
#include "socket.h"

#define UINT8   unsigned char
#define UINT16  unsigned short
#include "windivert.h"

/*
 * Pseudo ethhdr stores the DIVERT_ADDRESS
 */
struct pethhdr_s
{
    uint32_t if_idx;            // Packet's interface
    uint8_t  direction;         // Packet's direction
    uint8_t  pad1;              // Padding (0x0)
    uint32_t sub_if_idx;        // Packet's sub-interface
    uint16_t pad2;              // Padding (0x0)
    uint16_t proto;             // ETH_P_IP
} __attribute__((__packed__));

/*
 * Divert device handle.
 */
HANDLE handle = INVALID_HANDLE_VALUE;

/*
 * Initialises the packet capture device.
 */
void init_capture(void)
{
    handle = WinDivertOpen(
        "ip and "
        "!loopback and "
        "(outbound? tcp.DstPort == 80 or"
        " tcp.DstPort == 443 or"
        " udp.DstPort == 53 :"
        " icmp.Type == 11 and icmp.Code == 0)",
        WINDIVERT_LAYER_NETWORK, -501, 0);
    if (handle == INVALID_HANDLE_VALUE)
    {
        error("unable to open divert packet capture handle");
    }
}

/*
 * Get a captured packet.
 */
size_t get_packet(uint8_t *buff, size_t len)
{
    UINT offset = sizeof(struct pethhdr_s);
    if (len <= offset)
    {
        error("unable to read packet; buffer is too small");
    }
    UINT read_len;
    WINDIVERT_ADDRESS addr;
    do
    {
        if (!WinDivertRecv(handle, (PVOID)(buff+offset), (UINT)(len-offset),
            &addr, &read_len))
        {
            warning("unable to read packet from divert packet capture handle");
            continue;
        }
    }
    while (addr.Direction == WINDIVERT_DIRECTION_INBOUND);  // Drop icmp.
    struct pethhdr_s *peth_header = (struct pethhdr_s *)buff;
    peth_header->direction  = addr.Direction;
    peth_header->if_idx     = addr.IfIdx;
    peth_header->sub_if_idx = addr.SubIfIdx;
    peth_header->pad1       = 0x0;
    peth_header->pad2       = 0x0;
    peth_header->proto      = htons(ETH_P_IP);

    WinDivertHelperCalcChecksums((PVOID)(buff+offset), (UINT)read_len,
        NULL, 0);

    return (size_t)(read_len+offset);
}

/*
 * Re-inject a captured packet.
 */
void inject_packet(uint8_t *buff, size_t len)
{
    UINT offset = sizeof(struct pethhdr_s);
    if (len <= offset)
    {
        warning("unable to inject packet; buffer is too small");
    }
    struct pethhdr_s *peth_header = (struct pethhdr_s *)buff;
    WINDIVERT_ADDRESS addr;
    memset(&addr, 0, sizeof(addr));
    addr.Direction = peth_header->direction;
    addr.IfIdx     = peth_header->if_idx;
    addr.SubIfIdx  = peth_header->sub_if_idx;
    addr.Impostor  = 1;

    len -= offset;
    buff += offset;

    UINT write_len;
    if (!WinDivertSend(handle, (PVOID)buff, (UINT)len, &addr, &write_len) ||
        (UINT)len != write_len)
    {
        warning("unable to inject packet of size " SIZE_T_FMT " to "
            "divert packet capture handle", len);
    }
}


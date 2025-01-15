/*  netprof.c
 *
 *  Copyright (C) 2025 Toxic All Rights Reserved.
 *
 *  This file is part of Toxic. Toxic is free software licensed
 *  under the GNU General Public License 3.0.
 */

#include <stdint.h>
#include <stdio.h>

#include "netprof.h"

#ifdef TOX_EXPERIMENTAL
#include <tox/tox_private.h>
#endif  // TOX_EXPERIMENTAL

static void log_tcp_packet_id(FILE *fp, unsigned int id, uint64_t total, uint64_t TCP_id_sent, uint64_t TCP_id_recv)
{
    switch (id) {
        case TOX_NETPROF_PACKET_ID_ZERO:
        case TOX_NETPROF_PACKET_ID_ONE:
        case TOX_NETPROF_PACKET_ID_TWO:
        case TOX_NETPROF_PACKET_ID_TCP_DISCONNECT:
        case TOX_NETPROF_PACKET_ID_FOUR:
        case TOX_NETPROF_PACKET_ID_TCP_PONG:
        case TOX_NETPROF_PACKET_ID_TCP_OOB_SEND:
        case TOX_NETPROF_PACKET_ID_TCP_OOB_RECV:
        case TOX_NETPROF_PACKET_ID_TCP_ONION_REQUEST:
        case TOX_NETPROF_PACKET_ID_TCP_ONION_RESPONSE:
        case TOX_NETPROF_PACKET_ID_TCP_DATA: {
            if (TCP_id_recv || TCP_id_sent) {
                fprintf(fp, "0x%02x (total):     %lu (%.2f%%)\n", id, TCP_id_sent + TCP_id_recv,
                        ((float)TCP_id_recv + TCP_id_sent) / total * 100.0);
            }

            if (TCP_id_sent) {
                fprintf(fp, "0x%02x (sent):      %lu (%.2f%%)\n", id, TCP_id_sent, (float)TCP_id_sent / total * 100.0);
            }

            if (TCP_id_recv) {
                fprintf(fp, "0x%02x (recv):      %lu (%.2f%%)\n", id, TCP_id_recv, (float)TCP_id_recv / total * 100.0);
            }

            break;
        }

        default:
            return;
    }
}

static void log_udp_packet_id(FILE *fp, unsigned int id, uint64_t total, uint64_t UDP_id_sent, uint64_t UDP_id_recv)
{
    switch (id) {
        case TOX_NETPROF_PACKET_ID_ZERO:
        case TOX_NETPROF_PACKET_ID_ONE:
        case TOX_NETPROF_PACKET_ID_TWO:
        case TOX_NETPROF_PACKET_ID_FOUR:
        case TOX_NETPROF_PACKET_ID_COOKIE_REQUEST:
        case TOX_NETPROF_PACKET_ID_COOKIE_RESPONSE:
        case TOX_NETPROF_PACKET_ID_CRYPTO_HS:
        case TOX_NETPROF_PACKET_ID_CRYPTO_DATA:
        case TOX_NETPROF_PACKET_ID_CRYPTO:
        case TOX_NETPROF_PACKET_ID_LAN_DISCOVERY:
        case TOX_NETPROF_PACKET_ID_GC_HANDSHAKE:
        case TOX_NETPROF_PACKET_ID_GC_LOSSLESS:
        case TOX_NETPROF_PACKET_ID_GC_LOSSY:
        case TOX_NETPROF_PACKET_ID_ONION_SEND_INITIAL:
        case TOX_NETPROF_PACKET_ID_ONION_SEND_1:
        case TOX_NETPROF_PACKET_ID_ONION_SEND_2:
        case TOX_NETPROF_PACKET_ID_ANNOUNCE_REQUEST:
        case TOX_NETPROF_PACKET_ID_ANNOUNCE_REQUEST_OLD:
        case TOX_NETPROF_PACKET_ID_ANNOUNCE_RESPONSE:
        case TOX_NETPROF_PACKET_ID_ANNOUNCE_RESPONSE_OLD:
        case TOX_NETPROF_PACKET_ID_ONION_DATA_REQUEST:
        case TOX_NETPROF_PACKET_ID_ONION_DATA_RESPONSE:
        case TOX_NETPROF_PACKET_ID_ONION_RECV_3:
        case TOX_NETPROF_PACKET_ID_ONION_RECV_2:
        case TOX_NETPROF_PACKET_ID_ONION_RECV_1:
        case TOX_NETPROF_PACKET_ID_BOOTSTRAP_INFO:
        case TOX_NETPROF_PACKET_ID_FORWARD_REQUEST:
        case TOX_NETPROF_PACKET_ID_FORWARDING:
        case TOX_NETPROF_PACKET_ID_FORWARD_REPLY:
        case TOX_NETPROF_PACKET_ID_DATA_SEARCH_REQUEST:
        case TOX_NETPROF_PACKET_ID_DATA_SEARCH_RESPONSE:
        case TOX_NETPROF_PACKET_ID_DATA_RETRIEVE_REQUEST:
        case TOX_NETPROF_PACKET_ID_DATA_RETRIEVE_RESPONSE:
        case TOX_NETPROF_PACKET_ID_STORE_ANNOUNCE_REQUEST:
        case TOX_NETPROF_PACKET_ID_STORE_ANNOUNCE_RESPONSE: {
            if (UDP_id_recv || UDP_id_sent) {
                fprintf(fp, "0x%02x (total):     %lu (%.2f%%)\n", id, UDP_id_sent + UDP_id_recv,
                        ((float)UDP_id_recv + UDP_id_sent) / total * 100.0);
            }

            if (UDP_id_sent) {
                fprintf(fp, "0x%02x (sent):      %lu (%.2f%%)\n", id, UDP_id_sent, (float)UDP_id_sent / total * 100.0);
            }

            if (UDP_id_recv) {
                fprintf(fp, "0x%02x (recv):      %lu (%.2f%%)\n", id, UDP_id_recv, (float)UDP_id_recv / total * 100.0);
            }

            break;
        }

        default:
            return;
    }
}

static void dump_packet_id_counts(const Tox *tox, FILE *fp, uint64_t total_count, Tox_Netprof_Packet_Type packet_type)
{
    if (packet_type == TOX_NETPROF_PACKET_TYPE_TCP) {
        fprintf(fp, "--- TCP packet counts by packet ID --- \n");
    } else {
        fprintf(fp, "--- UDP packet counts by packet ID --- \n");
    }

    for (unsigned long i = TOX_NETPROF_PACKET_ID_ZERO; i <= TOX_NETPROF_PACKET_ID_BOOTSTRAP_INFO; ++i) {
        const uint64_t id_count_sent = tox_netprof_get_packet_id_count(tox, packet_type, i, TOX_NETPROF_DIRECTION_SENT);
        const uint64_t id_count_recv = tox_netprof_get_packet_id_count(tox, packet_type, i, TOX_NETPROF_DIRECTION_RECV);

        if (packet_type == TOX_NETPROF_PACKET_TYPE_TCP) {
            log_tcp_packet_id(fp, i, total_count, id_count_sent, id_count_recv);
        } else {
            log_udp_packet_id(fp, i, total_count, id_count_sent, id_count_recv);
        }
    }

    fprintf(fp, "\n\n");
}

static void dump_packet_id_bytes(const Tox *tox, FILE *fp, uint64_t total_bytes, Tox_Netprof_Packet_Type packet_type)
{
    if (packet_type == TOX_NETPROF_PACKET_TYPE_TCP) {
        fprintf(fp, "--- TCP byte counts by packet ID --- \n");
    } else {
        fprintf(fp, "--- UDP byte counts by packet ID --- \n");
    }

    for (unsigned long i = TOX_NETPROF_PACKET_ID_ZERO; i <= TOX_NETPROF_PACKET_ID_BOOTSTRAP_INFO; ++i) {
        const uint64_t id_bytes_sent = tox_netprof_get_packet_id_bytes(tox, packet_type, i, TOX_NETPROF_DIRECTION_SENT);
        const uint64_t id_bytes_recv = tox_netprof_get_packet_id_bytes(tox, packet_type, i, TOX_NETPROF_DIRECTION_RECV);

        if (packet_type == TOX_NETPROF_PACKET_TYPE_TCP) {
            log_tcp_packet_id(fp, i, total_bytes, id_bytes_sent, id_bytes_recv);
        } else {
            log_udp_packet_id(fp, i, total_bytes, id_bytes_sent, id_bytes_recv);
        }
    }

    fprintf(fp, "\n\n");
}

static void dump_packet_count_totals(const Tox *tox, FILE *fp, uint64_t total_packet_count,
                                     uint64_t UDP_count_sent, uint64_t UDP_count_recv,
                                     uint64_t TCP_count_sent, uint64_t TCP_count_recv)
{
    const uint64_t total_UDP_count = UDP_count_sent + UDP_count_recv;
    const uint64_t total_TCP_count = TCP_count_sent + TCP_count_recv;
    const uint64_t total_packet_count_sent = UDP_count_sent + TCP_count_sent;
    const uint64_t total_packet_count_recv = UDP_count_recv + TCP_count_recv;

    fprintf(fp, "--- Total packet counts --- \n");

    fprintf(fp, "Total packets:          %lu\n", total_packet_count);

    fprintf(fp, "Total packets sent:     %lu (%.2f%%)\n", total_packet_count_sent,
            (float)total_packet_count_sent / total_packet_count * 100.0);

    fprintf(fp, "Total packets recv:     %lu (%.2f%%)\n", total_packet_count_recv,
            (float)total_packet_count_recv / total_packet_count * 100.0);

    fprintf(fp, "total UDP packets:      %lu (%.2f%%)\n", total_UDP_count,
            (float)total_UDP_count / total_packet_count * 100.0);

    fprintf(fp, "UDP packets sent:       %lu (%.2f%%)\n", UDP_count_sent,
            (float)UDP_count_sent / total_packet_count * 100.0);

    fprintf(fp, "UDP packets recv:       %lu (%.2f%%)\n", UDP_count_recv,
            (float)UDP_count_recv / total_packet_count * 100.0);

    fprintf(fp, "Total TCP packets:      %lu (%.2f%%)\n", total_TCP_count,
            (float)total_TCP_count / total_packet_count * 100.0);

    fprintf(fp, "TCP packets sent:       %lu (%.2f%%)\n", TCP_count_sent,
            (float)TCP_count_sent / total_packet_count * 100.0);

    fprintf(fp, "TCP packets recv:       %lu (%.2f%%)\n", TCP_count_recv,
            (float)TCP_count_recv / total_packet_count * 100.0);

    fprintf(fp, "\n\n");
}

static void dump_packet_bytes_totals(const Tox *tox, FILE *fp, const uint64_t total_bytes,
                                     const uint64_t UDP_bytes_sent, const uint64_t UDP_bytes_recv,
                                     const uint64_t TCP_bytes_sent, const uint64_t TCP_bytes_recv)
{
    const uint64_t total_UDP_bytes = UDP_bytes_sent + UDP_bytes_recv;
    const uint64_t total_TCP_bytes = TCP_bytes_sent + TCP_bytes_recv;
    const uint64_t total_bytes_sent = UDP_bytes_sent + TCP_bytes_sent;
    const uint64_t total_bytes_recv = UDP_bytes_recv + TCP_bytes_recv;

    fprintf(fp, "--- Total byte counts --- \n");

    fprintf(fp, "Total bytes:            %lu\n", total_bytes);

    fprintf(fp, "Total bytes sent:       %lu (%.2f%%)\n", total_bytes_sent,
            (float)total_bytes_sent / total_bytes * 100.0);

    fprintf(fp, "Total bytes recv:       %lu (%.2f%%)\n", total_bytes_recv,
            (float)total_bytes_recv / total_bytes * 100.0);

    fprintf(fp, "Total UDP bytes:        %lu (%.2f%%)\n", total_UDP_bytes,
            (float)total_UDP_bytes / total_bytes * 100.0);

    fprintf(fp, "UDP bytes sent:         %lu (%.2f%%)\n", UDP_bytes_sent,
            (float)UDP_bytes_sent / total_bytes * 100.0);

    fprintf(fp, "UDP bytes recv:         %lu (%.2f%%)\n", UDP_bytes_recv,
            (float)UDP_bytes_recv / total_bytes * 100.0);

    fprintf(fp, "Total TCP bytes:        %lu (%.2f%%)\n", total_TCP_bytes,
            (float)total_TCP_bytes / total_bytes * 100.0);

    fprintf(fp, "TCP bytes sent:         %lu (%.2f%%)\n", TCP_bytes_sent,
            (float)TCP_bytes_sent / total_bytes * 100.0);

    fprintf(fp, "TCP bytes recv:         %lu (%.2f%%)\n", TCP_bytes_recv,
            (float)TCP_bytes_recv / total_bytes * 100.0);

    fprintf(fp, "\n\n");
}

void netprof_log_dump(const Tox *tox, FILE *fp, time_t run_time)
{
    if (fp == NULL) {
        fprintf(stderr, "Failed to dump network statistics: null file pointer\n");
        return;
    }

    const uint64_t UDP_count_sent = tox_netprof_get_packet_total_count(tox, TOX_NETPROF_PACKET_TYPE_UDP,
                                    TOX_NETPROF_DIRECTION_SENT);
    const uint64_t UDP_count_recv = tox_netprof_get_packet_total_count(tox, TOX_NETPROF_PACKET_TYPE_UDP,
                                    TOX_NETPROF_DIRECTION_RECV);
    const uint64_t TCP_count_sent = tox_netprof_get_packet_total_count(tox, TOX_NETPROF_PACKET_TYPE_TCP,
                                    TOX_NETPROF_DIRECTION_SENT);
    const uint64_t TCP_count_recv = tox_netprof_get_packet_total_count(tox, TOX_NETPROF_PACKET_TYPE_TCP,
                                    TOX_NETPROF_DIRECTION_RECV);
    const uint64_t UDP_bytes_sent = tox_netprof_get_packet_total_bytes(tox, TOX_NETPROF_PACKET_TYPE_UDP,
                                    TOX_NETPROF_DIRECTION_SENT);
    const uint64_t UDP_bytes_recv = tox_netprof_get_packet_total_bytes(tox, TOX_NETPROF_PACKET_TYPE_UDP,
                                    TOX_NETPROF_DIRECTION_RECV);
    const uint64_t TCP_bytes_sent = tox_netprof_get_packet_total_bytes(tox, TOX_NETPROF_PACKET_TYPE_TCP,
                                    TOX_NETPROF_DIRECTION_SENT);
    const uint64_t TCP_bytes_recv = tox_netprof_get_packet_total_bytes(tox, TOX_NETPROF_PACKET_TYPE_TCP,
                                    TOX_NETPROF_DIRECTION_RECV);

    const uint64_t total_count = UDP_count_sent + UDP_count_recv + TCP_count_sent + TCP_count_recv;
    const uint64_t total_bytes = UDP_bytes_sent + UDP_bytes_recv + TCP_bytes_sent + TCP_bytes_recv;

    fprintf(fp, "--- Tox network profile log dump ---\n");
    fprintf(fp, "Run time: %lu seconds\n", run_time);

    if (run_time && total_count && total_bytes) {
        fprintf(fp, "Average kilobytes per second: %.2f\n", ((float)total_bytes / run_time) / 1000.0);
        fprintf(fp, "Average packets per second: %lu\n", total_count / run_time);
        fprintf(fp, "Average packet size: %lu bytes\n", total_bytes / total_count);
        fprintf(fp, "\n");
    }

    dump_packet_count_totals(tox, fp, total_count, UDP_count_sent, UDP_count_recv, TCP_count_sent, TCP_count_recv);
    dump_packet_bytes_totals(tox, fp, total_bytes, UDP_bytes_sent, UDP_bytes_recv, TCP_bytes_sent, TCP_bytes_recv);
    dump_packet_id_counts(tox, fp, total_count, TOX_NETPROF_PACKET_TYPE_TCP);
    dump_packet_id_counts(tox, fp, total_count, TOX_NETPROF_PACKET_TYPE_UDP);
    dump_packet_id_bytes(tox, fp, total_bytes, TOX_NETPROF_PACKET_TYPE_TCP);
    dump_packet_id_bytes(tox, fp, total_bytes, TOX_NETPROF_PACKET_TYPE_UDP);

    fflush(fp);
}

uint64_t netprof_get_bytes_up(const Tox *tox)
{
    const uint64_t UDP_bytes_sent = tox_netprof_get_packet_total_bytes(tox, TOX_NETPROF_PACKET_TYPE_UDP,
                                    TOX_NETPROF_DIRECTION_SENT);
    const uint64_t TCP_bytes_sent = tox_netprof_get_packet_total_bytes(tox, TOX_NETPROF_PACKET_TYPE_TCP,
                                    TOX_NETPROF_DIRECTION_SENT);

    return UDP_bytes_sent + TCP_bytes_sent;

}

uint64_t netprof_get_bytes_down(const Tox *tox)
{
    const uint64_t UDP_bytes_recv = tox_netprof_get_packet_total_bytes(tox, TOX_NETPROF_PACKET_TYPE_UDP,
                                    TOX_NETPROF_DIRECTION_RECV);
    const uint64_t TCP_bytes_recv = tox_netprof_get_packet_total_bytes(tox, TOX_NETPROF_PACKET_TYPE_TCP,
                                    TOX_NETPROF_DIRECTION_RECV);

    return UDP_bytes_recv + TCP_bytes_recv;
}

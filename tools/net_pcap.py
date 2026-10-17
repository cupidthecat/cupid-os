#!/usr/bin/env python3
"""Validate Cupid OS network captures with the Python standard library.

The checker reads classic Ethernet PCAP files written by QEMU and requires:

* ARP requests and replies
* DHCP DISCOVER, OFFER, REQUEST, and ACK messages
* ICMP echo requests and replies
* One guest client handshake to a public destination
* One inbound handshake to that guest
* A separate, sequence-valid bidirectional teardown for each TCP direction
* Valid IPv4 header checksums

Usage:
    python tools/net_pcap.py tests/rtl8139.pcap [tests/e1000.pcap ...]
"""

from __future__ import annotations

import ipaddress
import struct
import sys
from pathlib import Path


class PcapError(ValueError):
    """A capture is malformed or uses an unsupported link format."""


def _pcap_packets(path: Path) -> list[bytes]:
    try:
        image = path.read_bytes()
    except OSError as error:
        raise PcapError(f"cannot read capture: {error}") from error
    if len(image) < 24:
        raise PcapError("global header is truncated")

    byte_order = {
        b"\xd4\xc3\xb2\xa1": "<",
        b"\xa1\xb2\xc3\xd4": ">",
        b"\x4d\x3c\xb2\xa1": "<",
        b"\xa1\xb2\x3c\x4d": ">",
    }.get(image[:4])
    if byte_order is None:
        raise PcapError("unknown PCAP magic")

    (
        _magic,
        major,
        minor,
        _zone,
        _accuracy,
        snap_length,
        link_type,
    ) = struct.unpack_from(byte_order + "IHHIIII", image, 0)
    if (major, minor) != (2, 4):
        raise PcapError(f"unsupported PCAP version {major}.{minor}")
    if snap_length == 0:
        raise PcapError("capture has a zero snapshot length")
    if link_type != 1:
        raise PcapError(f"unsupported link type {link_type}")

    packets = []
    offset = 24
    index = 0
    while offset < len(image):
        if len(image) - offset < 16:
            raise PcapError(f"packet {index} header is truncated")
        _seconds, _fraction, included, original = struct.unpack_from(
            byte_order + "IIII",
            image,
            offset,
        )
        offset += 16
        if included > snap_length:
            raise PcapError(
                f"packet {index} exceeds the snapshot length"
            )
        if included > original:
            raise PcapError(
                f"packet {index} captured length exceeds original length"
            )
        if included > len(image) - offset:
            raise PcapError(f"packet {index} payload is truncated")
        packets.append(image[offset : offset + included])
        offset += included
        index += 1
    return packets


def _ethernet_payload(frame: bytes, index: int) -> tuple[int, bytes]:
    if len(frame) < 14:
        raise PcapError(f"packet {index} has a truncated Ethernet header")
    ethertype = struct.unpack_from("!H", frame, 12)[0]
    offset = 14
    while ethertype in (0x8100, 0x88A8):
        if len(frame) < offset + 4:
            raise PcapError(f"packet {index} has a truncated VLAN header")
        ethertype = struct.unpack_from("!H", frame, offset + 2)[0]
        offset += 4
    return ethertype, frame[offset:]


def _checksum_is_valid(header: bytes) -> bool:
    if len(header) & 1:
        return False
    total = 0
    for offset in range(0, len(header), 2):
        total += (header[offset] << 8) | header[offset + 1]
        total = (total & 0xFFFF) + (total >> 16)
    return total == 0xFFFF


def _ipv4_packet(
    payload: bytes,
    index: int,
) -> tuple[int, bytes, bytes, bytes | None, bool] | None:
    if len(payload) < 20:
        raise PcapError(f"packet {index} has a truncated IPv4 header")
    version = payload[0] >> 4
    header_length = (payload[0] & 0x0F) * 4
    if version != 4:
        return None
    if header_length < 20 or header_length > len(payload):
        raise PcapError(f"packet {index} has an invalid IPv4 header length")
    total_length = struct.unpack_from("!H", payload, 2)[0]
    if total_length < header_length or total_length > len(payload):
        raise PcapError(f"packet {index} has an invalid IPv4 total length")
    fragment = struct.unpack_from("!H", payload, 6)[0]
    transport = (
        None
        if fragment & 0x3FFF
        else payload[header_length:total_length]
    )
    return (
        payload[9],
        payload[12:16],
        payload[16:20],
        transport,
        _checksum_is_valid(payload[:header_length]),
    )


def _dhcp_message(
    payload: bytes,
    index: int,
) -> tuple[int, int, bytes, bytes] | None:
    if len(payload) < 240 or payload[236:240] != b"\x63\x82\x53\x63":
        return None
    operation = payload[0]
    hardware_type = payload[1]
    hardware_length = payload[2]
    if hardware_type != 1 or hardware_length != 6:
        raise PcapError(
            f"packet {index} has an unsupported DHCP client address"
        )
    transaction_id = payload[4:8]
    client_address = payload[28 : 28 + hardware_length]
    offset = 240
    while offset < len(payload):
        code = payload[offset]
        offset += 1
        if code == 0:
            continue
        if code == 255:
            return None
        if offset >= len(payload):
            raise PcapError(f"packet {index} has a truncated DHCP option")
        length = payload[offset]
        offset += 1
        if length > len(payload) - offset:
            raise PcapError(f"packet {index} has a truncated DHCP option")
        value = payload[offset : offset + length]
        offset += length
        if code == 53 and length == 1:
            return (
                value[0],
                operation,
                transaction_id,
                client_address,
            )
    return None


def _tcp_sequence_end(segment: tuple) -> int:
    flags = segment[6]
    payload_length = segment[7]
    advance = payload_length
    advance += bool(flags & 0x02)
    advance += bool(flags & 0x01)
    return (segment[3] + advance) & 0xFFFFFFFF


def _tcp_handshakes(segments: list[tuple]) -> list[tuple]:
    handshakes = []
    seen_attempts = set()
    for syn_position, syn in enumerate(segments):
        flags = syn[6]
        if (
            not (flags & 0x02)
            or (flags & 0x10)
            or (flags & 0x05)
        ):
            continue
        attempt = (syn[1], syn[2], syn[3])
        if attempt in seen_attempts:
            continue
        seen_attempts.add(attempt)
        syn_ack = None
        syn_ack_position = 0
        for position in range(syn_position + 1, len(segments)):
            candidate = segments[position]
            if (
                candidate[1] == syn[2]
                and candidate[2] == syn[1]
                and (candidate[6] & 0x12) == 0x12
                and not (candidate[6] & 0x05)
                and candidate[4] == _tcp_sequence_end(syn)
            ):
                syn_ack = candidate
                syn_ack_position = position
                break
        if syn_ack is None:
            continue
        final_ack = None
        final_ack_position = 0
        for position in range(syn_ack_position + 1, len(segments)):
            candidate = segments[position]
            if (
                candidate[1] == syn[1]
                and candidate[2] == syn[2]
                and (candidate[6] & 0x10)
                and not (candidate[6] & 0x07)
                and candidate[3] == _tcp_sequence_end(syn)
                and candidate[4] == _tcp_sequence_end(syn_ack)
            ):
                final_ack = candidate
                final_ack_position = position
                break
        if final_ack is not None:
            expected_sequence = {
                syn[1]: _tcp_sequence_end(syn),
                syn[2]: _tcp_sequence_end(syn_ack),
            }
            interrupted = any(
                (candidate[6] & 0x04)
                and {candidate[1], candidate[2]} == {syn[1], syn[2]}
                and (
                    candidate[3] == expected_sequence[candidate[1]]
                    or (
                        position < syn_ack_position
                        and candidate[1] == syn[2]
                        and (candidate[6] & 0x10)
                        and candidate[4] == _tcp_sequence_end(syn)
                    )
                )
                for position, candidate in enumerate(
                    segments[syn_position + 1 : final_ack_position],
                    start=syn_position + 1,
                )
            )
            if interrupted:
                continue
            handshakes.append((syn, syn_ack, final_ack))
    return handshakes


def _tcp_forward_offset(sequence: int, initial: int) -> int | None:
    offset = (sequence - initial) & 0xFFFFFFFF
    if offset >= 0x80000000:
        return None
    return offset


def _tcp_payload_interval(
    sequence: int,
    length: int,
    initial: int,
) -> tuple[int, int] | None:
    start = _tcp_forward_offset(sequence, initial)
    if start is not None:
        return start, start + length
    overlap = (initial - sequence) & 0xFFFFFFFF
    if overlap >= 0x80000000 or overlap >= length:
        return None
    return 0, length - overlap


def _tcp_ack_matches_state(
    segment: tuple,
    peer_initial: int,
    peer_fin: tuple,
    peer_contiguous: int,
) -> bool:
    ack_offset = _tcp_forward_offset(segment[4], peer_initial)
    if ack_offset is not None and ack_offset <= peer_contiguous:
        return True
    fin_offset = _tcp_forward_offset(peer_fin[3], peer_initial)
    return (
        fin_offset is not None
        and segment[0] > peer_fin[0]
        and segment[4] == _tcp_sequence_end(peer_fin)
        and peer_contiguous == fin_offset
    )


def _tcp_extend_contiguous(
    intervals: list[tuple[int, int]],
    contiguous: int,
) -> int:
    changed = True
    while changed:
        changed = False
        for start, end in intervals:
            if start <= contiguous < end:
                contiguous = end
                changed = True
    return contiguous


def _tcp_causal_states(
    flow: list[tuple],
    initial_sequence: dict,
    fins_by_source: dict,
    completion: int,
) -> tuple[dict, dict] | None:
    targets = {}
    intervals = {}
    contiguous = {}
    for endpoint, initial in initial_sequence.items():
        target = _tcp_forward_offset(
            fins_by_source[endpoint][3],
            initial,
        )
        if target is None:
            return None
        targets[endpoint] = target
        intervals[endpoint] = []
        contiguous[endpoint] = 0

    before = {}
    for segment in flow:
        if segment[0] > completion:
            break
        before[segment[0]] = dict(contiguous)
        source = segment[1]
        peer = segment[2]
        flags = segment[6]
        selected_fin = fins_by_source[source]
        if segment[0] == selected_fin[0]:
            if not _tcp_ack_matches_state(
                segment,
                initial_sequence[peer],
                fins_by_source[peer],
                contiguous[peer],
            ):
                return None
            continue
        if (
            not (flags & 0x10)
            or (flags & 0x07)
            or segment[7] == 0
            or not _tcp_ack_matches_state(
                segment,
                initial_sequence[peer],
                fins_by_source[peer],
                contiguous[peer],
            )
        ):
            continue
        interval = _tcp_payload_interval(
            segment[3],
            segment[7],
            initial_sequence[source],
        )
        if interval is None:
            continue
        start, end = interval
        if end <= targets[source]:
            intervals[source].append((start, end))
            contiguous[source] = _tcp_extend_contiguous(
                intervals[source],
                contiguous[source],
            )
    return targets, before


def _tcp_segment_fits_before_fin(
    segment: tuple,
    initial: int,
    fin: tuple,
) -> bool:
    target = _tcp_forward_offset(fin[3], initial)
    interval = _tcp_payload_interval(
        segment[3],
        segment[7],
        initial,
    )
    if target is None or interval is None:
        return False
    start, end = interval
    return start <= target and end <= target


def _tcp_control_matches_stream(
    segment: tuple,
    initial: int,
    fin: tuple,
    contiguous: int,
) -> bool:
    target = _tcp_forward_offset(fin[3], initial)
    expected = (initial + contiguous) & 0xFFFFFFFF
    if (
        target is not None
        and segment[0] > fin[0]
        and contiguous == target
    ):
        expected = _tcp_sequence_end(fin)
    return segment[3] == expected


def _tcp_teardown_evidence(
    segments: list[tuple],
    handshake: tuple,
) -> tuple[int, ...] | None:
    syn, syn_ack, established = handshake
    endpoints = {syn[1], syn[2]}
    flow = [
        segment
        for segment in segments
        if {segment[1], segment[2]} == endpoints
        and segment[0] > established[0]
    ]
    initial_sequence = {
        established[1]: _tcp_sequence_end(established),
        established[2]: _tcp_sequence_end(syn_ack),
    }
    fins = [
        segment
        for segment in flow
        if (segment[6] & 0x11) == 0x11
        and not (segment[6] & 0x06)
    ]

    def acknowledgments(fin: tuple, sender_fin: tuple) -> list[tuple]:
        result = []
        fin_end = _tcp_sequence_end(fin)
        sender = sender_fin[1]
        sender_fin_end = _tcp_sequence_end(sender_fin)
        for candidate in flow:
            flags = candidate[6]
            if (
                candidate[0] <= fin[0]
                or candidate[1] != sender
                or candidate[2] != fin[1]
                or not (flags & 0x10)
                or (flags & 0x06)
                or candidate[4] != fin_end
            ):
                continue
            if candidate[0] == sender_fin[0]:
                result.append(candidate)
                continue
            if candidate[0] > sender_fin[0]:
                if (
                    (flags & 0x3F) == 0x10
                    and candidate[3] == sender_fin_end
                    and candidate[7] == 0
                ):
                    result.append(candidate)
                continue
            if not (flags & 0x01):
                result.append(candidate)
        return result

    for first_position, first_fin in enumerate(fins):
        for second_fin in fins[first_position + 1 :]:
            if (
                first_fin[1] != second_fin[2]
                or first_fin[2] != second_fin[1]
            ):
                continue
            first_acks = acknowledgments(first_fin, second_fin)
            second_acks = acknowledgments(second_fin, first_fin)
            for first_ack in first_acks:
                for second_ack in second_acks:
                    completion = max(first_ack[0], second_ack[0])
                    fins_by_source = {
                        first_fin[1]: first_fin,
                        second_fin[1]: second_fin,
                    }
                    causal = _tcp_causal_states(
                        flow,
                        initial_sequence,
                        fins_by_source,
                        completion,
                    )
                    if causal is None:
                        continue
                    _targets, before = causal
                    if (
                        first_ack[0] not in before
                        or second_ack[0] not in before
                    ):
                        continue
                    if not _tcp_ack_matches_state(
                        first_ack,
                        initial_sequence[first_fin[1]],
                        first_fin,
                        before[first_ack[0]][first_fin[1]],
                    ):
                        continue
                    if not _tcp_ack_matches_state(
                        second_ack,
                        initial_sequence[second_fin[1]],
                        second_fin,
                        before[second_ack[0]][second_fin[1]],
                    ):
                        continue
                    if (
                        first_ack[0] != second_fin[0]
                        and first_ack[0] < second_fin[0]
                        and not _tcp_segment_fits_before_fin(
                            first_ack,
                            initial_sequence[second_fin[1]],
                            second_fin,
                        )
                    ):
                        continue
                    if (
                        second_ack[0] != first_fin[0]
                        and second_ack[0] < first_fin[0]
                        and not _tcp_segment_fits_before_fin(
                            second_ack,
                            initial_sequence[first_fin[1]],
                            first_fin,
                        )
                    ):
                        continue
                    interrupted = any(
                        candidate[0] <= completion
                        and (candidate[6] & 0x06)
                        and _tcp_control_matches_stream(
                            candidate,
                            initial_sequence[candidate[1]],
                            (
                                first_fin
                                if candidate[1] == first_fin[1]
                                else second_fin
                            ),
                            before[candidate[0]][candidate[1]],
                        )
                        for candidate in flow
                        if candidate[0] in before
                    )
                    if interrupted:
                        continue
                    return tuple(
                        sorted(
                            {
                                first_fin[0],
                                second_fin[0],
                                first_ack[0],
                                second_ack[0],
                            }
                        )
                    )
    return None


def check_pcap(path: Path) -> tuple[bool, list[str]]:
    packets = _pcap_packets(path)
    arp_requests = []
    arp_replies = []
    dhcp_records = []
    icmp_requests = []
    icmp_replies = []
    tcp_segments = []
    bad_ip = 0

    for index, frame in enumerate(packets):
        ethertype, payload = _ethernet_payload(frame, index)
        if ethertype == 0x0806:
            if len(payload) < 28:
                raise PcapError(
                    f"packet {index} has a truncated ARP payload"
                )
            hardware_type, protocol_type = struct.unpack_from("!HH", payload, 0)
            hardware_length = payload[4]
            protocol_length = payload[5]
            if (
                hardware_type != 1
                or protocol_type != 0x0800
                or hardware_length != 6
                or protocol_length != 4
            ):
                raise PcapError(
                    f"packet {index} has an unsupported ARP format"
                )
            operation = struct.unpack_from("!H", payload, 6)[0]
            record = (
                index,
                payload[8:14],
                payload[14:18],
                payload[18:24],
                payload[24:28],
            )
            if operation == 1:
                arp_requests.append(record)
            elif operation == 2:
                arp_replies.append(record)
            continue
        if ethertype != 0x0800:
            continue

        parsed = _ipv4_packet(payload, index)
        if parsed is None:
            continue
        protocol, source, destination, transport, checksum_ok = parsed
        bad_ip += not checksum_ok
        if transport is None:
            continue

        if protocol == 1:
            if len(transport) < 8:
                raise PcapError(
                    f"packet {index} has a truncated ICMP echo header"
                )
            message_type, code, _checksum, identifier, sequence = (
                struct.unpack_from("!BBHHH", transport, 0)
            )
            record = (
                index,
                source,
                destination,
                identifier,
                sequence,
            )
            if message_type == 8 and code == 0:
                icmp_requests.append(record)
            elif message_type == 0 and code == 0:
                icmp_replies.append(record)
            continue

        if protocol == 17:
            if len(transport) < 8:
                raise PcapError(
                    f"packet {index} has a truncated UDP header"
                )
            source_port, destination_port, udp_length = struct.unpack_from(
                "!HHH",
                transport,
                0,
            )
            if udp_length < 8 or udp_length > len(transport):
                raise PcapError(
                    f"packet {index} has an invalid UDP length"
                )
            if {source_port, destination_port} == {67, 68}:
                message = _dhcp_message(
                    transport[8:udp_length],
                    index,
                )
                if message is not None:
                    dhcp_records.append(
                        (
                            index,
                            source_port,
                            destination_port,
                            *message,
                        )
                    )
            continue

        if protocol != 6:
            continue
        if len(transport) < 20:
            raise PcapError(f"packet {index} has a truncated TCP header")
        data_offset = (transport[12] >> 4) * 4
        if data_offset < 20 or data_offset > len(transport):
            raise PcapError(
                f"packet {index} has an invalid TCP header length"
            )
        source_port, destination_port, sequence, acknowledgment = (
            struct.unpack_from("!HHII", transport, 0)
        )
        flags = transport[13]
        tcp_segments.append(
            (
                index,
                (source, source_port),
                (destination, destination_port),
                sequence,
                acknowledgment,
                data_offset,
                flags,
                len(transport) - data_offset,
            )
        )

    arp_pairs = 0
    for request in arp_requests:
        if any(
            reply[0] > request[0]
            and reply[2] == request[4]
            and reply[4] == request[2]
            and reply[3] == request[1]
            for reply in arp_replies
        ):
            arp_pairs += 1

    dhcp_state = {}
    dhcp_exchanges = 0
    for (
        _index,
        source_port,
        destination_port,
        message_type,
        operation,
        transaction_id,
        client_address,
    ) in dhcp_records:
        key = (transaction_id, client_address)
        state = dhcp_state.get(key, 0)
        client_message = (
            operation == 1
            and source_port == 68
            and destination_port == 67
        )
        server_message = (
            operation == 2
            and source_port == 67
            and destination_port == 68
        )
        if message_type == 1 and client_message:
            dhcp_state[key] = 1
        elif message_type == 2 and server_message and state == 1:
            dhcp_state[key] = 2
        elif message_type == 3 and client_message and state == 2:
            dhcp_state[key] = 3
        elif message_type == 5 and server_message and state == 3:
            dhcp_state[key] = 4
            dhcp_exchanges += 1

    icmp_pairs = 0
    for request in icmp_requests:
        if any(
            reply[0] > request[0]
            and reply[1] == request[2]
            and reply[2] == request[1]
            and reply[3:] == request[3:]
            for reply in icmp_replies
        ):
            icmp_pairs += 1

    tcp_handshakes = _tcp_handshakes(tcp_segments)
    def is_public_client(handshake: tuple) -> bool:
        address = ipaddress.ip_address(handshake[0][2][0])
        return address.is_global and not address.is_multicast

    public_client_handshakes = [
        handshake
        for handshake in tcp_handshakes
        if is_public_client(handshake)
    ]
    guest_addresses = {
        handshake[0][1][0]
        for handshake in public_client_handshakes
    }
    def is_guest_server(handshake: tuple) -> bool:
        return (
            handshake[0][2][0] in guest_addresses
            and handshake[0][2][1] == 80
            and handshake[0][1][0] not in guest_addresses
        )

    guest_server_handshakes = sum(
        is_guest_server(handshake)
        for handshake in tcp_handshakes
    )
    teardown_records = []
    used_teardown_evidence = set()
    for handshake in tcp_handshakes:
        evidence = _tcp_teardown_evidence(tcp_segments, handshake)
        if evidence is None or evidence in used_teardown_evidence:
            continue
        used_teardown_evidence.add(evidence)
        teardown_records.append((handshake, evidence))
    tcp_teardowns = len(teardown_records)
    public_client_teardowns = sum(
        is_public_client(handshake)
        for handshake, _evidence in teardown_records
    )
    guest_server_teardowns = sum(
        is_guest_server(handshake)
        for handshake, _evidence in teardown_records
    )

    notes: list[str] = []
    ok = True

    def add(condition: bool, message: str) -> None:
        nonlocal ok
        if not condition:
            ok = False
        notes.append(("OK  " if condition else "FAIL") + " " + message)

    add(arp_pairs >= 1, f"ARP request/reply pairs: {arp_pairs}")
    add(dhcp_exchanges >= 1, f"Complete DHCP exchanges: {dhcp_exchanges}")
    add(icmp_pairs >= 1, f"ICMP echo pairs: {icmp_pairs}")
    add(len(tcp_handshakes) >= 2, f"Complete TCP handshakes: {len(tcp_handshakes)}")
    add(
        len(public_client_handshakes) >= 1,
        f"Public client handshakes: {len(public_client_handshakes)}",
    )
    add(
        guest_server_handshakes >= 1,
        f"Guest server handshakes: {guest_server_handshakes}",
    )
    add(tcp_teardowns >= 2, f"Complete TCP teardowns: {tcp_teardowns}")
    add(
        public_client_teardowns >= 1,
        f"Public client teardowns: {public_client_teardowns}",
    )
    add(
        guest_server_teardowns >= 1,
        f"Guest server teardowns: {guest_server_teardowns}",
    )
    add(bad_ip == 0, f"Bad IP checksums: {bad_ip}")
    return ok, notes


def main(argv: list[str]) -> int:
    if not argv:
        print("usage: net_pcap.py <pcap> [pcap ...]", file=sys.stderr)
        return 2
    overall_ok = True
    for argument in argv:
        path = Path(argument)
        print(f"\n=== {path} ===")
        try:
            ok, notes = check_pcap(path)
        except (OSError, PcapError) as error:
            print(f"  FAIL exception: {error}")
            overall_ok = False
            continue
        for note in notes:
            print(f"  {note}")
        if not ok:
            overall_ok = False
    return 0 if overall_ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

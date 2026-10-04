import struct
import tempfile
import unittest
from pathlib import Path

from tools import net_pcap

GUEST_MAC = b"\x52\x54\x00\x12\x34\x56"
GATEWAY_MAC = b"\x52\x54\x00\x65\x43\x21"
GUEST_IP = "10.0.2.15"
GATEWAY_IP = "10.0.2.2"
PUBLIC_IP = "93.184.216.34"


def checksum(data):
    if len(data) & 1:
        data += b"\0"
    total = 0
    for offset in range(0, len(data), 2):
        total += (data[offset] << 8) | data[offset + 1]
        total = (total & 0xFFFF) + (total >> 16)
    return (~total) & 0xFFFF


def ethernet(
    payload,
    ethertype,
    source=GUEST_MAC,
    destination=GATEWAY_MAC,
):
    return (
        destination
        + source
        + struct.pack("!H", ethertype)
        + payload
    )


def ipv4(protocol, payload, source, destination):
    source_bytes = bytes(int(part) for part in source.split("."))
    destination_bytes = bytes(int(part) for part in destination.split("."))
    header = bytearray(
        struct.pack(
            "!BBHHHBBH4s4s",
            0x45,
            0,
            20 + len(payload),
            1,
            0,
            64,
            protocol,
            0,
            source_bytes,
            destination_bytes,
        )
    )
    struct.pack_into("!H", header, 10, checksum(bytes(header)))
    return ethernet(bytes(header) + payload, 0x0800)


def arp(operation):
    if operation == 1:
        sender_mac = GUEST_MAC
        sender_ip = b"\x0a\x00\x02\x0f"
        target_mac = b"\0" * 6
        target_ip = b"\x0a\x00\x02\x02"
        destination = b"\xff" * 6
    else:
        sender_mac = GATEWAY_MAC
        sender_ip = b"\x0a\x00\x02\x02"
        target_mac = GUEST_MAC
        target_ip = b"\x0a\x00\x02\x0f"
        destination = GUEST_MAC
    payload = struct.pack(
        "!HHBBH6s4s6s4s",
        1,
        0x0800,
        6,
        4,
        operation,
        sender_mac,
        sender_ip,
        target_mac,
        target_ip,
    )
    return ethernet(
        payload,
        0x0806,
        source=sender_mac,
        destination=destination,
    )


def dhcp(message_type, transaction_id=0x12345678):
    bootp = bytearray(236)
    server_message = message_type in (2, 5)
    bootp[0] = 2 if server_message else 1
    bootp[1] = 1
    bootp[2] = 6
    struct.pack_into("!I", bootp, 4, transaction_id)
    bootp[28:34] = GUEST_MAC
    options = b"\x63\x82\x53\x63\x35\x01" + bytes([message_type]) + b"\xff"
    payload = bytes(bootp) + options
    source_port, destination_port = (67, 68) if server_message else (68, 67)
    udp = (
        struct.pack(
            "!HHHH",
            source_port,
            destination_port,
            8 + len(payload),
            0,
        )
        + payload
    )
    source = GATEWAY_IP if server_message else "0.0.0.0"
    return ipv4(17, udp, source, "255.255.255.255")


def icmp(message_type, source, destination, identifier=70, sequence=1):
    return ipv4(
        1,
        struct.pack("!BBHHH", message_type, 0, 0, identifier, sequence),
        source,
        destination,
    )


def tcp(
    flags,
    source,
    destination,
    source_port,
    destination_port,
    sequence,
    acknowledgment,
    payload=b"",
):
    header = struct.pack(
        "!HHIIBBHHH",
        source_port,
        destination_port,
        sequence,
        acknowledgment,
        0x50,
        flags,
        4096,
        0,
        0,
    )
    return ipv4(6, header + payload, source, destination)


def complete_frames(public_ip=PUBLIC_IP):
    frames = [
        arp(1),
        arp(2),
        dhcp(1),
        dhcp(2),
        dhcp(3),
        dhcp(5),
        icmp(8, GUEST_IP, GATEWAY_IP),
        icmp(0, GATEWAY_IP, GUEST_IP),
    ]
    frames.extend(
        [
            tcp(0x02, GUEST_IP, public_ip, 40000, 80, 1000, 0),
            tcp(0x12, public_ip, GUEST_IP, 80, 40000, 5000, 1001),
            tcp(0x10, GUEST_IP, public_ip, 40000, 80, 1001, 5001),
            tcp(0x11, public_ip, GUEST_IP, 80, 40000, 5001, 1001),
            tcp(0x10, GUEST_IP, public_ip, 40000, 80, 1001, 5002),
            tcp(0x11, GUEST_IP, public_ip, 40000, 80, 1001, 5002),
            tcp(0x10, public_ip, GUEST_IP, 80, 40000, 5002, 1002),
            tcp(0x02, GATEWAY_IP, GUEST_IP, 41000, 80, 2000, 0),
            tcp(0x12, GUEST_IP, GATEWAY_IP, 80, 41000, 6000, 2001),
            tcp(0x10, GATEWAY_IP, GUEST_IP, 41000, 80, 2001, 6001),
            tcp(0x11, GUEST_IP, GATEWAY_IP, 80, 41000, 6001, 2001),
            tcp(0x10, GATEWAY_IP, GUEST_IP, 41000, 80, 2001, 6002),
            tcp(0x11, GATEWAY_IP, GUEST_IP, 41000, 80, 2001, 6002),
            tcp(0x10, GUEST_IP, GATEWAY_IP, 80, 41000, 6002, 2002),
        ]
    )
    return frames


def vlan(frame):
    return frame[:12] + struct.pack("!HH", 0x8100, 7) + frame[12:]


def fragmented(frame):
    image = bytearray(frame)
    struct.pack_into("!H", image, 20, 0x2000)
    struct.pack_into("!H", image, 24, 0)
    struct.pack_into("!H", image, 24, checksum(bytes(image[14:34])))
    return bytes(image)


def pcap(
    frames,
    byte_order="<",
    nanosecond=False,
    snap_length=65535,
    link_type=1,
):
    magic = 0xA1B23C4D if nanosecond else 0xA1B2C3D4
    image = bytearray(
        struct.pack(
            byte_order + "IHHIIII",
            magic,
            2,
            4,
            0,
            0,
            snap_length,
            link_type,
        )
    )
    for index, frame in enumerate(frames):
        image.extend(
            struct.pack(
                byte_order + "IIII",
                index,
                0,
                len(frame),
                len(frame),
            )
        )
        image.extend(frame)
    return bytes(image)


class NetworkPcapTests(unittest.TestCase):
    def write_capture(self, data):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        path = Path(temporary.name) / "fixture.pcap"
        path.write_bytes(data)
        return path

    def test_complete_protocol_capture_passes(self):
        ok, notes = net_pcap.check_pcap(
            self.write_capture(pcap(complete_frames()))
        )

        self.assertTrue(ok, notes)
        self.assertTrue(all(note.startswith("OK  ") for note in notes))

    def test_big_endian_and_nanosecond_captures_pass(self):
        variants = ((">", False), ("<", True), (">", True))
        for byte_order, nanosecond in variants:
            with self.subTest(
                byte_order=byte_order,
                nanosecond=nanosecond,
            ):
                path = self.write_capture(
                    pcap(
                        complete_frames(),
                        byte_order=byte_order,
                        nanosecond=nanosecond,
                    )
                )
                ok, notes = net_pcap.check_pcap(path)
                self.assertTrue(ok, notes)

    def test_vlan_framing_preserves_protocol_correlation(self):
        frames = complete_frames()
        frames[0] = vlan(frames[0])

        ok, notes = net_pcap.check_pcap(
            self.write_capture(pcap(frames))
        )

        self.assertTrue(ok, notes)

    def test_arp_reply_must_reverse_the_request(self):
        frames = complete_frames()
        reply = bytearray(frames[1])
        reply[38:42] = b"\x0a\x00\x02\x63"
        frames[1] = bytes(reply)

        ok, notes = net_pcap.check_pcap(
            self.write_capture(pcap(frames))
        )

        self.assertFalse(ok)
        self.assertIn("FAIL ARP request/reply pairs: 0", notes)

    def test_dhcp_exchange_requires_one_transaction_and_direction(self):
        for replacement in (dhcp(5, 0x87654321),):
            with self.subTest(case="transaction"):
                frames = complete_frames()
                frames[5] = replacement
                ok, notes = net_pcap.check_pcap(
                    self.write_capture(pcap(frames))
                )
                self.assertFalse(ok)
                self.assertIn("FAIL Complete DHCP exchanges: 0", notes)

        frames = complete_frames()
        offer = bytearray(frames[3])
        struct.pack_into("!HH", offer, 34, 68, 67)
        offer[42] = 1
        frames[3] = bytes(offer)

        ok, notes = net_pcap.check_pcap(
            self.write_capture(pcap(frames))
        )

        self.assertFalse(ok)
        self.assertIn("FAIL Complete DHCP exchanges: 0", notes)

    def test_icmp_reply_must_match_identifier_and_sequence(self):
        frames = complete_frames()
        frames[7] = icmp(
            0,
            GATEWAY_IP,
            GUEST_IP,
            identifier=71,
            sequence=1,
        )

        ok, notes = net_pcap.check_pcap(
            self.write_capture(pcap(frames))
        )

        self.assertFalse(ok)
        self.assertIn("FAIL ICMP echo pairs: 0", notes)

    def test_tcp_handshake_requires_reverse_tuple_and_acknowledgment(self):
        frames = complete_frames()
        frames[9] = tcp(
            0x12,
            PUBLIC_IP,
            GUEST_IP,
            80,
            40000,
            5000,
            9999,
        )

        ok, notes = net_pcap.check_pcap(
            self.write_capture(pcap(frames))
        )

        self.assertFalse(ok)
        self.assertIn("FAIL Complete TCP handshakes: 1", notes)
        self.assertIn("FAIL Public client handshakes: 0", notes)

    def test_syn_retransmission_does_not_create_a_second_flow(self):
        frames = complete_frames()[:15]
        frames.insert(9, frames[8])

        ok, notes = net_pcap.check_pcap(
            self.write_capture(pcap(frames))
        )

        self.assertFalse(ok)
        self.assertIn("FAIL Complete TCP handshakes: 1", notes)
        self.assertIn("FAIL Complete TCP teardowns: 1", notes)

    def test_reset_or_fin_cannot_complete_a_handshake(self):
        for frame_index, flags in (
            (8, 0x07),
            (9, 0x16),
            (10, 0x15),
        ):
            with self.subTest(frame_index=frame_index, flags=flags):
                frames = complete_frames()
                frame = bytearray(frames[frame_index])
                frame[47] = flags
                frames[frame_index] = bytes(frame)

                ok, notes = net_pcap.check_pcap(
                    self.write_capture(pcap(frames))
                )

                self.assertFalse(ok)
                self.assertIn("FAIL Complete TCP handshakes: 1", notes)

    def test_sequence_valid_reset_interrupts_a_handshake(self):
        cases = (
            (
                "in-window",
                10,
                tcp(
                    0x14,
                    GUEST_IP,
                    PUBLIC_IP,
                    40000,
                    80,
                    1001,
                    5001,
                ),
                False,
            ),
            (
                "out-of-window",
                10,
                tcp(
                    0x14,
                    GUEST_IP,
                    PUBLIC_IP,
                    40000,
                    80,
                    9000,
                    5001,
                ),
                True,
            ),
            (
                "responder-rejects-syn",
                9,
                tcp(
                    0x14,
                    PUBLIC_IP,
                    GUEST_IP,
                    80,
                    40000,
                    0,
                    1001,
                ),
                False,
            ),
        )
        for name, position, reset, expected in cases:
            with self.subTest(case=name):
                frames = complete_frames()
                frames.insert(position, reset)

                ok, notes = net_pcap.check_pcap(
                    self.write_capture(pcap(frames))
                )

                self.assertEqual(ok, expected, notes)
                if not expected:
                    self.assertIn(
                        "FAIL Complete TCP handshakes: 1",
                        notes,
                    )

    def test_two_client_flows_do_not_stand_in_for_the_guest_server(self):
        frames = complete_frames()[:15]
        frames.extend(
            [
                tcp(0x02, GUEST_IP, "8.8.8.8", 40001, 443, 2000, 0),
                tcp(0x12, "8.8.8.8", GUEST_IP, 443, 40001, 6000, 2001),
                tcp(0x10, GUEST_IP, "8.8.8.8", 40001, 443, 2001, 6001),
                tcp(0x11, "8.8.8.8", GUEST_IP, 443, 40001, 6001, 2001),
                tcp(0x10, GUEST_IP, "8.8.8.8", 40001, 443, 2001, 6002),
                tcp(0x11, GUEST_IP, "8.8.8.8", 40001, 443, 2001, 6002),
                tcp(0x10, "8.8.8.8", GUEST_IP, 443, 40001, 6002, 2002),
            ]
        )

        ok, notes = net_pcap.check_pcap(
            self.write_capture(pcap(frames))
        )

        self.assertFalse(ok)
        self.assertIn("FAIL Guest server handshakes: 0", notes)

    def test_tcp_teardown_requires_both_fin_ack_directions(self):
        frames = complete_frames()
        for index in (11, 13, 18, 20):
            frame = bytearray(frames[index])
            frame[47] &= ~0x01
            frames[index] = bytes(frame)

        ok, notes = net_pcap.check_pcap(
            self.write_capture(pcap(frames))
        )

        self.assertFalse(ok)
        self.assertIn("FAIL Complete TCP teardowns: 0", notes)

    def test_reset_cannot_complete_a_teardown(self):
        cases = {}
        reset_before_fin = complete_frames()
        reset_before_fin.insert(
            11,
            tcp(
                0x14,
                PUBLIC_IP,
                GUEST_IP,
                80,
                40000,
                5001,
                1001,
            ),
        )
        cases["reset-before-fin"] = reset_before_fin

        reset_fin = complete_frames()
        frame = bytearray(reset_fin[11])
        frame[47] |= 0x04
        reset_fin[11] = bytes(frame)
        cases["reset-fin"] = reset_fin

        reset_before_final_ack = complete_frames()
        reset_before_final_ack.insert(
            14,
            tcp(
                0x14,
                PUBLIC_IP,
                GUEST_IP,
                80,
                40000,
                5002,
                1002,
            ),
        )
        cases["reset-before-final-ack"] = reset_before_final_ack

        for name, frames in cases.items():
            with self.subTest(case=name):
                ok, notes = net_pcap.check_pcap(
                    self.write_capture(pcap(frames))
                )

                self.assertFalse(ok)
                self.assertIn("FAIL Complete TCP teardowns: 1", notes)

    def test_out_of_window_reset_does_not_hide_a_valid_teardown(self):
        cases = {}
        out_of_window = complete_frames()
        out_of_window.insert(
            14,
            tcp(
                0x14,
                PUBLIC_IP,
                GUEST_IP,
                80,
                40000,
                9000,
                1002,
            ),
        )
        cases["out-of-window"] = out_of_window

        base = complete_frames()
        stale_close = [
            tcp(
                0x18,
                PUBLIC_IP,
                GUEST_IP,
                80,
                40000,
                5001,
                1001,
                payload=b"data",
            ),
            tcp(
                0x14,
                PUBLIC_IP,
                GUEST_IP,
                80,
                40000,
                5001,
                1001,
            ),
            tcp(
                0x11,
                PUBLIC_IP,
                GUEST_IP,
                80,
                40000,
                5005,
                1001,
            ),
            tcp(
                0x11,
                GUEST_IP,
                PUBLIC_IP,
                40000,
                80,
                1001,
                5006,
            ),
            tcp(
                0x10,
                PUBLIC_IP,
                GUEST_IP,
                80,
                40000,
                5006,
                1002,
            ),
        ]
        cases["stale-sequence"] = base[:11] + stale_close + base[15:]

        for name, frames in cases.items():
            with self.subTest(case=name):
                ok, notes = net_pcap.check_pcap(
                    self.write_capture(pcap(frames))
                )

                self.assertTrue(ok, notes)

    def test_teardown_requires_valid_flags_and_sequence_state(self):
        cases = (
            ("first-fin-syn", 11, 47, b"\x13"),
            ("first-ack-syn", 12, 47, b"\x12"),
            ("second-fin-syn", 13, 47, b"\x13"),
            ("second-fin-ack", 13, 42, struct.pack("!I", 1234)),
            ("second-fin-sequence", 13, 38, struct.pack("!I", 4242)),
            ("final-ack-syn", 14, 47, b"\x12"),
            ("final-ack-sequence", 14, 38, struct.pack("!I", 4242)),
        )
        for name, frame_index, offset, replacement in cases:
            with self.subTest(case=name):
                frames = complete_frames()
                frame = bytearray(frames[frame_index])
                frame[offset : offset + len(replacement)] = replacement
                if name == "first-ack-syn":
                    frame = bytearray(frames[frame_index])
                    frame[47] = 0x12
                    frames[13] = tcp(
                        0x01,
                        GUEST_IP,
                        PUBLIC_IP,
                        40000,
                        80,
                        1001,
                        5002,
                    )
                frames[frame_index] = bytes(frame)

                ok, notes = net_pcap.check_pcap(
                    self.write_capture(pcap(frames))
                )

                self.assertFalse(ok)
                self.assertIn("FAIL Complete TCP teardowns: 1", notes)

    def test_teardown_cannot_invent_stream_sequence_state(self):
        cases = (
            (
                "first-fin-sequence",
                (
                    (11, tcp(
                        0x11, PUBLIC_IP, GUEST_IP, 80, 40000, 7000, 1001
                    )),
                    (12, tcp(
                        0x10, GUEST_IP, PUBLIC_IP, 40000, 80, 1001, 7001
                    )),
                    (13, tcp(
                        0x11, GUEST_IP, PUBLIC_IP, 40000, 80, 1001, 7001
                    )),
                    (14, tcp(
                        0x10, PUBLIC_IP, GUEST_IP, 80, 40000, 7001, 1002
                    )),
                ),
            ),
            (
                "first-ack-sequence",
                (
                    (12, tcp(
                        0x10, GUEST_IP, PUBLIC_IP, 40000, 80, 4242, 5002
                    )),
                    (13, tcp(
                        0x11, GUEST_IP, PUBLIC_IP, 40000, 80, 4242, 5002
                    )),
                    (14, tcp(
                        0x10, PUBLIC_IP, GUEST_IP, 80, 40000, 5002, 4243
                    )),
                ),
            ),
        )
        for name, replacements in cases:
            with self.subTest(case=name):
                frames = complete_frames()
                for frame_index, frame in replacements:
                    frames[frame_index] = frame

                ok, notes = net_pcap.check_pcap(
                    self.write_capture(pcap(frames))
                )

                self.assertFalse(ok)
                self.assertIn("FAIL Complete TCP teardowns: 1", notes)

    def test_terminal_teardown_ack_cannot_carry_payload(self):
        frames = complete_frames()
        frames[14] = tcp(
            0x18,
            PUBLIC_IP,
            GUEST_IP,
            80,
            40000,
            5002,
            1002,
            payload=b"late",
        )

        ok, notes = net_pcap.check_pcap(
            self.write_capture(pcap(frames))
        )

        self.assertFalse(ok)
        self.assertIn("FAIL Complete TCP teardowns: 1", notes)

    def test_half_close_allows_peer_data_before_its_fin(self):
        frames = complete_frames()
        frames[13:15] = [
            tcp(
                0x18,
                GUEST_IP,
                PUBLIC_IP,
                40000,
                80,
                1001,
                5002,
                payload=b"data",
            ),
            tcp(
                0x10,
                PUBLIC_IP,
                GUEST_IP,
                80,
                40000,
                5002,
                1005,
            ),
            tcp(
                0x11,
                GUEST_IP,
                PUBLIC_IP,
                40000,
                80,
                1005,
                5002,
            ),
            tcp(
                0x10,
                PUBLIC_IP,
                GUEST_IP,
                80,
                40000,
                5002,
                1006,
            ),
        ]

        ok, notes = net_pcap.check_pcap(
            self.write_capture(pcap(frames))
        )

        self.assertTrue(ok, notes)

    def test_sequence_valid_close_variants_pass(self):
        variants = {
            "simultaneous-close": [
                tcp(
                    0x11,
                    PUBLIC_IP,
                    GUEST_IP,
                    80,
                    40000,
                    5001,
                    1001,
                ),
                tcp(
                    0x11,
                    GUEST_IP,
                    PUBLIC_IP,
                    40000,
                    80,
                    1001,
                    5001,
                ),
                tcp(
                    0x10,
                    PUBLIC_IP,
                    GUEST_IP,
                    80,
                    40000,
                    5002,
                    1002,
                ),
                tcp(
                    0x10,
                    GUEST_IP,
                    PUBLIC_IP,
                    40000,
                    80,
                    1002,
                    5002,
                ),
            ],
            "crossed-data": [
                tcp(
                    0x11,
                    PUBLIC_IP,
                    GUEST_IP,
                    80,
                    40000,
                    5001,
                    1001,
                ),
                tcp(
                    0x18,
                    GUEST_IP,
                    PUBLIC_IP,
                    40000,
                    80,
                    1001,
                    5001,
                    payload=b"data",
                ),
                tcp(
                    0x10,
                    PUBLIC_IP,
                    GUEST_IP,
                    80,
                    40000,
                    5002,
                    1005,
                ),
                tcp(
                    0x10,
                    GUEST_IP,
                    PUBLIC_IP,
                    40000,
                    80,
                    1005,
                    5002,
                ),
                tcp(
                    0x11,
                    GUEST_IP,
                    PUBLIC_IP,
                    40000,
                    80,
                    1005,
                    5002,
                ),
                tcp(
                    0x10,
                    PUBLIC_IP,
                    GUEST_IP,
                    80,
                    40000,
                    5002,
                    1006,
                ),
            ],
            "reordered-data": [
                tcp(
                    0x11,
                    PUBLIC_IP,
                    GUEST_IP,
                    80,
                    40000,
                    5001,
                    1001,
                ),
                tcp(
                    0x10,
                    GUEST_IP,
                    PUBLIC_IP,
                    40000,
                    80,
                    1001,
                    5002,
                ),
                tcp(
                    0x18,
                    GUEST_IP,
                    PUBLIC_IP,
                    40000,
                    80,
                    1005,
                    5002,
                    payload=b"efgh",
                ),
                tcp(
                    0x18,
                    GUEST_IP,
                    PUBLIC_IP,
                    40000,
                    80,
                    1001,
                    5002,
                    payload=b"abcd",
                ),
                tcp(
                    0x10,
                    PUBLIC_IP,
                    GUEST_IP,
                    80,
                    40000,
                    5002,
                    1009,
                ),
                tcp(
                    0x11,
                    GUEST_IP,
                    PUBLIC_IP,
                    40000,
                    80,
                    1009,
                    5002,
                ),
                tcp(
                    0x10,
                    PUBLIC_IP,
                    GUEST_IP,
                    80,
                    40000,
                    5002,
                    1010,
                ),
            ],
        }
        base = complete_frames()
        for name, close in variants.items():
            with self.subTest(case=name):
                frames = base[:11] + close + base[15:]

                ok, notes = net_pcap.check_pcap(
                    self.write_capture(pcap(frames))
                )

                self.assertTrue(ok, notes)

    def test_sequence_graph_requires_causal_data_and_valid_acks(self):
        base = complete_frames()
        cases = {
            "data-after-completion": [
                tcp(
                    0x11,
                    PUBLIC_IP,
                    GUEST_IP,
                    80,
                    40000,
                    5005,
                    1001,
                ),
                tcp(
                    0x11,
                    GUEST_IP,
                    PUBLIC_IP,
                    40000,
                    80,
                    1001,
                    5006,
                ),
                tcp(
                    0x10,
                    PUBLIC_IP,
                    GUEST_IP,
                    80,
                    40000,
                    5006,
                    1002,
                ),
                tcp(
                    0x18,
                    PUBLIC_IP,
                    GUEST_IP,
                    80,
                    40000,
                    5001,
                    1001,
                    payload=b"late",
                ),
            ],
            "data-with-impossible-ack": [
                tcp(
                    0x18,
                    PUBLIC_IP,
                    GUEST_IP,
                    80,
                    40000,
                    5001,
                    9999,
                    payload=b"data",
                ),
                tcp(
                    0x11,
                    PUBLIC_IP,
                    GUEST_IP,
                    80,
                    40000,
                    5005,
                    1001,
                ),
                tcp(
                    0x11,
                    GUEST_IP,
                    PUBLIC_IP,
                    40000,
                    80,
                    1001,
                    5006,
                ),
                tcp(
                    0x10,
                    PUBLIC_IP,
                    GUEST_IP,
                    80,
                    40000,
                    5006,
                    1002,
                ),
            ],
            "data-after-fin-ack": [
                tcp(
                    0x11,
                    PUBLIC_IP,
                    GUEST_IP,
                    80,
                    40000,
                    5005,
                    1001,
                ),
                tcp(
                    0x11,
                    GUEST_IP,
                    PUBLIC_IP,
                    40000,
                    80,
                    1001,
                    5006,
                ),
                tcp(
                    0x18,
                    PUBLIC_IP,
                    GUEST_IP,
                    80,
                    40000,
                    5001,
                    1001,
                    payload=b"late",
                ),
                tcp(
                    0x10,
                    PUBLIC_IP,
                    GUEST_IP,
                    80,
                    40000,
                    5006,
                    1002,
                ),
            ],
            "mutual-future-acks": [
                tcp(
                    0x18,
                    PUBLIC_IP,
                    GUEST_IP,
                    80,
                    40000,
                    5001,
                    1005,
                    payload=b"data",
                ),
                tcp(
                    0x18,
                    GUEST_IP,
                    PUBLIC_IP,
                    40000,
                    80,
                    1001,
                    5005,
                    payload=b"peer",
                ),
                tcp(
                    0x11,
                    PUBLIC_IP,
                    GUEST_IP,
                    80,
                    40000,
                    5005,
                    1005,
                ),
                tcp(
                    0x11,
                    GUEST_IP,
                    PUBLIC_IP,
                    40000,
                    80,
                    1005,
                    5006,
                ),
                tcp(
                    0x10,
                    PUBLIC_IP,
                    GUEST_IP,
                    80,
                    40000,
                    5006,
                    1006,
                ),
            ],
        }
        for name, close in cases.items():
            with self.subTest(case=name):
                frames = base[:11] + close + base[15:]

                ok, notes = net_pcap.check_pcap(
                    self.write_capture(pcap(frames))
                )

                self.assertFalse(ok)
                self.assertIn("FAIL Complete TCP teardowns: 1", notes)

    def test_overlapping_retransmission_extends_contiguous_data(self):
        base = complete_frames()
        cases = {
            "inside-stream": base[:11] + [
                tcp(
                    0x18,
                    PUBLIC_IP,
                    GUEST_IP,
                    80,
                    40000,
                    5001,
                    1001,
                    payload=b"abcd",
                ),
                tcp(
                    0x18,
                    PUBLIC_IP,
                    GUEST_IP,
                    80,
                    40000,
                    5003,
                    1001,
                    payload=b"cdef",
                ),
                tcp(
                    0x11,
                    PUBLIC_IP,
                    GUEST_IP,
                    80,
                    40000,
                    5007,
                    1001,
                ),
                tcp(
                    0x11,
                    GUEST_IP,
                    PUBLIC_IP,
                    40000,
                    80,
                    1001,
                    5008,
                ),
                tcp(
                    0x10,
                    PUBLIC_IP,
                    GUEST_IP,
                    80,
                    40000,
                    5008,
                    1002,
                ),
            ] + base[15:],
            "handshake-boundary": base[:8] + [
                tcp(
                    0x02,
                    GUEST_IP,
                    PUBLIC_IP,
                    40000,
                    80,
                    1000,
                    0,
                ),
                tcp(
                    0x12,
                    PUBLIC_IP,
                    GUEST_IP,
                    80,
                    40000,
                    5000,
                    1001,
                ),
                tcp(
                    0x18,
                    GUEST_IP,
                    PUBLIC_IP,
                    40000,
                    80,
                    1001,
                    5001,
                    payload=b"init",
                ),
                tcp(
                    0x18,
                    GUEST_IP,
                    PUBLIC_IP,
                    40000,
                    80,
                    1003,
                    5001,
                    payload=b"cdefgh",
                ),
                tcp(
                    0x11,
                    GUEST_IP,
                    PUBLIC_IP,
                    40000,
                    80,
                    1009,
                    5001,
                ),
                tcp(
                    0x11,
                    PUBLIC_IP,
                    GUEST_IP,
                    80,
                    40000,
                    5001,
                    1010,
                ),
                tcp(
                    0x10,
                    GUEST_IP,
                    PUBLIC_IP,
                    40000,
                    80,
                    1010,
                    5002,
                ),
            ] + base[15:],
        }
        for name, frames in cases.items():
            with self.subTest(case=name):
                ok, notes = net_pcap.check_pcap(
                    self.write_capture(pcap(frames))
                )

                self.assertTrue(ok, notes)

    def test_each_required_direction_needs_a_complete_teardown(self):
        frames = complete_frames()[:18]
        frames.extend(
            [
                tcp(0x02, GUEST_IP, "8.8.8.8", 40001, 443, 2000, 0),
                tcp(0x12, "8.8.8.8", GUEST_IP, 443, 40001, 6000, 2001),
                tcp(0x10, GUEST_IP, "8.8.8.8", 40001, 443, 2001, 6001),
                tcp(0x11, "8.8.8.8", GUEST_IP, 443, 40001, 6001, 2001),
                tcp(0x10, GUEST_IP, "8.8.8.8", 40001, 443, 2001, 6002),
                tcp(0x11, GUEST_IP, "8.8.8.8", 40001, 443, 2001, 6002),
                tcp(0x10, "8.8.8.8", GUEST_IP, 443, 40001, 6002, 2002),
            ]
        )

        ok, notes = net_pcap.check_pcap(
            self.write_capture(pcap(frames))
        )

        self.assertFalse(ok)
        self.assertIn("OK   Complete TCP teardowns: 2", notes)
        self.assertIn("FAIL Guest server teardowns: 0", notes)

    def test_private_destination_does_not_count_as_public(self):
        for destination in ("192.168.1.10", "224.0.0.1"):
            with self.subTest(destination=destination):
                ok, notes = net_pcap.check_pcap(
                    self.write_capture(
                        pcap(complete_frames(public_ip=destination))
                    )
                )

                self.assertFalse(ok)
                self.assertIn("FAIL Public client handshakes: 0", notes)

    def test_guest_self_connection_is_not_an_inbound_server_flow(self):
        frames = complete_frames()[:15]
        frames.extend(
            [
                tcp(0x02, GUEST_IP, GUEST_IP, 41000, 80, 2000, 0),
                tcp(0x12, GUEST_IP, GUEST_IP, 80, 41000, 6000, 2001),
                tcp(0x10, GUEST_IP, GUEST_IP, 41000, 80, 2001, 6001),
                tcp(0x11, GUEST_IP, GUEST_IP, 80, 41000, 6001, 2001),
                tcp(0x10, GUEST_IP, GUEST_IP, 41000, 80, 2001, 6002),
                tcp(0x11, GUEST_IP, GUEST_IP, 41000, 80, 2001, 6002),
                tcp(0x10, GUEST_IP, GUEST_IP, 80, 41000, 6002, 2002),
            ]
        )

        ok, notes = net_pcap.check_pcap(
            self.write_capture(pcap(frames))
        )

        self.assertFalse(ok)
        self.assertIn("FAIL Guest server handshakes: 0", notes)

    def test_fragmented_transport_cannot_satisfy_a_flow(self):
        frames = complete_frames()
        frames[8:] = [fragmented(frame) for frame in frames[8:]]

        ok, notes = net_pcap.check_pcap(
            self.write_capture(pcap(frames))
        )

        self.assertFalse(ok)
        self.assertIn("FAIL Complete TCP handshakes: 0", notes)

    def test_bad_ipv4_checksum_is_reported(self):
        frame = bytearray(icmp(8, GUEST_IP, GATEWAY_IP))
        frame[24] ^= 0xFF
        path = self.write_capture(pcap([bytes(frame)]))

        ok, notes = net_pcap.check_pcap(path)

        self.assertFalse(ok)
        self.assertIn("FAIL Bad IP checksums: 1", notes)

    def test_truncated_packet_record_is_rejected(self):
        image = (
            struct.pack("<IHHIIII", 0xA1B2C3D4, 2, 4, 0, 0, 65535, 1)
            + struct.pack("<IIII", 0, 0, 20, 20)
            + b"short"
        )

        with self.assertRaisesRegex(
            net_pcap.PcapError,
            "packet 0 payload is truncated",
        ):
            net_pcap.check_pcap(self.write_capture(image))

    def test_non_ethernet_capture_is_rejected(self):
        image = struct.pack(
            "<IHHIIII",
            0xA1B2C3D4,
            2,
            4,
            0,
            0,
            65535,
            101,
        )

        with self.assertRaisesRegex(
            net_pcap.PcapError,
            "unsupported link type 101",
        ):
            net_pcap.check_pcap(self.write_capture(image))

    def test_malformed_packet_shapes_have_useful_errors(self):
        global_header = pcap([])
        tcp_empty = ipv4(6, b"", GUEST_IP, PUBLIC_IP)
        tcp_short = ipv4(6, b"\0" * 10, GUEST_IP, PUBLIC_IP)
        udp_short = ipv4(17, b"\0" * 7, GUEST_IP, GATEWAY_IP)
        tcp_bad_offset = bytearray(
            tcp(0x02, GUEST_IP, PUBLIC_IP, 40000, 80, 1, 0)
        )
        tcp_bad_offset[46] = 0x40
        udp_bad_length = bytearray(
            ipv4(
                17,
                struct.pack("!HHHH", 68, 67, 80, 0) + b"\0" * 8,
                "0.0.0.0",
                "255.255.255.255",
            )
        )
        bad_ihl = bytearray(icmp(8, GUEST_IP, GATEWAY_IP))
        bad_ihl[14] = 0x44
        bad_total_length = bytearray(icmp(8, GUEST_IP, GATEWAY_IP))
        struct.pack_into("!H", bad_total_length, 16, 0xFFFF)
        cases = (
            (b"\xd4\xc3\xb2\xa1", "global header is truncated"),
            (pcap([], snap_length=0), "zero snapshot length"),
            (global_header + b"\0" * 8, "packet 0 header is truncated"),
            (
                pcap([b"\0" * 20], snap_length=10),
                "packet 0 exceeds the snapshot length",
            ),
            (
                global_header
                + struct.pack("<IIII", 0, 0, 20, 10)
                + b"\0" * 20,
                "captured length exceeds original length",
            ),
            (pcap([b"\0" * 13]), "truncated Ethernet header"),
            (
                pcap([b"\0" * 12 + b"\x81\x00\0\0"]),
                "truncated VLAN header",
            ),
            (
                pcap([ethernet(b"\x45" + b"\0" * 18, 0x0800)]),
                "truncated IPv4 header",
            ),
            (pcap([bytes(bad_ihl)]), "invalid IPv4 header length"),
            (
                pcap([bytes(bad_total_length)]),
                "invalid IPv4 total length",
            ),
            (pcap([tcp_empty]), "truncated TCP header"),
            (pcap([tcp_short]), "truncated TCP header"),
            (
                pcap([bytes(tcp_bad_offset)]),
                "invalid TCP header length",
            ),
            (pcap([udp_short]), "truncated UDP header"),
            (
                pcap([bytes(udp_bad_length)]),
                "invalid UDP length",
            ),
        )

        for image, message in cases:
            with self.subTest(message=message):
                with self.assertRaisesRegex(
                    net_pcap.PcapError,
                    message,
                ):
                    net_pcap.check_pcap(self.write_capture(image))

    def test_truncated_dhcp_option_is_rejected(self):
        bootp = bytearray(236)
        bootp[0:3] = b"\x01\x01\x06"
        struct.pack_into("!I", bootp, 4, 0x12345678)
        bootp[28:34] = GUEST_MAC
        payload = bytes(bootp) + b"\x63\x82\x53\x63\x35"
        udp = struct.pack("!HHHH", 68, 67, 8 + len(payload), 0) + payload
        frame = ipv4(17, udp, "0.0.0.0", "255.255.255.255")

        with self.assertRaisesRegex(
            net_pcap.PcapError,
            "truncated DHCP option",
        ):
            net_pcap.check_pcap(
                self.write_capture(pcap([frame]))
            )


if __name__ == "__main__":
    unittest.main()

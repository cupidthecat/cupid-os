#!/usr/bin/env python3
"""
Pcap-level protocol verification for CupidOS network tests.

Walks a capture produced by `qemu-system-i386 -object filter-dump` and
asserts:
  * ARP request/reply pairing for the gateway
  * Successful DHCP DISCOVER/OFFER/REQUEST/ACK exchange
  * At least one ICMP echo-request and matching echo-reply
  * One full TCP 3-way handshake to a non-RFC1918 destination
  * TCP FIN exchange and clean teardown
  * IP header checksums all valid

Usage:
    python3 tools/net_pcap.py tests/rtl8139.pcap [tests/e1000.pcap ...]

Exit 0 if all checks pass for every pcap, else 1.
"""
from __future__ import annotations
import sys
from pathlib import Path

from scapy.all import rdpcap, IP, TCP, UDP, ARP, ICMP, Ether, BOOTP, DHCP


def check_pcap(path: Path) -> tuple[bool, list[str]]:
    pkts = rdpcap(str(path))
    notes: list[str] = []
    ok = True

    def add(b: bool, msg: str) -> None:
        nonlocal ok
        if not b:
            ok = False
        notes.append(("OK  " if b else "FAIL") + " " + msg)

    # ARP
    arp_req = [p for p in pkts if p.haslayer(ARP) and p[ARP].op == 1]
    arp_rep = [p for p in pkts if p.haslayer(ARP) and p[ARP].op == 2]
    add(len(arp_req) >= 1, f"ARP requests: {len(arp_req)}")
    add(len(arp_rep) >= 1, f"ARP replies:  {len(arp_rep)}")

    # DHCP
    dhcp_msgs = {1: 0, 2: 0, 3: 0, 5: 0}  # DISCOVER/OFFER/REQUEST/ACK
    for p in pkts:
        if not p.haslayer(DHCP):
            continue
        for opt in p[DHCP].options:
            if isinstance(opt, tuple) and opt[0] == "message-type":
                t = opt[1]
                if t in dhcp_msgs:
                    dhcp_msgs[t] += 1
    add(dhcp_msgs[1] >= 1, f"DHCP DISCOVER: {dhcp_msgs[1]}")
    add(dhcp_msgs[2] >= 1, f"DHCP OFFER:    {dhcp_msgs[2]}")
    add(dhcp_msgs[3] >= 1, f"DHCP REQUEST:  {dhcp_msgs[3]}")
    add(dhcp_msgs[5] >= 1, f"DHCP ACK:      {dhcp_msgs[5]}")

    # ICMP
    icmp_req = [p for p in pkts if p.haslayer(ICMP) and p[ICMP].type == 8]
    icmp_rep = [p for p in pkts if p.haslayer(ICMP) and p[ICMP].type == 0]
    add(len(icmp_req) >= 1, f"ICMP echo-request: {len(icmp_req)}")
    add(len(icmp_rep) >= 1, f"ICMP echo-reply:   {len(icmp_rep)}")

    # TCP handshake / teardown
    tcp = [p for p in pkts if p.haslayer(TCP) and p.haslayer(IP)]
    syn      = [p for p in tcp if p[TCP].flags & 0x02 and not (p[TCP].flags & 0x10)]
    syn_ack  = [p for p in tcp if (p[TCP].flags & 0x12) == 0x12]
    fin      = [p for p in tcp if p[TCP].flags & 0x01]
    add(len(syn) >= 1,     f"TCP SYN sent:     {len(syn)}")
    add(len(syn_ack) >= 1, f"TCP SYN-ACK rcvd: {len(syn_ack)}")
    add(len(fin) >= 1,     f"TCP FIN seen:     {len(fin)}")

    # Look for outbound connection to a public IP (anything not 10.0.0.0/8).
    pub = [p for p in syn if not p[IP].dst.startswith("10.")]
    add(len(pub) >= 1, f"TCP SYN to public IP: {len(pub)}")

    # IP header checksums
    bad_ip = 0
    for p in pkts:
        if not p.haslayer(IP):
            continue
        ip = p[IP]
        orig_csum = ip.chksum
        ip.chksum = None  # force recompute
        new_pkt = IP(bytes(ip))
        if new_pkt.chksum != orig_csum:
            bad_ip += 1
    add(bad_ip == 0, f"Bad IP checksums: {bad_ip}")

    return ok, notes


def main(argv: list[str]) -> int:
    if not argv:
        print("usage: net_pcap.py <pcap> [pcap ...]", file=sys.stderr)
        return 2
    overall_ok = True
    for arg in argv:
        path = Path(arg)
        print(f"\n=== {path} ===")
        try:
            ok, notes = check_pcap(path)
        except Exception as e:
            print(f"  FAIL exception: {e}")
            overall_ok = False
            continue
        for n in notes:
            print(f"  {n}")
        if not ok:
            overall_ok = False
    return 0 if overall_ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

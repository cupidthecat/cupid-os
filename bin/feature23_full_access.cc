//help: P6 Phase-4 sanity - exercises new networking + driver bindings
//help: Usage: feature23_full_access
//help: Checks native network argument widths and static integer array data.

I32 feature23_initializer_probe() {
    static U16 ports[3] = {65537, 54321,};
    static I16 samples[3] = {65535, -32768};
    static char bytes[3] = {'A', '\n'};
    static U32 words[2] = {2147483648u, 4294967295u};
    static I32 signed_words[2] = {-2147483647, 2147483647};
    if (sizeof(ports) != 6 || sizeof(samples) != 6 || sizeof(bytes) != 3) return 0;
    if (ports[1] != 54321 || ports[2] != 0) return 0;
    if (samples[0] != -1 || samples[1] != -32768 || samples[2] != 0) return 0;
    if (bytes[0] != 65 || bytes[1] != 10 || bytes[2] != 0) return 0;
    if (words[0] != 2147483648u || words[1] != 4294967295u) return 0;
    if (signed_words[0] != -2147483647 || signed_words[1] != 2147483647) return 0;
    I32 previous = ports[0];
    ports[0] += 1;
    return previous;
}

I32 feature23_halfword_type_probe() {
    U16 u = 65535;
    I16 s = -2;
    U16 grid[2][3];
    I16 cube[2][3][4];
    float4 uf;
    float4 sf;
    double2 ud;
    double2 sd;
    if ((U32)(&u + 1) - (U32)&u != 2) return 0;
    if ((U32)(1 + &s) - (U32)&s != 2) return 0;
    if (sizeof(*grid) != 6 || sizeof(**grid) != 2) return 0;
    if (sizeof(*cube) != 24 || sizeof(**cube) != 8 || sizeof(***cube) != 2) return 0;
    uf = _mm_set1_ps(u);
    sf = _mm_set1_ps(s);
    ud = _mm_set1_pd(u);
    sd = _mm_set1_pd(s);
    if (uf.x != 65535.0f || uf.w != 65535.0f) return 0;
    if (sf.x != -2.0f || sf.w != -2.0f) return 0;
    if (ud.x != 65535.0 || ud.y != 65535.0) return 0;
    if (sd.x != -2.0 || sd.y != -2.0) return 0;
    if ((U16)&u != ((U32)&u & 65535u)) return 0;
    if ((I16)&s != (I16)((U32)&s & 65535u)) return 0;
    return 1;
}

I32 feature23_binding_probe() {
    I32 first = socket(2.0);
    if (first < 0) return 0;
    I32 second = socket(2.0);
    if (second < 0) { close(first); return 0; }
    double first_slot = first;
    double second_slot = second;
    I32 ok = 1;
    U8 bytes[2];
    bytes[0] = 53;
    bytes[1] = 0;
    U32 parsed_word = 0;
    I32 signed_peer = 0;
    U16 ports[2];
    ports[0] = 65000;
    ports[1] = 1234;

    if (sizeof(U16) != 2 || sizeof(I16) != 2 || sizeof(ports) != 4) ok = 0;
    /* Closed/listening sockets and oversize UDP payloads avoid packet output. */
    if (bind(second_slot, 0.0, 0.0) != 0) ok = 0;
    if (listen(second_slot, 3.0) != 0) ok = 0;
    if (sock_state(second) != 1 || sock_state(second_slot) != 1) ok = 0;
    if (sock_state(first) != 0 || sock_avail(second_slot) != 0) ok = 0;
    if (htons(70196.75) != 13330 || ntohs(70196.75) != 13330) ok = 0;
    if (connect(second_slot, 0.0, 65537.0) != -3) ok = 0;
    if (accept(first_slot, &parsed_word, &ports[0]) != -2) ok = 0;
    if (sendto(-1.0, bytes, 2147483649.0, 0.0, 70196.75) != -2) ok = 0;
    if (recvfrom(-1.0, bytes, 2147483649.0, &parsed_word, &ports[0]) != -2) ok = 0;
    if (udp_send_raw(0.0, 70196.75, -1.0, bytes, 1473.0) != -1) ok = 0;
    if (parsed_word != 0 || ports[0] != 65000 || ports[1] != 1234) ok = 0;
    if (send(-1.0, bytes, 2147483649.0) != -2) ok = 0;
    if (recv(-1.0, bytes, 2147483649.0) != -2) ok = 0;
    if (bytes[0] != 53 || bytes[1] != 0) ok = 0;
    if (setsockopt(second_slot, 1.0, 1.0, bytes, 4294967295.0) != -3) ok = 0;
    if (dns_resolve(0, &parsed_word) != -1 || parsed_word != 0) ok = 0;
    if (ip_parse("1.0.0.128", &parsed_word) != 0 || parsed_word != 2147483649u) ok = 0;
    if (ip_parse("1.0.0.128", &signed_peer) != 0 || signed_peer != -2147483647) ok = 0;
    if (htonl(2147483649.0) != 16777344u) ok = 0;
    if (ntohl(16777344.0) != 2147483649u) ok = 0;
    if (close(second_slot) != 0) ok = 0;
    if (sock_state(second) != -2 || sock_state(first) != 0) ok = 0;
    if (close(first_slot) != 0) ok = 0;
    if (ok) serial_printf("PASS feature23_binding bindings=20 slots=4 unsigned=4 peer=1 halfwords=8 guards=2\n");
    else serial_printf("FAIL feature23_binding\n");
    return ok;
}

U0 Main() {
    U8 mac[6];
    U32 ip;
    U32 gw;
    U32 dns;
    U32 mask;
    U32 link;
    U32 rxp;
    U32 txp;
    I32 npci;
    I32 nblk;
    I32 ip_ok;
    U32 parsed = 0;

    /* Network interface info */
    ip   = net_get_ip();
    gw   = net_get_gateway();
    dns  = net_get_dns();
    mask = net_get_mask();
    link = net_link_up();
    rxp  = net_rx_packets();
    txp  = net_tx_packets();
    net_get_mac(mac);

    serial_printf("[feature23] ip=%x gw=%x dns=%x mask=%x link=%d\n",
                  ip, gw, dns, mask, link);
    serial_printf("[feature23] mac=%x:%x:%x:%x:%x:%x rxp=%d txp=%d\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], rxp, txp);

    /* IP parser round-trip */
    ip_ok = ip_parse("8.8.8.8", &parsed);
    serial_printf("[feature23] ip_parse rc=%d val=%x\n", ip_ok, parsed);

    /* PCI introspection */
    npci = pci_device_count();
    serial_printf("[feature23] pci_device_count=%d\n", npci);
    {
        I32 i;
        i = 0;
        while (i < npci) {
            U32 v = pci_get_vendor(i);
            U32 d = pci_get_device_id(i);
            U32 c = pci_get_class(i);
            U32 q = pci_get_irq(i);
            serial_printf("[feature23]  pci[%d] %x:%x class=%x irq=%d\n",
                          i, v, d, c, q);
            i = i + 1;
        }
    }

    /* Block devices */
    nblk = blkdev_count();
    serial_printf("[feature23] blkdev_count=%d\n", nblk);

    /* Concurrency primitives - lock/unlock smoke (must not deadlock) */
    bkl_lock();
    bkl_unlock();

    I32 bindings_ok = feature23_binding_probe();
    I32 initializers_ok = feature23_initializer_probe() == 1;
    if (feature23_initializer_probe() != 2) initializers_ok = 0;
    if (initializers_ok) serial_printf("PASS feature23_initializers arrays=5 calls=2\n");
    else serial_printf("FAIL feature23_initializers\n");
    I32 halfword_types_ok = feature23_halfword_type_probe();
    if (halfword_types_ok) serial_printf("PASS feature23_halfword_types strides=2 rows=3 broadcasts=4 casts=2\n");
    else serial_printf("FAIL feature23_halfword_types\n");

    /* Pass criteria: link up + non-zero IP + at least one PCI device */
    if (link == 1 && ip != 0 && npci > 0 && bindings_ok && initializers_ok && halfword_types_ok) {
        serial_printf("[feature23] PASS\n");
    } else {
        serial_printf("[feature23] FAIL link=%d ip=%x npci=%d\n", link, ip, npci);
    }
}
Main();

//help: P6 Phase-4 sanity - exercises new networking + driver bindings
//help: Usage: feature23_full_access

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

    /* Pass criteria: link up + non-zero IP + at least one PCI device */
    if (link == 1 && ip != 0 && npci > 0) {
        serial_printf("[feature23] PASS\n");
    } else {
        serial_printf("[feature23] FAIL link=%d ip=%x npci=%d\n", link, ip, npci);
    }
}
Main();

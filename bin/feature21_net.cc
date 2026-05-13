//help: P6 TCP client smoke — DNS + connect + HTTP GET
//help: Usage: feature21_net

U0 Main() {
    U32 ip = 0;
    if (dns_resolve("example.com", &ip) != 0) {
        serial_printf("[feature21] SKIP dns failed\n"); return;
    }
    I32 fd = socket(2);
    if (fd < 0) { serial_printf("[feature21] FAIL socket\n"); return; }
    if (connect(fd, ip, htons(80)) != 0) {
        serial_printf("[feature21] SKIP connect failed\n"); close(fd); return;
    }
    const char *req = "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n";
    send(fd, req, strlen(req));
    U8 buf[2048];
    I32 n = recv(fd, buf, 2048);
    close(fd);
    if (n > 100 && buf[0] == 'H' && buf[1] == 'T' && buf[2] == 'T' && buf[3] == 'P') {
        serial_printf("[feature21] PASS (%d bytes)\n", n);
    } else {
        serial_printf("[feature21] FAIL recv n=%d\n", n);
    }
}
Main();

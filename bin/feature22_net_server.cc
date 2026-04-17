//help: P6 TCP server smoke — listen/accept/echo on port 80
//help: Usage: feature22_net_server (then curl http://localhost:8080/ from host)

U0 Main() {
    I32 fd = socket(2);
    if (bind(fd, 0, htons(80)) != 0) {
        serial_printf("[feature22] FAIL bind\n"); close(fd); return;
    }
    if (listen(fd, 1) != 0) {
        serial_printf("[feature22] FAIL listen\n"); close(fd); return;
    }
    serial_printf("[feature22] listening on port 80\n");
    U32 peer_ip = 0;
    U16 peer_port = 0;
    I32 cfd = accept(fd, &peer_ip, &peer_port);
    if (cfd < 0) {
        serial_printf("[feature22] FAIL accept\n"); close(fd); return;
    }
    U8 buf[512];
    I32 n = recv(cfd, buf, 512);
    const char *resp = "HTTP/1.0 200 OK\r\nContent-Length: 13\r\n\r\nHello CupidOS";
    send(cfd, resp, strlen(resp));
    close(cfd); close(fd);
    serial_printf("[feature22] PASS served %d bytes in\n", n);
}
Main();

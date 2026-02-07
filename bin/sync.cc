// sync.cc — Flush disk cache to disk
void main() {
    blockcache_sync();
    print("Cache flushed to disk\n");
}

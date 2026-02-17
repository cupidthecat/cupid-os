/* bmptest.cc - BMP encode/decode round-trip test
 *
 * Creates a test pattern in memory, encodes it as a 24-bit BMP,
 * decodes it back, verifies pixels match, then displays on screen.
 * Uses kmalloc for pixel buffers to stay within CupidC data limits.
 */

int W = 32;
int H = 32;
int NPIX = 1024;    /* 32*32 */
int BUFSZ = 4096;   /* 1024*4 */

int main() {
  int ret = 0;
  int errors = 0;

  println("=== BMP Encode/Decode Test ===");

  /* Allocate pixel buffers on heap (too large for CupidC globals) */
  int *img  = kmalloc(BUFSZ);
  int *img2 = kmalloc(BUFSZ);
  if (img == 0 || img2 == 0) {
    println("FAIL: kmalloc failed");
    return 1;
  }

  /* Step 1: Generate test pattern */
  println("Generating 32x32 test pattern...");
  int y = 0;
  while (y < H) {
    int x = 0;
    while (x < W) {
      int r = x * 8;
      int g = y * 8;
      int b = 0;

      /* Blue cross in center */
      if (x == 16 || y == 16) {
        r = 0;
        g = 0;
        b = 255;
      }

      /* White border */
      if (x == 0 || x == 31 || y == 0 || y == 31) {
        r = 255;
        g = 255;
        b = 255;
      }

      img[y * W + x] = (r << 16) | (g << 8) | b;
      x = x + 1;
    }
    y = y + 1;
  }

  /* Step 2: Encode to BMP file */
  println("Encoding to /tmp/test.bmp...");
  ret = bmp_encode("/tmp/test.bmp", img, 32, 32);
  if (ret != 0) {
    print("FAIL: bmp_encode returned ");
    print_int(ret);
    print("\n");
    kfree(img);
    kfree(img2);
    return 1;
  }
  println("  Encode OK");

  /* Step 3: Read back info to verify header */
  int info[4];   /* width, height, bpp, data_size -- matches bmp_info_t */
  ret = bmp_get_info("/tmp/test.bmp", info);
  if (ret != 0) {
    print("FAIL: bmp_get_info returned ");
    print_int(ret);
    print("\n");
    kfree(img);
    kfree(img2);
    return 1;
  }
  print("  Info: ");
  print_int(info[0]);
  print("x");
  print_int(info[1]);
  print(", bpp=");
  print_int(info[2]);
  print(", bytes=");
  print_int(info[3]);
  print("\n");

  if (info[0] != 32 || info[1] != 32) {
    print("FAIL: dimensions mismatch\n");
    kfree(img);
    kfree(img2);
    return 1;
  }

  /* Step 4: Decode back into second buffer */
  println("Decoding /tmp/test.bmp...");
  ret = bmp_decode("/tmp/test.bmp", img2, BUFSZ);
  if (ret != 0) {
    print("FAIL: bmp_decode returned ");
    print_int(ret);
    print("\n");
    kfree(img);
    kfree(img2);
    return 1;
  }
  println("  Decode OK");

  /* Step 5: Verify round-trip -- compare all pixels */
  println("Verifying round-trip...");
  errors = 0;
  int i = 0;
  while (i < NPIX) {
    if (img[i] != img2[i]) {
      if (errors < 5) {
        print("  Mismatch at pixel ");
        print_int(i);
        print(": ");
        print_hex(img[i]);
        print(" vs ");
        print_hex(img2[i]);
        print("\n");
      }
      errors = errors + 1;
    }
    i = i + 1;
  }

  if (errors == 0) {
    print("PASS: All ");
    print_int(NPIX);
    println(" pixels match!");
  } else {
    print("FAIL: ");
    print_int(errors);
    println(" pixel mismatches");
  }

  /* Step 6: Display the BMP on framebuffer */
  println("Displaying on framebuffer at (304, 224)...");
  ret = bmp_decode_to_fb("/tmp/test.bmp", 304, 224);
  if (ret != 0) {
    print("  bmp_decode_to_fb returned ");
    print_int(ret);
    print("\n");
  } else {
    println("  Displayed OK");
    gfx2d_flip();
  }

  kfree(img);
  kfree(img2);
  println("=== Test Complete ===");
  return 0;
}

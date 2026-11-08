struct Bytes { unsigned int head; char tail[]; };
struct Shorts { unsigned char head; unsigned short tail[]; };
struct Padded { unsigned int head[2]; unsigned char marker; char tail[]; };
struct Cell { unsigned int first, second, third; };
struct Cells { unsigned int head; struct Cell tail[]; };

char *byte_view(struct Bytes *value) { return value->tail; }
char *dot_view(struct Bytes *value) { return (*value).tail; }
const char *const_view(const struct Bytes *value) { return value->tail; }
volatile char *volatile_view(volatile struct Bytes *value) { return value->tail; }
unsigned short *short_view(struct Shorts *value) { return value->tail; }
char *padded_view(struct Padded *value) { return value->tail; }
struct Cell *cell_view(struct Cells *value) { return value->tail; }
char byte_at(struct Bytes *value, unsigned int index) { return value->tail[index]; }
unsigned short short_at(struct Shorts *value, unsigned int index) { return value->tail[index]; }
unsigned int cell_at(struct Cells *value, unsigned int index) { return value->tail[index].third; }
void *whole_array_address(struct Bytes *value) { return &value->tail; }

int main(void) {
  struct Bytes bytes;
  struct Shorts shorts;
  struct Padded padded;
  struct Cells cells;
  if ((char *)byte_view(&bytes) != (char *)&bytes + 4) return 1;
  if (dot_view(&bytes) != byte_view(&bytes)) return 2;
  if (const_view(&bytes) != byte_view(&bytes)) return 3;
  if (volatile_view(&bytes) != byte_view(&bytes)) return 4;
  if ((char *)short_view(&shorts) != (char *)&shorts + 2) return 5;
  if (padded_view(&padded) != (char *)&padded + 9) return 6;
  if ((char *)cell_view(&cells) != (char *)&cells + 4) return 7;
  if (whole_array_address(&bytes) != (void *)byte_view(&bytes)) return 8;
  if (sizeof(struct Bytes) != 4 || sizeof(struct Shorts) != 2 ||
      sizeof(struct Padded) != 12 || sizeof(struct Cells) != 4) return 9;
  return 0;
}

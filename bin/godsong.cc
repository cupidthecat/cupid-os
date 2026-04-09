//help: TempleOS GodSong-style PC speaker generator
//help: Usage: godsong [seed] [quarter_ms]
//help: GUI mode opens a TempleOS-style settings flow before generation.
//help: Generates 2-measure phrases using GodSong rhythm rules, then plays AABB.

#define DUR_4 0
#define DUR_8_8 1
#define DUR_3_3_3 2
#define DUR_16_16_16_16 3
#define DUR_8DOT_16 4
#define DUR_8_16_16 5
#define DUR_16_16_8 6

int gs_seed;
int gs_quarter_ms;
int gs_complexity;
int gs_rests;
int gs_six_eight;
int gs_octave;
int gs_octave_state;

int gs_simple_songs[5];
int gs_normal_songs[5];
int gs_complex_songs[9];

char gs_song1[256];
char gs_song2[256];

int gs_parse_int(char *s) {
  int i = 0;
  int v = 0;
  if (s == 0) return 0;
  while (s[i] >= '0' && s[i] <= '9') {
    v = v * 10 + (s[i] - 48);
    i = i + 1;
  }
  return v;
}

void gs_defaults() {
  gs_complexity = 1;  // TempleOS default: normal
  gs_rests = 0;
  gs_six_eight = 0;
  gs_octave = 4;
  if (gs_quarter_ms < 200 || gs_quarter_ms > 4000) {
    gs_quarter_ms = 800;
  }
}

void gs_settings_ui() {
  char *items[3];
  char in_oct[16];
  char in_tempo[16];
  int c;
  int v;

  items[0] = "Simple";
  items[1] = "Normal";
  items[2] = "Complex";

  c = popup_menu(220, 110, (void*)items, 3);
  if (c >= 0 && c <= 2) {
    gs_complexity = c;
  }

  gs_rests = confirm_dialog("GodSong: Enable rests?");
  gs_six_eight = confirm_dialog("GodSong: Use 6/8 time?");

  in_oct[0] = 0;
  in_tempo[0] = 0;

  if (input_dialog("GodSong octave (1-7):", in_oct, 16)) {
    v = gs_parse_int(in_oct);
    if (v >= 1 && v <= 7) {
      gs_octave = v;
    }
  }

  if (input_dialog("GodSong quarter ms (200-4000):", in_tempo, 16)) {
    v = gs_parse_int(in_tempo);
    if (v >= 200 && v <= 4000) {
      gs_quarter_ms = v;
    }
  }
}

int gs_rand() {
  gs_seed = gs_seed * 1103515245 + 12345;
  return (gs_seed >> 1) & 0x7FFFFFFF;
}

int gs_bits(int n) {
  int mask = (1 << n) - 1;
  return gs_rand() & mask;
}

void gs_seed_from_args() {
  char *args = (char*)get_args();
  int i = 0;
  int v = 0;
  int t = 0;

  if (args == 0 || args[0] == 0) {
    gs_seed = uptime_ms() ^ 0x534F4E47;
    if (gs_seed == 0) gs_seed = 1;
    gs_quarter_ms = 800;
    return;
  }

  while (args[i] >= '0' && args[i] <= '9') {
    v = v * 10 + (args[i] - 48);
    i = i + 1;
  }

  if (v <= 0) v = uptime_ms() ^ 0x534F4E47;
  if (v == 0) v = 1;
  gs_seed = v;

  while (args[i] == ' ') {
    i = i + 1;
  }

  while (args[i] >= '0' && args[i] <= '9') {
    t = t * 10 + (args[i] - 48);
    i = i + 1;
  }

  if (t < 200) t = 800;
  if (t > 4000) t = 4000;
  gs_quarter_ms = t;
}

void gs_init_tables() {
  gs_simple_songs[0] = DUR_4;
  gs_simple_songs[1] = DUR_4;
  gs_simple_songs[2] = DUR_4;
  gs_simple_songs[3] = DUR_4;
  gs_simple_songs[4] = DUR_8_8;

  gs_normal_songs[0] = DUR_4;
  gs_normal_songs[1] = DUR_4;
  gs_normal_songs[2] = DUR_8_8;
  gs_normal_songs[3] = DUR_3_3_3;
  gs_normal_songs[4] = DUR_16_16_16_16;

  gs_complex_songs[0] = DUR_4;
  gs_complex_songs[1] = DUR_4;
  gs_complex_songs[2] = DUR_8_8;
  gs_complex_songs[3] = DUR_8_8;
  gs_complex_songs[4] = DUR_8DOT_16;
  gs_complex_songs[5] = DUR_3_3_3;
  gs_complex_songs[6] = DUR_8_16_16;
  gs_complex_songs[7] = DUR_16_16_8;
  gs_complex_songs[8] = DUR_16_16_16_16;
}

void gs_ins_note(char *buf, int k, int *j) {
  if (k == 0 && gs_rests) {
    buf[*j] = 'R';
    *j = *j + 1;
  } else {
    k = k / 2;
    if (k < 3) {
      if (gs_octave_state != gs_octave) {
        gs_octave_state = gs_octave;
        buf[*j] = gs_octave_state + 48;
        *j = *j + 1;
      }
      if (k == 0) {
        buf[*j] = 'G';
      } else {
        buf[*j] = k - 1 + 'A';
      }
      *j = *j + 1;
    } else {
      if (gs_octave_state != gs_octave + 1) {
        gs_octave_state = gs_octave + 1;
        buf[*j] = gs_octave_state + 48;
        *j = *j + 1;
      }
      buf[*j] = k - 1 + 'A';
      *j = *j + 1;
    }
  }
}

void gs_make_song_str(char *buf) {
  int i;
  int j = 0;
  int len;
  int n;
  int k;
  int k2;
  int duration;
  int last_duration = -1;

  gs_octave_state = gs_octave + 1;
  buf[j++] = '0' + gs_octave_state;

  if (gs_six_eight) {
    len = 6;
    buf[j++] = 'M';
    buf[j++] = '6';
    buf[j++] = '/';
    buf[j++] = '8';
  } else {
    len = 8;
  }

  i = 0;
  while (i < len) {
    n = gs_bits(8);
    if (gs_complexity == 2) {
      duration = gs_complex_songs[n % 9];
    } else if (gs_complexity == 1) {
      duration = gs_normal_songs[n % 5];
    } else {
      duration = gs_simple_songs[n % 5];
    }

    if (duration == DUR_8_8) {
      if (last_duration != DUR_8_8) {
        buf[j++] = 'e';
      }
      gs_ins_note(buf, gs_bits(4), &j);
      gs_ins_note(buf, gs_bits(4), &j);
    } else if (duration == DUR_8DOT_16) {
      buf[j++] = 'e';
      buf[j++] = '.';
      gs_ins_note(buf, gs_bits(4), &j);
      buf[j++] = 's';
      gs_ins_note(buf, gs_bits(4), &j);
      duration = DUR_16_16_16_16;
    } else if (duration == DUR_3_3_3) {
      if (last_duration != DUR_3_3_3) {
        buf[j++] = 'e';
        buf[j++] = 't';
      }
      gs_ins_note(buf, gs_bits(4), &j);
      gs_ins_note(buf, gs_bits(4), &j);
      gs_ins_note(buf, gs_bits(4), &j);
    } else if (duration == DUR_8_16_16) {
      if (last_duration != DUR_8_8) {
        buf[j++] = 'e';
      }
      gs_ins_note(buf, gs_bits(4), &j);
      buf[j++] = 's';
      gs_ins_note(buf, gs_bits(4), &j);
      gs_ins_note(buf, gs_bits(4), &j);
      duration = DUR_16_16_16_16;
    } else if (duration == DUR_16_16_8) {
      if (last_duration != DUR_16_16_16_16) {
        buf[j++] = 's';
      }
      gs_ins_note(buf, gs_bits(4), &j);
      gs_ins_note(buf, gs_bits(4), &j);
      buf[j++] = 'e';
      gs_ins_note(buf, gs_bits(4), &j);
      duration = DUR_8_8;
    } else if (duration == DUR_16_16_16_16) {
      if (last_duration != DUR_16_16_16_16) {
        buf[j++] = 's';
      }
      k = gs_bits(4);
      k2 = gs_bits(4);
      gs_ins_note(buf, k, &j);
      gs_ins_note(buf, k2, &j);
      gs_ins_note(buf, k, &j);
      gs_ins_note(buf, k2, &j);
    } else {
      if (last_duration != DUR_4) {
        buf[j++] = 'q';
      }
      gs_ins_note(buf, gs_bits(4), &j);
    }

    last_duration = duration;
    i = i + 1;
  }

  buf[j] = 0;
}

int gs_base_freq(char note) {
  if (note == 'A') return 440;
  if (note == 'B') return 494;
  if (note == 'C') return 523;
  if (note == 'D') return 587;
  if (note == 'E') return 659;
  if (note == 'F') return 698;
  if (note == 'G') return 784;
  return 0;
}

int gs_freq_for(char note, int octave) {
  int f = gs_base_freq(note);
  int s = octave - 4;

  if (f <= 0) return 0;

  while (s > 0) {
    f = f * 2;
    s = s - 1;
  }
  while (s < 0) {
    f = f / 2;
    s = s + 1;
  }
  return f;
}

void gs_play_song_str(char *song) {
  int i = 0;
  int cur_oct = 4;
  int dur_ms = gs_quarter_ms;

  while (song[i]) {
    char c = song[i];

    if (c >= '0' && c <= '9') {
      cur_oct = c - '0';
      i = i + 1;
      continue;
    }

    if (c == 'M') {
      i = i + 1;
      if (song[i] >= '0' && song[i] <= '9') i = i + 1;
      if (song[i] == '/') i = i + 1;
      if (song[i] >= '0' && song[i] <= '9') i = i + 1;
      continue;
    }

    if (c == 'w') {
      dur_ms = gs_quarter_ms * 4;
      i = i + 1;
      continue;
    }
    if (c == 'h') {
      dur_ms = gs_quarter_ms * 2;
      i = i + 1;
      continue;
    }
    if (c == 'q') {
      dur_ms = gs_quarter_ms;
      i = i + 1;
      continue;
    }
    if (c == 'e') {
      dur_ms = gs_quarter_ms / 2;
      i = i + 1;
      continue;
    }
    if (c == 's') {
      dur_ms = gs_quarter_ms / 4;
      i = i + 1;
      continue;
    }
    if (c == 't') {
      dur_ms = (dur_ms * 2) / 3;
      i = i + 1;
      continue;
    }
    if (c == '.') {
      dur_ms = (dur_ms * 3) / 2;
      i = i + 1;
      continue;
    }
    if (c == '(') {
      i = i + 1;
      continue;
    }

    if (c == 'R') {
      pc_speaker_off();
      sleep_ms(dur_ms);
      i = i + 1;
      continue;
    }

    if (c >= 'A' && c <= 'G') {
      int f = gs_freq_for(c, cur_oct);
      int on_ms = (dur_ms * 9) / 10;
      int off_ms = dur_ms - on_ms;
      if (f > 0) {
        pc_speaker_on(f);
      }
      sleep_ms(on_ms);
      pc_speaker_off();
      if (off_ms > 0) {
        sleep_ms(off_ms);
      }
      i = i + 1;
      continue;
    }

    i = i + 1;
  }
}

void main() {
  gs_seed_from_args();
  gs_defaults();

  if (is_gui_mode()) {
    gs_settings_ui();
  }

  gs_init_tables();

  gs_make_song_str(gs_song1);
  gs_make_song_str(gs_song2);

  print("godsong seed=");
  print_int(gs_seed);
  print("\n");
  print("godsong quarter_ms=");
  print_int(gs_quarter_ms);
  print("\n");
  print("godsong complexity=");
  print_int(gs_complexity);
  print(" rests=");
  print_int(gs_rests);
  print(" six_eight=");
  print_int(gs_six_eight);
  print(" octave=");
  print_int(gs_octave);
  print("\n");
  print("godsong A=");
  println(gs_song1);
  print("godsong B=");
  println(gs_song2);
  println("godsong: playing AABB...");

  gs_play_song_str(gs_song1);
  gs_play_song_str(gs_song1);
  gs_play_song_str(gs_song2);
  gs_play_song_str(gs_song2);

  pc_speaker_off();
  println("godsong: done");
}

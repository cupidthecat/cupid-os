//help: Verifies derived floating updates in CupidC AOT executables.
//help: Usage: feature13_derived_aot

struct feature13_derived_record {
    float single;
    double wide;
};

struct feature13_derived_record feature13_derived_records[2];
int feature13_derived_index_calls;

int feature13_derived_next_index() {
    feature13_derived_index_calls += 1;
    return 1;
}

int main() {
    float pointed = -0.0f;
    float *pointer = &pointed;
    float old_pointed;
    float indexed;
    double old_wide;
    int score;
    int old_pointed_bits;
    int prefix_index_calls;

    feature13_derived_records[1].single = 2.25f;
    feature13_derived_records[1].wide = 6.5;
    feature13_derived_index_calls = 0;

    old_pointed = (*pointer)++;
    indexed = ++feature13_derived_records[
        feature13_derived_next_index()].single;
    prefix_index_calls = feature13_derived_index_calls;
    old_wide = feature13_derived_records[
        feature13_derived_next_index()].wide--;

    score = (int)(pointed * 4.0f) + (int)(indexed * 4.0f) +
            (int)(feature13_derived_records[1].wide * 2.0) +
            (int)(old_wide * 2.0);
    old_pointed_bits = *(int *)&old_pointed;

    if (score != 41 || prefix_index_calls != 1 ||
        feature13_derived_index_calls != 2 ||
        feature13_derived_records[1].single != 3.25f ||
        old_pointed_bits != (int)0x80000000) {
        serial_printf(
            "[feature13-derived-aot] FAIL score=%d once=%d zero=%x\n",
            score, feature13_derived_index_calls, old_pointed_bits);
        return 1;
    }

    serial_printf(
        "[feature13-derived-aot] PASS score=%d once=%d zero=%x\n",
        score, feature13_derived_index_calls, old_pointed_bits);
    return 0;
}

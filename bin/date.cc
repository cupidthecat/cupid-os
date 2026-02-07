//help: Show current date and time
//help: Usage: date [+epoch|+short]
//help: Displays the current date and time from the
//help: hardware real-time clock.
//help:   date         Full date and time
//help:   date +epoch  Seconds since Unix epoch
//help:   date +short  Short date and time

void main() {
    char *args = (char*)get_args();

    if (strcmp(args, "+epoch") == 0) {
        print_int(rtc_epoch());
        print("\n");
        return;
    }

    if (strcmp(args, "+short") == 0) {
        print(date_short_string());
        print("  ");
        println(time_short_string());
        return;
    }

    // Default: full date and time
    print(date_full_string());
    print("  ");
    println(time_string());
}

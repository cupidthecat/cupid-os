//help: Show command history
//help: Usage: history
//help: Displays recently executed commands, numbered from
//help: oldest to newest.

void main() {
    int count = get_history_count();
    if (count == 0) {
        println("No history yet.");
        return;
    }

    int start = count - 1;
    int i = start;
    while (i >= 0) {
        print_int(count - i);
        print(": ");
        char *entry = (char*)get_history_entry(start - i);
        if (entry) {
            print(entry);
        }
        print("\n");
        i = i - 1;
    }
}

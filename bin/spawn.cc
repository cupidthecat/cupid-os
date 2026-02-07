//help:spawn - spawn test processes
//help:Usage: spawn [N]
//help:Spawns N test processes that print messages and yield. Default N=1,
void main() {
    int count = 1;
    char *args = get_args();

    if (args != 0) {
        if (args[0] >= '1') {
            if (args[0] <= '9') {
                count = 0;
                int i = 0;
                while (args[i] >= '0') {
                    if (args[i] > '9') {
                        break;
                    }
                    count = count * 10 + (args[i] - '0');
                    i = i + 1;
                }
            }
        }
    }

    spawn_test(count);
}

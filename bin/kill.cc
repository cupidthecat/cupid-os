//help:kill - kill a process by PID
//help:Usage: kill <pid>
//help:Terminates the process with the given PID. Cannot kill PID 1 (idle process).
void main() {
    char *args = get_args();
    if (args == 0) {
        println("Usage: kill <pid>");
        return;
    }
    if (args[0] == 0) {
        println("Usage: kill <pid>");
        return;
    }

    int pid = 0;
    int i = 0;
    while (args[i] >= '0') {
        if (args[i] > '9') {
            i = -1;
            break;
        }
        pid = pid * 10 + (args[i] - '0');
        i = i + 1;
    }

    if (pid == 0) {
        println("Invalid PID");
        return;
    }
    if (pid == 1) {
        println("Cannot kill idle process (PID 1)");
        return;
    }

    print("Killing PID ");
    print_int(pid);
    println("...");
    process_kill(pid);
}

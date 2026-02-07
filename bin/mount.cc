//help:mount - list mounted filesystems
//help:Usage: mount
//help:Displays all currently mounted filesystems and their mount points.
void main() {
    int count = mount_count();
    if (count == 0) {
        println("No filesystems mounted.");
        return;
    }
    int i = 0;
    while (i < count) {
        char *name = mount_name(i);
        char *path = mount_path(i);
        if (name != 0) {
            if (path != 0) {
                print(name);
                print(" on ");
                println(path);
            }
        }
        i = i + 1;
    }
}

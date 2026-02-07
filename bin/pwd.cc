//help: Print the current working directory
//help: Usage: pwd
//help: Displays the absolute path of the current working directory.

void main() {
    char *cwd = (char*)get_cwd();
    println(cwd);
}

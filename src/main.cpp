#include "filesystem.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <ctime>

static void print_ls(const std::vector<StatInfo>& entries) {
    if (entries.empty()) { std::cout << "(empty directory)\n"; return; }
    std::cout << std::left
              << std::setw(8) << "type"
              << std::setw(10) << "size"
              << std::setw(8)  << "inode"
              << "name\n"
              << std::string(40, '-') << "\n";
    for (const auto& e : entries) {
        std::string type = (e.type == FileType::DIRECTORY) ? "[dir]" : "[file]";
        std::cout << std::left
                  << std::setw(8)  << type
                  << std::setw(10) << e.size
                  << std::setw(8)  << e.inode_num
                  << e.name << "\n";
    }
}

static void print_help() {
    std::cout << R"(
Commands:
  format               create a fresh file system
  mount                load existing disk image
  create <path>        create a new empty file
  mkdir  <path>        create a directory
  write  <path> <data> write text to a file (overwrites)
  read   <path>        print file contents
  ls     [path]        list directory  (default: /)
  rm     <path>        delete a file
  rmdir  <path>        delete an empty directory
  stat   <path>        show file/directory metadata
  df                   show disk usage
  help                 show this message
  exit                 quit (auto-saves disk image)
)";
}

static bool run_command(FileSystem& fs, const std::string& line) {
    std::istringstream ss(line);
    std::string cmd;
    ss >> cmd;
    if (cmd.empty() || cmd[0] == '#') return true;
    if (cmd == "exit" || cmd == "quit") return false;

    if (cmd == "format") {
        fs.format();

    } else if (cmd == "mount") {
        fs.mount();

    } else if (cmd == "create") {
        std::string path; ss >> path;
        if (path.empty()) std::cerr << "Usage: create <path>\n";
        else if (fs.create(path)) std::cout << "Created " << path << "\n";

    } else if (cmd == "mkdir") {
        std::string path; ss >> path;
        if (path.empty()) std::cerr << "Usage: mkdir <path>\n";
        else if (fs.mkdir(path)) std::cout << "Directory created: " << path << "\n";

    } else if (cmd == "write") {
        std::string path; ss >> path;
        std::string data; std::getline(ss >> std::ws, data);
        if (path.empty() || data.empty()) std::cerr << "Usage: write <path> <data>\n";
        else if (fs.write(path, data)) std::cout << "Written " << data.size() << " bytes\n";

    } else if (cmd == "read") {
        std::string path; ss >> path;
        if (path.empty()) std::cerr << "Usage: read <path>\n";
        else {
            std::string content = fs.read(path);
            if (!content.empty()) std::cout << content << "\n";
        }

    } else if (cmd == "ls") {
        std::string path = "/"; ss >> path;
        print_ls(fs.ls(path));

    } else if (cmd == "rm") {
        std::string path; ss >> path;
        if (path.empty()) std::cerr << "Usage: rm <path>\n";
        else if (fs.remove(path)) std::cout << "Removed " << path << "\n";

    } else if (cmd == "rmdir") {
        std::string path; ss >> path;
        if (path.empty()) std::cerr << "Usage: rmdir <path>\n";
        else if (fs.rmdir(path)) std::cout << "Removed " << path << "\n";

    } else if (cmd == "stat") {
        std::string path; ss >> path;
        if (path.empty()) { std::cerr << "Usage: stat <path>\n"; }
        else {
            StatInfo si;
            if (!fs.stat(path, si)) { std::cerr << "Not found: " << path << "\n"; }
            else {
                char tbuf[32];
                time_t t = (time_t)si.modified_at;
                strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", localtime(&t));
                std::cout << "Name    : " << si.name << "\n"
                          << "Inode   : " << si.inode_num << "\n"
                          << "Type    : " << (si.type == FileType::DIRECTORY ? "directory" : "regular file") << "\n"
                          << "Size    : " << si.size << " bytes\n"
                          << "Modified: " << tbuf << "\n";
            }
        }

    } else if (cmd == "df") {
        fs.df();

    } else if (cmd == "help") {
        print_help();

    } else {
        std::cerr << "Unknown command: '" << cmd << "'  (type 'help')\n";
    }
    return true;
}

int main(int argc, char* argv[]) {
    std::string image = (argc >= 2) ? argv[1] : "minifs.img";
    FileSystem fs(image);

    std::cout << "MiniFS — type 'help' for commands\n";
    std::cout << "Image: " << image << "\n\n";

    if (!fs.mount())
        std::cout << "Type 'format' to create a new file system.\n";

    std::string line;
    while (true) {
        std::cout << "minifs> ";
        std::cout.flush();
        if (!std::getline(std::cin, line)) break;
        if (!run_command(fs, line)) break;
    }
    std::cout << "Goodbye.\n";
    return 0;
}
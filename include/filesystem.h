#pragma once
#include "disk.h"
#include <string>
#include <vector>

// Simple stat info returned to the shell
struct StatInfo {
    std::string name;
    FileType    type;
    uint32_t    size;
    uint32_t    inode_num;
    int64_t     modified_at;
};

// ── FileSystem ────────────────────────────────────────────────────────────────
// VFS-style interface. All shell commands go through here.
// Owns a Disk instance; resolves paths and orchestrates inode/block operations.

class FileSystem {
public:
    explicit FileSystem(const std::string& image_path);

    // Lifecycle
    void format();
    bool mount();

    // File operations
    bool        create(const std::string& path);
    bool        remove(const std::string& path);
    bool        write (const std::string& path, const std::string& data);
    std::string read  (const std::string& path);

    // Directory operations
    bool                  mkdir(const std::string& path);
    bool                  rmdir(const std::string& path);
    std::vector<StatInfo> ls   (const std::string& path = "/");

    // Info
    bool stat(const std::string& path, StatInfo& out);
    void df();

private:
    Disk disk_;

    // Resolve "/a/b/c" → inode number of "c". Returns INVALID_INODE if not found.
    uint32_t resolve(const std::string& path) const;

    // Look up one name inside a directory inode.
    uint32_t lookup(uint32_t dir_ino, const std::string& name) const;

    // Read all active entries from a directory's data block.
    std::vector<DirEntry> read_entries(uint32_t dir_ino) const;

    // Add / remove one entry in a directory.
    bool add_entry   (uint32_t dir_ino, const std::string& name, uint32_t target);
    bool remove_entry(uint32_t dir_ino, const std::string& name);

    // Get (or allocate) the physical block for logical block index within a file.
    // Only handles direct blocks for the core version.
    uint32_t get_block(Inode& inode, uint32_t ino, uint32_t idx, bool alloc);

    // Free all data blocks belonging to a file.
    void free_blocks(Inode& inode);

    // Split "/a/b/c" → {"a","b","c"}
    static std::vector<std::string> split(const std::string& path);
};
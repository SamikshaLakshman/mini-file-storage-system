#pragma once
#include "fs_types.h"
#include <string>
#include <vector>

// ── Disk ──────────────────────────────────────────────────────────────────────
// Owns the simulated disk: a flat byte array backed by a file.
// Provides block-level read/write, inode I/O, and bitmap allocation.
//
// All higher layers call read_block / write_block.
// Nothing above this layer touches raw bytes.

class Disk {
public:
    explicit Disk(const std::string& image_path);
    ~Disk();                    // auto-saves on destruction

    void format();              // wipe and initialise fresh FS
    bool load();                // load existing image from file
    void save();                // flush in-memory image to file

    // ── Block I/O ─────────────────────────────────────────────────────────
    void read_block (uint32_t blk, void* buf) const;
    void write_block(uint32_t blk, const void* buf);

    // ── Inode I/O ─────────────────────────────────────────────────────────
    Inode    read_inode (uint32_t num) const;
    void     write_inode(uint32_t num, const Inode& inode);

    // ── Superblock I/O ────────────────────────────────────────────────────
    Superblock read_superblock() const;
    void       write_superblock(const Superblock& sb);

    // ── Allocation ────────────────────────────────────────────────────────
    uint32_t alloc_block();              // returns NULL_BLOCK on failure
    uint32_t alloc_inode();              // returns INVALID_INODE on failure
    void     free_block(uint32_t blk);
    void     free_inode(uint32_t num);

    bool is_ready() const { return ready_; }

private:
    std::string          path_;
    std::vector<uint8_t> data_;          // entire disk lives here
    bool                 ready_ = false;

    bool bitmap_get(uint32_t bitmap_blk, uint32_t idx) const;
    void bitmap_set(uint32_t bitmap_blk, uint32_t idx, bool val);
    int  bitmap_find_free(uint32_t bitmap_blk, uint32_t count) const;
};
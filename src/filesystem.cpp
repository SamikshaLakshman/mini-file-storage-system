#include "filesystem.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <ctime>
#include <algorithm>

// ── Constructor / lifecycle ───────────────────────────────────────────────────

FileSystem::FileSystem(const std::string& path) : disk_(path) {}

void FileSystem::format() {
    disk_.format();
    std::cout << "Formatted. Fresh MiniFS ready.\n";
}

bool FileSystem::mount() {
    if (!disk_.load()) {
        std::cout << "No disk image found. Run 'format' first.\n";
        return false;
    }
    Superblock sb = disk_.read_superblock();
    std::cout << "Mounted " << sb.label
              << "  |  " << sb.free_blocks << "/" << sb.total_blocks
              << " blocks free\n";
    return true;
}

// ── Path utilities ────────────────────────────────────────────────────────────

std::vector<std::string> FileSystem::split(const std::string& path) {
    std::vector<std::string> parts;
    std::istringstream ss(path);
    std::string tok;
    while (std::getline(ss, tok, '/'))
        if (!tok.empty()) parts.push_back(tok);
    return parts;
}

// Walk the directory tree component by component.
// "/" returns ROOT_INODE.  "/a/b/c" returns inode of "c".
uint32_t FileSystem::resolve(const std::string& path) const {
    auto parts = split(path);
    uint32_t cur = ROOT_INODE;
    for (const auto& p : parts) {
        Inode inode = disk_.read_inode(cur);
        if (inode.file_type != FileType::DIRECTORY) return INVALID_INODE;
        cur = lookup(cur, p);
        if (cur == INVALID_INODE) return INVALID_INODE;
    }
    return cur;
}

// ── Directory helpers ─────────────────────────────────────────────────────────

// A directory's data is a flat array of DirEntry structs.
// MAX entries per block: 4096 / 64 = 64 entries.
std::vector<DirEntry> FileSystem::read_entries(uint32_t dir_ino) const {
    Inode inode = disk_.read_inode(dir_ino);
    std::vector<DirEntry> result;
    if (inode.direct[0] == NULL_BLOCK) return result;

    uint8_t buf[BLOCK_SIZE];
    disk_.read_block(inode.direct[0], buf);

    constexpr uint32_t N = BLOCK_SIZE / sizeof(DirEntry);
    for (uint32_t i = 0; i < N; ++i) {
        DirEntry e;
        memcpy(&e, buf + i * sizeof(DirEntry), sizeof(DirEntry));
        if (e.in_use) result.push_back(e);
    }
    return result;
}

uint32_t FileSystem::lookup(uint32_t dir_ino, const std::string& name) const {
    Inode inode = disk_.read_inode(dir_ino);
    if (inode.direct[0] == NULL_BLOCK) return INVALID_INODE;

    uint8_t buf[BLOCK_SIZE];
    disk_.read_block(inode.direct[0], buf);

    constexpr uint32_t N = BLOCK_SIZE / sizeof(DirEntry);
    for (uint32_t i = 0; i < N; ++i) {
        DirEntry e;
        memcpy(&e, buf + i * sizeof(DirEntry), sizeof(DirEntry));
        if (e.in_use && name == e.name) return e.inode_num;
    }
    return INVALID_INODE;
}

bool FileSystem::add_entry(uint32_t dir_ino, const std::string& name, uint32_t target) {
    if (name.size() >= MAX_FILENAME) {
        std::cerr << "Error: filename too long (max " << MAX_FILENAME - 1 << ")\n";
        return false;
    }

    Inode inode = disk_.read_inode(dir_ino);

    // Allocate a data block for this directory if it has none yet.
    if (inode.direct[0] == NULL_BLOCK) {
        uint32_t blk = disk_.alloc_block();
        if (blk == NULL_BLOCK) { std::cerr << "Error: disk full\n"; return false; }
        inode.direct[0] = blk;
        disk_.write_inode(dir_ino, inode);
    }

    uint8_t buf[BLOCK_SIZE];
    disk_.read_block(inode.direct[0], buf);

    constexpr uint32_t N = BLOCK_SIZE / sizeof(DirEntry);
    for (uint32_t i = 0; i < N; ++i) {
        DirEntry e;
        memcpy(&e, buf + i * sizeof(DirEntry), sizeof(DirEntry));
        if (!e.in_use) {
            e.clear();
            strncpy(e.name, name.c_str(), MAX_FILENAME - 1);
            e.inode_num = target;
            e.in_use    = 1;
            memcpy(buf + i * sizeof(DirEntry), &e, sizeof(DirEntry));
            disk_.write_block(inode.direct[0], buf);
            return true;
        }
    }
    std::cerr << "Error: directory is full\n";
    return false;
}

bool FileSystem::remove_entry(uint32_t dir_ino, const std::string& name) {
    Inode inode = disk_.read_inode(dir_ino);
    if (inode.direct[0] == NULL_BLOCK) return false;

    uint8_t buf[BLOCK_SIZE];
    disk_.read_block(inode.direct[0], buf);

    constexpr uint32_t N = BLOCK_SIZE / sizeof(DirEntry);
    for (uint32_t i = 0; i < N; ++i) {
        DirEntry e;
        memcpy(&e, buf + i * sizeof(DirEntry), sizeof(DirEntry));
        if (e.in_use && name == e.name) {
            e.in_use = 0;
            memcpy(buf + i * sizeof(DirEntry), &e, sizeof(DirEntry));
            disk_.write_block(inode.direct[0], buf);
            return true;
        }
    }
    return false;
}

// ── Block mapping ─────────────────────────────────────────────────────────────
// Maps logical block index (0, 1, 2...) inside a file to a physical block number.
// For this core version only direct blocks (indices 0–11) are supported.
uint32_t FileSystem::get_block(Inode& inode, uint32_t ino, uint32_t idx, bool alloc) {
    if (idx >= DIRECT_BLOCKS) {
        std::cerr << "Error: file too large for core version (max "
                  << DIRECT_BLOCKS * BLOCK_SIZE / 1024 << " KB)\n";
        return NULL_BLOCK;
    }
    if (inode.direct[idx] == NULL_BLOCK && alloc) {
        uint32_t blk = disk_.alloc_block();
        if (blk == NULL_BLOCK) return NULL_BLOCK;
        inode.direct[idx] = blk;
        disk_.write_inode(ino, inode);
    }
    return inode.direct[idx];
}

void FileSystem::free_blocks(Inode& inode) {
    for (auto& b : inode.direct) {
        if (b != NULL_BLOCK) { disk_.free_block(b); b = NULL_BLOCK; }
    }
}

// ── File operations ───────────────────────────────────────────────────────────

bool FileSystem::create(const std::string& path) {
    auto parts = split(path);
    if (parts.empty()) { std::cerr << "Error: invalid path\n"; return false; }

    std::string name = parts.back();
    parts.pop_back();

    // Build parent path string and resolve it.
    std::string parent = "/";
    for (const auto& p : parts) parent += p + "/";
    uint32_t parent_ino = resolve(parent);
    if (parent_ino == INVALID_INODE) {
        std::cerr << "Error: parent directory not found: " << parent << "\n";
        return false;
    }
    if (lookup(parent_ino, name) != INVALID_INODE) {
        std::cerr << "Error: already exists: " << path << "\n";
        return false;
    }

    uint32_t new_ino = disk_.alloc_inode();
    if (new_ino == INVALID_INODE) { std::cerr << "Error: no free inodes\n"; return false; }

    Inode inode; inode.init(FileType::REGULAR);
    disk_.write_inode(new_ino, inode);

    if (!add_entry(parent_ino, name, new_ino)) {
        disk_.free_inode(new_ino);
        return false;
    }
    return true;
}

bool FileSystem::mkdir(const std::string& path) {
    auto parts = split(path);
    if (parts.empty()) { std::cerr << "Error: invalid path\n"; return false; }

    std::string name = parts.back();
    parts.pop_back();
    std::string parent = "/";
    for (const auto& p : parts) parent += p + "/";

    uint32_t parent_ino = resolve(parent);
    if (parent_ino == INVALID_INODE) {
        std::cerr << "Error: parent not found\n"; return false;
    }
    if (lookup(parent_ino, name) != INVALID_INODE) {
        std::cerr << "Error: already exists\n"; return false;
    }

    uint32_t new_ino = disk_.alloc_inode();
    if (new_ino == INVALID_INODE) { std::cerr << "Error: no free inodes\n"; return false; }

    Inode inode; inode.init(FileType::DIRECTORY);
    disk_.write_inode(new_ino, inode);

    if (!add_entry(parent_ino, name, new_ino)) {
        disk_.free_inode(new_ino);
        return false;
    }
    return true;
}

bool FileSystem::write(const std::string& path, const std::string& data) {
    uint32_t ino = resolve(path);
    if (ino == INVALID_INODE) { std::cerr << "Error: not found: " << path << "\n"; return false; }

    Inode inode = disk_.read_inode(ino);
    if (inode.file_type != FileType::REGULAR) { std::cerr << "Error: not a file\n"; return false; }

    // Overwrite: free existing blocks first, then write from scratch.
    free_blocks(inode);
    inode.size = 0;
    disk_.write_inode(ino, inode);
    inode = disk_.read_inode(ino);

    const char* src   = data.c_str();
    uint32_t remaining = (uint32_t)data.size();
    uint32_t offset    = 0;

    while (remaining > 0) {
        uint32_t blk_idx = offset / BLOCK_SIZE;
        uint32_t blk_off = offset % BLOCK_SIZE;
        uint32_t chunk   = std::min(remaining, BLOCK_SIZE - blk_off);

        uint32_t blk = get_block(inode, ino, blk_idx, true);
        if (blk == NULL_BLOCK) { std::cerr << "Error: disk full\n"; return false; }

        uint8_t buf[BLOCK_SIZE];
        disk_.read_block(blk, buf);
        memcpy(buf + blk_off, src, chunk);
        disk_.write_block(blk, buf);

        src       += chunk;
        offset    += chunk;
        remaining -= chunk;
    }

    inode = disk_.read_inode(ino);
    inode.size        = offset;
    inode.modified_at = (int64_t)time(nullptr);
    disk_.write_inode(ino, inode);
    return true;
}

std::string FileSystem::read(const std::string& path) {
    uint32_t ino = resolve(path);
    if (ino == INVALID_INODE) { std::cerr << "Error: not found: " << path << "\n"; return ""; }

    Inode inode = disk_.read_inode(ino);
    if (inode.file_type != FileType::REGULAR) { std::cerr << "Error: not a file\n"; return ""; }

    std::string result;
    result.reserve(inode.size);
    uint32_t remaining = inode.size;
    uint32_t offset    = 0;

    while (remaining > 0) {
        uint32_t blk_idx = offset / BLOCK_SIZE;
        uint32_t blk_off = offset % BLOCK_SIZE;
        uint32_t chunk   = std::min(remaining, BLOCK_SIZE - blk_off);

        uint32_t blk = get_block(inode, ino, blk_idx, false);
        if (blk == NULL_BLOCK) break;

        uint8_t buf[BLOCK_SIZE];
        disk_.read_block(blk, buf);
        result.append(reinterpret_cast<char*>(buf + blk_off), chunk);

        offset    += chunk;
        remaining -= chunk;
    }
    return result;
}

bool FileSystem::remove(const std::string& path) {
    auto parts = split(path);
    if (parts.empty()) return false;
    std::string name = parts.back();
    parts.pop_back();
    std::string parent = "/";
    for (const auto& p : parts) parent += p + "/";

    uint32_t parent_ino = resolve(parent);
    if (parent_ino == INVALID_INODE) { std::cerr << "Error: parent not found\n"; return false; }

    uint32_t ino = lookup(parent_ino, name);
    if (ino == INVALID_INODE) { std::cerr << "Error: not found: " << path << "\n"; return false; }

    Inode inode = disk_.read_inode(ino);
    if (inode.file_type == FileType::DIRECTORY) {
        std::cerr << "Error: use rmdir for directories\n"; return false;
    }

    free_blocks(inode);
    disk_.write_inode(ino, inode);
    disk_.free_inode(ino);
    remove_entry(parent_ino, name);
    return true;
}

bool FileSystem::rmdir(const std::string& path) {
    uint32_t ino = resolve(path);
    if (ino == INVALID_INODE) { std::cerr << "Error: not found\n"; return false; }
    if (ino == ROOT_INODE)    { std::cerr << "Error: cannot remove root\n"; return false; }

    Inode inode = disk_.read_inode(ino);
    if (inode.file_type != FileType::DIRECTORY) { std::cerr << "Error: not a directory\n"; return false; }
    if (!read_entries(ino).empty()) { std::cerr << "Error: directory not empty\n"; return false; }

    auto parts = split(path);
    std::string name = parts.back();
    parts.pop_back();
    std::string parent = "/";
    for (const auto& p : parts) parent += p + "/";
    uint32_t parent_ino = resolve(parent);

    free_blocks(inode);
    disk_.free_inode(ino);
    remove_entry(parent_ino, name);
    return true;
}

// ── ls / stat / df ────────────────────────────────────────────────────────────

std::vector<StatInfo> FileSystem::ls(const std::string& path) {
    uint32_t ino = resolve(path);
    std::vector<StatInfo> result;
    if (ino == INVALID_INODE) { std::cerr << "Error: not found: " << path << "\n"; return result; }

    Inode inode = disk_.read_inode(ino);
    if (inode.file_type != FileType::DIRECTORY) {
        std::cerr << "Error: not a directory\n"; return result;
    }

    for (const auto& e : read_entries(ino)) {
        Inode fi = disk_.read_inode(e.inode_num);
        result.push_back({ e.name, fi.file_type, fi.size, e.inode_num, fi.modified_at });
    }
    return result;
}

bool FileSystem::stat(const std::string& path, StatInfo& out) {
    uint32_t ino = resolve(path);
    if (ino == INVALID_INODE) return false;
    Inode inode = disk_.read_inode(ino);
    auto parts = split(path);
    out = { parts.empty() ? "/" : parts.back(), inode.file_type,
            inode.size, ino, inode.modified_at };
    return true;
}

void FileSystem::df() {
    Superblock sb = disk_.read_superblock();
    uint32_t used_blk = sb.total_blocks - sb.free_blocks;
    uint32_t used_ino = sb.total_inodes - sb.free_inodes;
    std::cout << "\nFilesystem : " << sb.label << "\n";
    std::cout << "Block size : " << sb.block_size << " B\n";
    std::cout << "Blocks     : " << used_blk << " used / " << sb.total_blocks
              << " total  (" << sb.free_blocks << " free)\n";
    std::cout << "Inodes     : " << used_ino  << " used / " << sb.total_inodes
              << " total  (" << sb.free_inodes << " free)\n";
    std::cout << "Used       : " << (used_blk * sb.block_size) / 1024
              << " KB / " << (sb.total_blocks * sb.block_size) / 1024 << " KB\n";
}
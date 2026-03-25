#include "disk.h"
#include <fstream>
#include <cstring>
#include <stdexcept>
#include <iostream>

Disk::Disk(const std::string& path)
    : path_(path), data_(TOTAL_BLOCKS * BLOCK_SIZE, 0)
{}

Disk::~Disk() {
    if (ready_) save();
}

// ── Format ────────────────────────────────────────────────────────────────────
void Disk::format() {
    std::fill(data_.begin(), data_.end(), 0);

    // Superblock
    Superblock sb; sb.init();
    write_superblock(sb);

    // Mark root inode as used
    bitmap_set(INODE_BITMAP_BLOCK, ROOT_INODE, true);

    // Mark all metadata blocks as used
    for (uint32_t i = 0; i < DATA_BLOCK_START; ++i)
        bitmap_set(BLOCK_BITMAP_BLOCK, i, true);

    // Root directory inode
    Inode root;
    root.init(FileType::DIRECTORY);
    write_inode(ROOT_INODE, root);

    ready_ = true;
    save();
}

// ── Persistence ───────────────────────────────────────────────────────────────
bool Disk::load() {
    std::ifstream f(path_, std::ios::binary);
    if (!f) return false;
    f.read(reinterpret_cast<char*>(data_.data()), (std::streamsize)data_.size());
    if (!f) return false;
    if (read_superblock().magic != 0xDEADC0DE) return false;
    ready_ = true;
    return true;
}

void Disk::save() {
    std::ofstream f(path_, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("Cannot write disk image: " + path_);
    f.write(reinterpret_cast<const char*>(data_.data()), (std::streamsize)data_.size());
}

// ── Block I/O ─────────────────────────────────────────────────────────────────
void Disk::read_block(uint32_t blk, void* buf) const {
    if (blk >= TOTAL_BLOCKS) throw std::out_of_range("read_block: out of range");
    memcpy(buf, data_.data() + blk * BLOCK_SIZE, BLOCK_SIZE);
}

void Disk::write_block(uint32_t blk, const void* buf) {
    if (blk >= TOTAL_BLOCKS) throw std::out_of_range("write_block: out of range");
    memcpy(data_.data() + blk * BLOCK_SIZE, buf, BLOCK_SIZE);
}

// ── Inode I/O ─────────────────────────────────────────────────────────────────
// 32 inodes per block (4096 / 128 = 32)
Inode Disk::read_inode(uint32_t num) const {
    if (num >= INODE_COUNT) throw std::out_of_range("read_inode: out of range");
    constexpr uint32_t PER_BLOCK = BLOCK_SIZE / sizeof(Inode);
    uint32_t blk = INODE_TABLE_START + num / PER_BLOCK;
    uint32_t off = (num % PER_BLOCK) * sizeof(Inode);
    uint8_t buf[BLOCK_SIZE];
    read_block(blk, buf);
    Inode inode;
    memcpy(&inode, buf + off, sizeof(Inode));
    return inode;
}

void Disk::write_inode(uint32_t num, const Inode& inode) {
    if (num >= INODE_COUNT) throw std::out_of_range("write_inode: out of range");
    constexpr uint32_t PER_BLOCK = BLOCK_SIZE / sizeof(Inode);
    uint32_t blk = INODE_TABLE_START + num / PER_BLOCK;
    uint32_t off = (num % PER_BLOCK) * sizeof(Inode);
    uint8_t buf[BLOCK_SIZE];
    read_block(blk, buf);
    memcpy(buf + off, &inode, sizeof(Inode));
    write_block(blk, buf);
}

// ── Superblock I/O ────────────────────────────────────────────────────────────
Superblock Disk::read_superblock() const {
    uint8_t buf[BLOCK_SIZE];
    read_block(SUPERBLOCK_BLOCK, buf);
    Superblock sb;
    memcpy(&sb, buf, sizeof(sb));
    return sb;
}

void Disk::write_superblock(const Superblock& sb) {
    uint8_t buf[BLOCK_SIZE] = {};
    memcpy(buf, &sb, sizeof(sb));
    write_block(SUPERBLOCK_BLOCK, buf);
}

// ── Bitmap ────────────────────────────────────────────────────────────────────
bool Disk::bitmap_get(uint32_t bblk, uint32_t idx) const {
    uint8_t buf[BLOCK_SIZE];
    read_block(bblk, buf);
    return (buf[idx / 8] >> (idx % 8)) & 1;
}

void Disk::bitmap_set(uint32_t bblk, uint32_t idx, bool val) {
    uint8_t buf[BLOCK_SIZE];
    read_block(bblk, buf);
    if (val) buf[idx/8] |=  (uint8_t)(1 << (idx % 8));
    else     buf[idx/8] &= ~(uint8_t)(1 << (idx % 8));
    write_block(bblk, buf);
}

int Disk::bitmap_find_free(uint32_t bblk, uint32_t count) const {
    uint8_t buf[BLOCK_SIZE];
    read_block(bblk, buf);
    for (uint32_t i = 0; i < count; ++i)
        if (!((buf[i/8] >> (i%8)) & 1)) return (int)i;
    return -1;
}

// ── Allocation ────────────────────────────────────────────────────────────────
uint32_t Disk::alloc_block() {
    int idx = bitmap_find_free(BLOCK_BITMAP_BLOCK, TOTAL_BLOCKS);
    if (idx < 0) return NULL_BLOCK;
    bitmap_set(BLOCK_BITMAP_BLOCK, (uint32_t)idx, true);
    // Zero out the freshly allocated block
    uint8_t zero[BLOCK_SIZE] = {};
    write_block((uint32_t)idx, zero);
    Superblock sb = read_superblock();
    if (sb.free_blocks > 0) { sb.free_blocks--; write_superblock(sb); }
    return (uint32_t)idx;
}

uint32_t Disk::alloc_inode() {
    int idx = bitmap_find_free(INODE_BITMAP_BLOCK, INODE_COUNT);
    if (idx < 0) return INVALID_INODE;
    bitmap_set(INODE_BITMAP_BLOCK, (uint32_t)idx, true);
    Superblock sb = read_superblock();
    if (sb.free_inodes > 0) { sb.free_inodes--; write_superblock(sb); }
    return (uint32_t)idx;
}

void Disk::free_block(uint32_t blk) {
    if (blk == NULL_BLOCK || blk < DATA_BLOCK_START) return;
    bitmap_set(BLOCK_BITMAP_BLOCK, blk, false);
    Superblock sb = read_superblock();
    sb.free_blocks++;
    write_superblock(sb);
}

void Disk::free_inode(uint32_t num) {
    if (num >= INODE_COUNT) return;
    bitmap_set(INODE_BITMAP_BLOCK, num, false);
    Superblock sb = read_superblock();
    sb.free_inodes++;
    write_superblock(sb);
}
#pragma once
#include <cstdint>
#include <cstring>
#include <ctime>

// ── Disk geometry ─────────────────────────────────────────────────────────────
static constexpr uint32_t BLOCK_SIZE    = 4096;   // 4 KB per block
static constexpr uint32_t TOTAL_BLOCKS  = 1024;   // 4 MB simulated disk
static constexpr uint32_t INODE_COUNT   = 128;
static constexpr uint32_t DIRECT_BLOCKS = 12;     // pointers stored directly in inode

// Block layout:
//   Block 0      → Superblock
//   Block 1      → Inode bitmap   (1 bit per inode)
//   Block 2      → Block bitmap   (1 bit per block)
//   Blocks 3–18  → Inode table    (128 inodes × 128 bytes, 16 blocks)
//   Blocks 19+   → Data blocks
static constexpr uint32_t SUPERBLOCK_BLOCK   = 0;
static constexpr uint32_t INODE_BITMAP_BLOCK = 1;
static constexpr uint32_t BLOCK_BITMAP_BLOCK = 2;
static constexpr uint32_t INODE_TABLE_START  = 3;
static constexpr uint32_t INODE_TABLE_BLOCKS = 16;
static constexpr uint32_t DATA_BLOCK_START   = INODE_TABLE_START + INODE_TABLE_BLOCKS; // 19

static constexpr uint32_t MAX_FILENAME  = 56;
static constexpr uint32_t ROOT_INODE    = 0;
static constexpr uint32_t NULL_BLOCK    = 0xFFFFFFFF;
static constexpr uint32_t INVALID_INODE = INODE_COUNT;

// ── File type ─────────────────────────────────────────────────────────────────
enum class FileType : uint8_t {
    UNUSED    = 0,
    REGULAR   = 1,
    DIRECTORY = 2,
};

// ── On-disk inode (exactly 128 bytes) ────────────────────────────────────────
// Stores everything ABOUT a file — size, type, timestamps, block pointers.
// Does NOT store the filename. That lives in DirEntry.
// Layout: 1+1+2+4+4+4(pad)+8+8+48+4+4+40(pad) = 128
struct Inode {
    FileType file_type;
    uint8_t  _pad0;           // alignment
    uint16_t link_count;
    uint32_t size;            // file size in bytes
    uint32_t uid;             // owner
    uint32_t _pad1;           // align int64_t to 8-byte boundary
    int64_t  created_at;
    int64_t  modified_at;
    uint32_t direct[DIRECT_BLOCKS];   // block pointers (12 × 4 = 48 bytes)
    uint32_t single_indirect;         // → block full of pointers (for large files)
    uint32_t double_indirect;         // → two levels of pointer blocks
    uint8_t  _pad2[40];               // pad to 128 bytes

    void init(FileType ft, uint32_t owner = 0) {
        memset(this, 0, sizeof(*this));
        file_type        = ft;
        uid              = owner;
        link_count       = 1;
        created_at       = modified_at = (int64_t)time(nullptr);
        single_indirect  = NULL_BLOCK;
        double_indirect  = NULL_BLOCK;
        for (auto& b : direct) b = NULL_BLOCK;
    }
};
static_assert(sizeof(Inode) == 128, "Inode must be exactly 128 bytes");

// ── On-disk directory entry (exactly 64 bytes) ───────────────────────────────
// Maps a filename → inode number. Lives inside a directory's data block.
struct DirEntry {
    char     name[MAX_FILENAME];  // null-terminated (max 55 chars + '\0')
    uint32_t inode_num;
    uint8_t  in_use;
    uint8_t  _pad[3];

    void clear() { memset(this, 0, sizeof(*this)); }
};
static_assert(sizeof(DirEntry) == 64, "DirEntry must be exactly 64 bytes");

// ── Superblock (stored in block 0) ───────────────────────────────────────────
struct Superblock {
    uint32_t magic;           // 0xDEADC0DE — validity check on mount
    uint32_t total_blocks;
    uint32_t total_inodes;
    uint32_t free_blocks;
    uint32_t free_inodes;
    uint32_t block_size;
    char     label[32];

    void init() {
        magic        = 0xDEADC0DE;
        total_blocks = TOTAL_BLOCKS;
        total_inodes = INODE_COUNT;
        free_blocks  = TOTAL_BLOCKS - DATA_BLOCK_START;
        free_inodes  = INODE_COUNT - 1; // root inode pre-allocated
        block_size   = BLOCK_SIZE;
        strncpy(label, "MiniFS v1.0", sizeof(label) - 1);
    }
};
# Mini File Storage System

A user-space file system simulator in C++17. Models the core internals of a Unix file system: inodes, directory tables, bitmap-based block allocation, and path resolution.

---

## Build & run

```bash
make        # builds ./minifs
make run    # starts the interactive shell
make clean  # removes binaries and .img files
```

Requires `g++` with C++17 support.

---

## Architecture

```
Shell (main.cpp)
      │
      ▼
FileSystem layer       — path resolution, inode & directory management
      │
      ▼
Disk layer             — block I/O, bitmap allocation, inode table
      │
      ▼
In-memory byte array   — persisted to minifs.img on exit
```

### On-disk layout (4 MB image)

| Block(s)  | Contents           |
|-----------|--------------------|
| 0         | Superblock         |
| 1         | Inode bitmap       |
| 2         | Block bitmap       |
| 3 – 18    | Inode table        |
| 19 – 1023 | Data blocks        |

### Key structures

**Inode (128 bytes)** — stores everything *about* a file except its name: size, type, timestamps, and 12 direct block pointers. Never stores the filename.

**DirEntry (64 bytes)** — maps a filename → inode number. Stored inside directory data blocks.

**Superblock** — filesystem label, total/free counts for blocks and inodes.

---

## Shell commands

```
format               create a fresh file system
mount                load existing disk image
create <path>        create a new empty file
mkdir  <path>        create a directory
write  <path> <data> write text to a file (overwrites)
read   <path>        print file contents
ls     [path]        list directory (default: /)
rm     <path>        delete a file
rmdir  <path>        delete an empty directory
stat   <path>        show metadata
df                   disk usage summary
exit                 quit and auto-save
```

---

## Example

```
minifs> format
Formatted. Fresh MiniFS ready.

minifs> mkdir /projects
Directory created: /projects

minifs> create /projects/notes.txt
Created /projects/notes.txt

minifs> write /projects/notes.txt Learning OS internals
Written 21 bytes

minifs> read /projects/notes.txt
Learning OS internals

minifs> ls /projects
type    size      inode   name
----------------------------------------
[file]  21        2       notes.txt

minifs> stat /projects/notes.txt
Name    : notes.txt
Inode   : 2
Type    : regular file
Size    : 21 bytes
Modified: 2025-08-01 14:05:33

minifs> df
Filesystem : MiniFS v1.0
Block size : 4096 B
Blocks     : 22 used / 1024 total  (1002 free)
Inodes     : 3 used / 128 total  (125 free)
Used       : 88 KB / 4096 KB
```

---

## Design notes

- **Inodes are separate from filenames** — enables hard links; renaming never moves data
- **Fixed 4 KB blocks** — eliminates external fragmentation; matches Linux page size
- **Bitmap allocation** — O(n/8) space, simple linear scan to find free blocks
- **Persistence** — disk image auto-saved to `minifs.img` on exit; reloaded on next run

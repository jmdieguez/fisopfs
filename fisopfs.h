//
// Created by manu on 20/12/22.
//

#ifndef SISOP_2022B_G23_FISOPFS_H
#define SISOP_2022B_G23_FISOPFS_H

#define FS_FILENAME_LEN 64
#define BLOCK_SIZE 256
#define N_BLOCKS 256
#define N_INODES 64  // 1 inode : 4 blocks ratio
#define N_FILES_DIR 16
#define N_BLOCKS_INODE 16  // files max size = 4096 bytes
#define SUPERBLOCK_MAGIC 123456
#define MAX_FILE_NAME_SIZE 50
#define MAX_DEPTH_DIR 8
#define PERMISSION_DENIED -13

struct superblock {
    int magic;
    int n_files;
    int n_dirs;
};

struct bmap_blocks {
    int free_blocks[N_BLOCKS];
};

struct bmap_inodes {
    int free_inodes[N_INODES];
};

struct block {
    char content[N_BLOCKS];
    int free_space;
    int ref;  // reference to next data block
};

struct file {
    char path[FS_FILENAME_LEN];
    char filename[FS_FILENAME_LEN];  // filename used by FUSE filler
    int d_ino;                       // inode number
};

struct dirent {
    int n_dir;
    char path[FS_FILENAME_LEN];
    char dirname[FS_FILENAME_LEN];  // dirname used by FUSE filler
    int d_ino;                      // inode number
    int n_files;
    int files[N_FILES_DIR];
    int parent;
    int level;
};

struct inode {
    mode_t st_mode;      // protection
    nlink_t st_nlink;    // number of hard links
    uid_t st_uid;        // user ID of owner
    gid_t st_gid;        // group ID of owner
    off_t st_size;       // total size, in bytes
    int ref;             // reference to the first data block
    blkcnt_t st_blocks;  // number of blocks allocated
    time_t st_atime;     // time of last access
    time_t st_mtime;     // time of last modification
    time_t st_ctime;     // time of last status change
};

#endif //SISOP_2022B_G23_FISOPFS_H

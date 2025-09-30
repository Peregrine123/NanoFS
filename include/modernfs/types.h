#ifndef MODERNFS_TYPES_H
#define MODERNFS_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ============ 基础类型 ============

typedef uint32_t block_t;       // 块号
typedef uint32_t inode_t;       // Inode号

// ============ 文件系统常量 ============

#define BLOCK_SIZE          4096        // 块大小
#define BLOCK_SIZE_BITS     12          // log2(BLOCK_SIZE)

#define MAX_FILENAME        255         // 最大文件名长度
#define INODE_SIZE          128         // Inode大小

// ============ 磁盘布局常量 ============

#define SUPERBLOCK_BLOCK    0           // 超级块位置
#define SUPERBLOCK_MAGIC    0x4D4F4446  // "MODF" (ModernFS)

// ============ 超级块结构 ============

typedef struct superblock {
    uint32_t magic;                 // 魔数
    uint32_t version;               // 版本号
    uint32_t block_size;            // 块大小
    uint32_t total_blocks;          // 总块数
    uint32_t free_blocks;           // 空闲块数

    // 布局信息
    uint32_t journal_start;         // 日志起始块
    uint32_t journal_blocks;        // 日志块数
    uint32_t inode_bitmap_start;    // Inode位图起始块
    uint32_t inode_bitmap_blocks;   // Inode位图块数
    uint32_t data_bitmap_start;     // 数据位图起始块
    uint32_t data_bitmap_blocks;    // 数据位图块数
    uint32_t inode_table_start;     // Inode表起始块
    uint32_t inode_table_blocks;    // Inode表块数
    uint32_t data_start;            // 数据区起始块
    uint32_t data_blocks;           // 数据区块数

    uint32_t total_inodes;          // 总Inode数
    uint32_t free_inodes;           // 空闲Inode数

    uint64_t mount_time;            // 挂载时间
    uint64_t write_time;            // 最后写入时间
    uint32_t mount_count;           // 挂载次数

    uint8_t padding[4008];          // 填充到4096字节 (17*4 + 3*8 + 1*4 = 88, 4096-88=4008)
} __attribute__((packed)) superblock_t;

// ============ Inode类型 ============

#define INODE_TYPE_FILE     1
#define INODE_TYPE_DIR      2
#define INODE_TYPE_SYMLINK  3

// ============ 磁盘Inode结构 ============

#define INODE_DIRECT_BLOCKS     12      // 直接块数
#define INODE_INDIRECT_BLOCKS   1       // 一级间接块数
#define INODE_DOUBLE_INDIRECT   1       // 二级间接块数

typedef struct disk_inode {
    uint16_t mode;                  // 文件模式和权限
    uint16_t uid;                   // 用户ID
    uint16_t gid;                   // 组ID
    uint16_t nlink;                 // 硬链接数
    uint8_t  type;                  // 文件类型
    uint8_t  flags;                 // 标志位
    uint16_t reserved;

    uint64_t size;                  // 文件大小
    uint64_t blocks;                // 占用的块数

    uint64_t atime;                 // 访问时间
    uint64_t mtime;                 // 修改时间
    uint64_t ctime;                 // 创建时间

    // 数据块指针
    block_t  direct[INODE_DIRECT_BLOCKS];       // 直接块
    block_t  indirect;                          // 一级间接块
    block_t  double_indirect;                   // 二级间接块

    uint8_t  padding[20];           // 填充到128字节 (108 + 20 = 128)
} __attribute__((packed)) disk_inode_t;

// ============ 目录项结构 ============

typedef struct dirent {
    uint32_t inum;                  // Inode号
    uint16_t rec_len;               // 记录长度
    uint8_t  name_len;              // 文件名长度
    uint8_t  file_type;             // 文件类型
    char     name[MAX_FILENAME];    // 文件名
} __attribute__((packed)) dirent_t;

// ============ 错误码 ============

#define MODERNFS_SUCCESS        0
#define MODERNFS_ERROR         -1
#define MODERNFS_ENOSPC        -2   // 空间不足
#define MODERNFS_EINVAL        -3   // 无效参数
#define MODERNFS_EIO           -4   // IO错误
#define MODERNFS_ENOENT        -5   // 文件不存在

#endif // MODERNFS_TYPES_H
#include "modernfs/directory.h"
#include <string.h>
#include <errno.h>
#include <stdio.h>

// ============ 内部辅助函数 ============

// 计算目录项的实际大小（对齐到8字节）
static uint16_t dirent_size(const dirent_t *de) {
    uint16_t size = sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint8_t) +
                    sizeof(uint8_t) + de->name_len;
    // 8字节对齐
    return (size + 7) & ~7;
}

// 创建目录项
int dir_make_entry(inode_t inum,
                   const char *name,
                   uint8_t file_type,
                   dirent_t *dirent_out) {
    if (!name || !dirent_out) {
        return MODERNFS_EINVAL;
    }

    size_t name_len = strlen(name);
    if (name_len == 0 || name_len > MAX_FILENAME) {
        return MODERNFS_EINVAL;
    }

    memset(dirent_out, 0, sizeof(dirent_t));
    dirent_out->inum = inum;
    dirent_out->name_len = name_len;
    dirent_out->file_type = file_type;
    dirent_out->rec_len = dirent_size(dirent_out);
    memcpy(dirent_out->name, name, name_len);

    return MODERNFS_SUCCESS;
}

// ============ 目录查找 ============

int dir_lookup(inode_cache_t *cache,
               inode_t_mem *dir,
               const char *name,
               inode_t *inum_out) {
    if (!cache || !dir || !name || !inum_out) {
        return MODERNFS_EINVAL;
    }

    if (dir->disk.type != INODE_TYPE_DIR) {
        return MODERNFS_EINVAL;
    }

    size_t name_len = strlen(name);
    if (name_len == 0 || name_len > MAX_FILENAME) {
        return MODERNFS_EINVAL;
    }

    // 遍历目录块
    uint64_t offset = 0;
    uint8_t block_buf[BLOCK_SIZE];

    while (offset < dir->disk.size) {
        // 读取一个块
        ssize_t read = inode_read(cache, dir, block_buf, offset, BLOCK_SIZE);
        if (read < 0) {
            return read;
        }

        if (read == 0) {
            break;
        }

        // 解析目录项
        uint32_t pos = 0;
        while (pos < (uint32_t)read) {
            dirent_t *de = (dirent_t *)(block_buf + pos);

            // 检查有效性
            if (de->rec_len == 0 || de->rec_len > BLOCK_SIZE - pos) {
                break;
            }

            // 检查是否匹配
            if (de->inum != 0 &&
                de->name_len == name_len &&
                memcmp(de->name, name, name_len) == 0) {
                *inum_out = de->inum;
                return MODERNFS_SUCCESS;
            }

            pos += de->rec_len;
        }

        offset += BLOCK_SIZE;
    }

    return MODERNFS_ENOENT;
}

// ============ 目录添加 ============

int dir_add(inode_cache_t *cache,
            inode_t_mem *dir,
            const char *name,
            inode_t inum) {
    if (!cache || !dir || !name) {
        return MODERNFS_EINVAL;
    }

    if (dir->disk.type != INODE_TYPE_DIR) {
        return MODERNFS_EINVAL;
    }

    size_t name_len = strlen(name);
    if (name_len == 0 || name_len > MAX_FILENAME) {
        return MODERNFS_EINVAL;
    }

    // 检查是否已存在
    inode_t existing;
    if (dir_lookup(cache, dir, name, &existing) == MODERNFS_SUCCESS) {
        return MODERNFS_ERROR; // 文件已存在
    }

    // 创建新目录项
    dirent_t new_entry;
    inode_t_mem *target = inode_get(cache, inum);
    if (!target) {
        return MODERNFS_ERROR;
    }

    int ret = dir_make_entry(inum, name, target->disk.type, &new_entry);
    inode_put(cache, target);

    if (ret != MODERNFS_SUCCESS) {
        return ret;
    }

    // 查找空闲空间
    uint64_t offset = 0;
    uint8_t block_buf[BLOCK_SIZE];

    while (offset < dir->disk.size) {
        // 读取一个块
        ssize_t read = inode_read(cache, dir, block_buf, offset, BLOCK_SIZE);
        if (read < 0) {
            return read;
        }

        if (read == 0) {
            break;
        }

        // 查找空闲空间
        uint32_t pos = 0;
        while (pos < (uint32_t)read) {
            dirent_t *de = (dirent_t *)(block_buf + pos);

            if (de->rec_len == 0 || de->rec_len > BLOCK_SIZE - pos) {
                break;
            }

            // 检查是否有足够空间（删除的项或末尾空闲空间）
            uint16_t actual_size = dirent_size(de);
            uint16_t free_space = de->rec_len - actual_size;

            if (de->inum == 0) {
                // 已删除的项
                free_space = de->rec_len;
            }

            if (free_space >= new_entry.rec_len) {
                // 找到空闲空间
                uint32_t insert_pos = pos;
                if (de->inum != 0) {
                    // 分裂现有项 - 需要先写回修改后的原项
                    uint16_t old_rec_len = de->rec_len;
                    de->rec_len = actual_size;

                    // 写回修改后的原目录项
                    ssize_t written = inode_write(cache, dir, de,
                                                  offset + pos,
                                                  sizeof(dirent_t), NULL);
                    if (written < 0) {
                        return written;
                    }

                    insert_pos += actual_size;
                    new_entry.rec_len = old_rec_len - actual_size;
                } else {
                    new_entry.rec_len = free_space;
                }

                // 写入新项
                ssize_t written = inode_write(cache, dir, &new_entry,
                                              offset + insert_pos,
                                              sizeof(dirent_t), NULL);
                if (written < 0) {
                    return written;
                }

                return MODERNFS_SUCCESS;
            }

            pos += de->rec_len;
        }

        offset += BLOCK_SIZE;
    }

    // 没有找到空闲空间，追加到末尾
    new_entry.rec_len = BLOCK_SIZE; // 占据整个块的剩余空间

    ssize_t written = inode_write(cache, dir, &new_entry,
                                  dir->disk.size, sizeof(dirent_t), NULL);
    if (written < 0) {
        return written;
    }

    return MODERNFS_SUCCESS;
}

// ============ 目录删除 ============

int dir_remove(inode_cache_t *cache,
               inode_t_mem *dir,
               const char *name) {
    if (!cache || !dir || !name) {
        return MODERNFS_EINVAL;
    }

    if (dir->disk.type != INODE_TYPE_DIR) {
        return MODERNFS_EINVAL;
    }

    size_t name_len = strlen(name);
    if (name_len == 0 || name_len > MAX_FILENAME) {
        return MODERNFS_EINVAL;
    }

    // 遍历目录块
    uint64_t offset = 0;
    uint8_t block_buf[BLOCK_SIZE];

    while (offset < dir->disk.size) {
        // 读取一个块
        ssize_t read = inode_read(cache, dir, block_buf, offset, BLOCK_SIZE);
        if (read < 0) {
            return read;
        }

        if (read == 0) {
            break;
        }

        // 解析目录项
        uint32_t pos = 0;
        dirent_t *prev = NULL;

        while (pos < (uint32_t)read) {
            dirent_t *de = (dirent_t *)(block_buf + pos);

            if (de->rec_len == 0 || de->rec_len > BLOCK_SIZE - pos) {
                break;
            }

            // 检查是否匹配
            if (de->inum != 0 &&
                de->name_len == name_len &&
                memcmp(de->name, name, name_len) == 0) {

                // 找到了，标记为删除
                if (prev) {
                    // 合并到前一个项
                    prev->rec_len += de->rec_len;

                    // 写回前一个项
                    uint32_t prev_pos = (uint8_t *)prev - block_buf;
                    ssize_t written = inode_write(cache, dir, prev,
                                                  offset + prev_pos,
                                                  sizeof(dirent_t), NULL);
                    if (written < 0) {
                        return written;
                    }
                } else {
                    // 第一个项，只清除inum
                    de->inum = 0;

                    ssize_t written = inode_write(cache, dir, de,
                                                  offset + pos,
                                                  sizeof(dirent_t), NULL);
                    if (written < 0) {
                        return written;
                    }
                }

                return MODERNFS_SUCCESS;
            }

            if (de->inum != 0) {
                prev = de;
            }

            pos += de->rec_len;
        }

        offset += BLOCK_SIZE;
    }

    return MODERNFS_ENOENT;
}

// ============ 目录遍历 ============

int dir_iterate(inode_cache_t *cache,
                inode_t_mem *dir,
                dir_iter_callback_t callback,
                void *arg) {
    if (!cache || !dir || !callback) {
        return MODERNFS_EINVAL;
    }

    if (dir->disk.type != INODE_TYPE_DIR) {
        return MODERNFS_EINVAL;
    }

    // 遍历目录块
    uint64_t offset = 0;
    uint8_t block_buf[BLOCK_SIZE];

    while (offset < dir->disk.size) {
        // 读取一个块
        ssize_t read = inode_read(cache, dir, block_buf, offset, BLOCK_SIZE);
        if (read < 0) {
            return read;
        }

        if (read == 0) {
            break;
        }

        // 解析目录项
        uint32_t pos = 0;
        while (pos < (uint32_t)read) {
            dirent_t *de = (dirent_t *)(block_buf + pos);

            if (de->rec_len == 0 || de->rec_len > BLOCK_SIZE - pos) {
                break;
            }

            // 调用回调
            if (de->inum != 0) {
                char name_buf[MAX_FILENAME + 1];
                memcpy(name_buf, de->name, de->name_len);
                name_buf[de->name_len] = '\0';

                int ret = callback(name_buf, de->inum, arg);
                if (ret != 0) {
                    return ret;
                }
            }

            pos += de->rec_len;
        }

        offset += BLOCK_SIZE;
    }

    return MODERNFS_SUCCESS;
}

// ============ 检查目录是否为空 ============

static int count_entries_callback(const char *name, inode_t inum, void *arg) {
    (void)inum;
    int *count = (int *)arg;

    // 跳过 "." 和 ".."
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return 0;
    }

    (*count)++;
    return 0;
}

int dir_is_empty(inode_cache_t *cache, inode_t_mem *dir) {
    if (!cache || !dir) {
        return MODERNFS_EINVAL;
    }

    if (dir->disk.type != INODE_TYPE_DIR) {
        return MODERNFS_EINVAL;
    }

    int count = 0;
    int ret = dir_iterate(cache, dir, count_entries_callback, &count);
    if (ret < 0) {
        return ret;
    }

    return count == 0 ? 1 : 0;
}
#define _GNU_SOURCE
#define FUSE_USE_VERSION 31

#include "modernfs/fuse_ops.h"
#include "modernfs/fs_context.h"
#include "modernfs/path.h"
#include "modernfs/directory.h"
#include "modernfs/inode.h"
#include "modernfs/rust_ffi.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>

// 获取文件系统上下文
#define GET_CTX() ((fs_context_t *)fuse_get_context()->private_data)

// ============ 初始化和销毁 ============

void* modernfs_init(struct fuse_conn_info *conn, struct fuse_config *cfg) {
    (void)conn;

    // 配置FUSE选项
    cfg->use_ino = 1;              // 使用我们的Inode号
    cfg->entry_timeout = 1.0;      // 目录项缓存超时
    cfg->attr_timeout = 1.0;       // 属性缓存超时
    cfg->negative_timeout = 0.0;   // 禁用负缓存

    fs_context_t *ctx = GET_CTX();
    printf("ModernFS mounted: root_inum=%u\n", ctx->root_inum);

    return ctx;
}

void modernfs_destroy(void *private_data) {
    fs_context_t *ctx = (fs_context_t *)private_data;
    if (ctx) {
        fs_context_destroy(ctx);
    }
}

// ============ 辅助函数 ============

static void inode_to_stat(inode_t_mem *inode, struct stat *stbuf) {
    memset(stbuf, 0, sizeof(struct stat));

    stbuf->st_ino = inode->inum;
    stbuf->st_mode = (inode->disk.type == INODE_TYPE_DIR ? S_IFDIR : S_IFREG) | inode->disk.mode;
    stbuf->st_nlink = inode->disk.nlink;
    stbuf->st_uid = inode->disk.uid;
    stbuf->st_gid = inode->disk.gid;
    stbuf->st_size = inode->disk.size;
    stbuf->st_blocks = inode->disk.blocks;
    stbuf->st_blksize = BLOCK_SIZE;
    stbuf->st_atime = inode->disk.atime;
    stbuf->st_mtime = inode->disk.mtime;
    stbuf->st_ctime = inode->disk.ctime;
}

// ============ 文件属性 ============

int modernfs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void)fi;
    fs_context_t *ctx = GET_CTX();

    // 解析路径
    inode_t_mem *inode = path_resolve(ctx->icache, ctx->root_inum, ctx->root_inum, path, false);
    if (!inode) {
        return -ENOENT;
    }

    // 填充stat结构
    inode_lock(inode);
    inode_to_stat(inode, stbuf);
    inode_unlock(inode);

    inode_put(ctx->icache, inode);
    return 0;
}

// ============ 目录操作 ============

int modernfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                     off_t offset, struct fuse_file_info *fi,
                     enum fuse_readdir_flags flags) {
    (void)offset;
    (void)fi;
    (void)flags;

    fs_context_t *ctx = GET_CTX();

    // 解析路径
    inode_t_mem *dir_inode = path_resolve(ctx->icache, ctx->root_inum, ctx->root_inum, path, false);
    if (!dir_inode) {
        return -ENOENT;
    }

    inode_lock(dir_inode);

    // 检查是否是目录
    if (dir_inode->disk.type != INODE_TYPE_DIR) {
        inode_unlock(dir_inode);
        inode_put(ctx->icache, dir_inode);
        return -ENOTDIR;
    }

    // 遍历目录项
    struct readdir_ctx {
        void *buf;
        fuse_fill_dir_t filler;
    } readdir_ctx = { .buf = buf, .filler = filler };

    int iterate_callback(const char *name, inode_t inum, void *arg) {
        struct readdir_ctx *rctx = (struct readdir_ctx *)arg;
        struct stat st = {0};
        st.st_ino = inum;
        st.st_mode = S_IFDIR;  // 简化处理，稍后可以优化

        if (rctx->filler(rctx->buf, name, &st, 0, 0) != 0) {
            return -1;  // 缓冲区满
        }
        return 0;
    }

    int ret = dir_iterate(ctx->icache, dir_inode, iterate_callback, &readdir_ctx);

    inode_unlock(dir_inode);
    inode_put(ctx->icache, dir_inode);

    return ret < 0 ? ret : 0;
}

int modernfs_mkdir(const char *path, mode_t mode) {
    fs_context_t *ctx = GET_CTX();

    if (ctx->read_only) {
        return -EROFS;
    }

    // 解析父目录
    inode_t_mem *parent_inode;
    char filename[256];

    if (path_resolve_parent(ctx->icache, ctx->root_inum, ctx->root_inum, path, &parent_inode, filename) < 0) {
        return -ENOENT;
    }

    inode_lock(parent_inode);

    // 检查父节点是否是目录
    if (parent_inode->disk.type != INODE_TYPE_DIR) {
        inode_unlock(parent_inode);
        inode_put(ctx->icache, parent_inode);
        return -ENOTDIR;
    }

    // 检查是否已存在
    inode_t existing_inum;
    if (dir_lookup(ctx->icache, parent_inode, filename, &existing_inum) == 0) {
        inode_unlock(parent_inode);
        inode_put(ctx->icache, parent_inode);
        return -EEXIST;
    }

    // 分配新Inode
    inode_t_mem *new_inode = inode_alloc(ctx->icache, INODE_TYPE_DIR);
    if (!new_inode) {
        inode_unlock(parent_inode);
        inode_put(ctx->icache, parent_inode);
        return -ENOSPC;
    }

    inode_lock(new_inode);
    new_inode->disk.mode = mode & 0777;
    new_inode->disk.nlink = 2;  // . 和 ..
    new_inode->disk.uid = fuse_get_context()->uid;
    new_inode->disk.gid = fuse_get_context()->gid;

    // 添加 . 和 .. 目录项
    if (dir_add(ctx->icache, new_inode, ".", new_inode->inum) < 0 ||
        dir_add(ctx->icache, new_inode, "..", parent_inode->inum) < 0) {
        inode_unlock(new_inode);
        inode_free(ctx->icache, new_inode);
        inode_unlock(parent_inode);
        inode_put(ctx->icache, parent_inode);
        return -EIO;
    }

    new_inode->dirty = true;
    inode_unlock(new_inode);

    // 添加到父目录
    int ret = dir_add(ctx->icache, parent_inode, filename, new_inode->inum);
    if (ret < 0) {
        inode_free(ctx->icache, new_inode);
        inode_unlock(parent_inode);
        inode_put(ctx->icache, parent_inode);
        return ret;
    }

    parent_inode->disk.nlink++;  // 子目录的 .. 指向父目录
    parent_inode->dirty = true;

    inode_unlock(parent_inode);
    inode_put(ctx->icache, parent_inode);
    inode_put(ctx->icache, new_inode);

    return 0;
}

int modernfs_rmdir(const char *path) {
    fs_context_t *ctx = GET_CTX();

    if (ctx->read_only) {
        return -EROFS;
    }

    // 解析父目录
    inode_t_mem *parent_inode;
    char filename[256];

    if (path_resolve_parent(ctx->icache, ctx->root_inum, ctx->root_inum, path, &parent_inode, filename) < 0) {
        return -ENOENT;
    }

    inode_lock(parent_inode);

    // 查找目标目录
    inode_t target_inum;
    if (dir_lookup(ctx->icache, parent_inode, filename, &target_inum) < 0) {
        inode_unlock(parent_inode);
        inode_put(ctx->icache, parent_inode);
        return -ENOENT;
    }

    inode_t_mem *target_inode = inode_get(ctx->icache, target_inum);
    if (!target_inode) {
        inode_unlock(parent_inode);
        inode_put(ctx->icache, parent_inode);
        return -ENOENT;
    }

    inode_lock(target_inode);

    // 检查是否是目录
    if (target_inode->disk.type != INODE_TYPE_DIR) {
        inode_unlock(target_inode);
        inode_put(ctx->icache, target_inode);
        inode_unlock(parent_inode);
        inode_put(ctx->icache, parent_inode);
        return -ENOTDIR;
    }

    // 检查是否为空 (只包含 . 和 ..)
    if (target_inode->disk.nlink > 2) {
        inode_unlock(target_inode);
        inode_put(ctx->icache, target_inode);
        inode_unlock(parent_inode);
        inode_put(ctx->icache, parent_inode);
        return -ENOTEMPTY;
    }

    inode_unlock(target_inode);

    // 从父目录删除
    if (dir_remove(ctx->icache, parent_inode, filename) < 0) {
        inode_put(ctx->icache, target_inode);
        inode_unlock(parent_inode);
        inode_put(ctx->icache, parent_inode);
        return -EIO;
    }

    parent_inode->disk.nlink--;
    parent_inode->dirty = true;

    // 释放Inode
    inode_free(ctx->icache, target_inode);

    inode_unlock(parent_inode);
    inode_put(ctx->icache, parent_inode);

    return 0;
}

// ============ 文件操作 ============

int modernfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    fs_context_t *ctx = GET_CTX();

    if (ctx->read_only) {
        return -EROFS;
    }

    // 解析父目录
    inode_t_mem *parent_inode;
    char filename[256];

    if (path_resolve_parent(ctx->icache, ctx->root_inum, ctx->root_inum, path, &parent_inode, filename) < 0) {
        return -ENOENT;
    }

    inode_lock(parent_inode);

    // 检查是否已存在
    inode_t existing_inum;
    if (dir_lookup(ctx->icache, parent_inode, filename, &existing_inum) == 0) {
        inode_unlock(parent_inode);
        inode_put(ctx->icache, parent_inode);
        return -EEXIST;
    }

    // 分配新Inode
    inode_t_mem *new_inode = inode_alloc(ctx->icache, INODE_TYPE_FILE);
    if (!new_inode) {
        inode_unlock(parent_inode);
        inode_put(ctx->icache, parent_inode);
        return -ENOSPC;
    }

    inode_lock(new_inode);
    new_inode->disk.mode = mode & 0777;
    new_inode->disk.nlink = 1;
    new_inode->disk.uid = fuse_get_context()->uid;
    new_inode->disk.gid = fuse_get_context()->gid;
    new_inode->dirty = true;

    // 设置文件句柄为Inode号，供后续write使用
    fi->fh = new_inode->inum;

    inode_unlock(new_inode);

    // 添加到父目录
    int ret = dir_add(ctx->icache, parent_inode, filename, new_inode->inum);
    if (ret < 0) {
        inode_free(ctx->icache, new_inode);
        inode_unlock(parent_inode);
        inode_put(ctx->icache, parent_inode);
        return ret;
    }

    parent_inode->dirty = true;

    inode_unlock(parent_inode);
    inode_put(ctx->icache, parent_inode);
    inode_put(ctx->icache, new_inode);

    return 0;
}

int modernfs_open(const char *path, struct fuse_file_info *fi) {
    fs_context_t *ctx = GET_CTX();

    // 解析路径
    inode_t_mem *inode = path_resolve(ctx->icache, ctx->root_inum, ctx->root_inum, path, false);
    if (!inode) {
        return -ENOENT;
    }

    inode_lock(inode);

    // 检查是否是文件
    if (inode->disk.type != INODE_TYPE_FILE) {
        inode_unlock(inode);
        inode_put(ctx->icache, inode);
        return -EISDIR;
    }

    // 保存Inode号到文件句柄
    fi->fh = inode->inum;

    inode_unlock(inode);
    inode_put(ctx->icache, inode);

    return 0;
}

int modernfs_read(const char *path, char *buf, size_t size, off_t offset,
                  struct fuse_file_info *fi) {
    (void)path;
    fs_context_t *ctx = GET_CTX();

    // 从文件句柄获取Inode号
    inode_t inum = fi->fh;

    inode_t_mem *inode = inode_get(ctx->icache, inum);
    if (!inode) {
        return -ENOENT;
    }

    inode_lock(inode);

    // 读取数据
    ssize_t bytes_read = inode_read(ctx->icache, inode, (uint8_t *)buf, offset, size);

    // 更新访问时间
    inode->disk.atime = time(NULL);
    inode->dirty = true;

    // 同步inode到磁盘（可选：访问时间更新的同步可以延迟）
    // inode_sync(ctx->icache, inode);

    inode_unlock(inode);
    inode_put(ctx->icache, inode);

    ctx->read_count++;

    return bytes_read;
}

int modernfs_write(const char *path, const char *buf, size_t size,
                   off_t offset, struct fuse_file_info *fi) {
    (void)path;
    fs_context_t *ctx = GET_CTX();

    if (ctx->read_only) {
        return -EROFS;
    }

    // 从文件句柄获取Inode号
    inode_t inum = fi->fh;

    inode_t_mem *inode = inode_get(ctx->icache, inum);
    if (!inode) {
        return -ENOENT;
    }

    inode_lock(inode);

    // Week 7: 如果有Journal，开始事务
    RustTransaction *txn = NULL;
    if (ctx->journal) {
        txn = rust_journal_begin(ctx->journal);
        if (!txn) {
            fprintf(stderr, "modernfs_write: failed to begin transaction\n");
            inode_unlock(inode);
            inode_put(ctx->icache, inode);
            return -EIO;
        }
    }

    // 写入数据 (传递txn指针)
    ssize_t bytes_written = inode_write(ctx->icache, inode, (const uint8_t *)buf, offset, size, txn);

    if (bytes_written < 0) {
        // 写入失败，中止事务
        if (txn) {
            rust_journal_abort(txn);
        }
        inode_unlock(inode);
        inode_put(ctx->icache, inode);
        return bytes_written;
    }

    // 更新修改时间
    inode->disk.mtime = time(NULL);
    inode->disk.ctime = inode->disk.mtime;
    inode->dirty = true;

    // 同步inode到磁盘（确保文件大小等元数据持久化）
    inode_sync(ctx->icache, inode);

    // Week 7: 提交事务
    if (txn) {
        if (rust_journal_commit(ctx->journal, txn) < 0) {
            fprintf(stderr, "modernfs_write: failed to commit transaction\n");
            inode_unlock(inode);
            inode_put(ctx->icache, inode);
            return -EIO;
        }

        // 立即执行checkpoint，将Journal数据写入最终位置
        // TODO: 未来可以改为异步checkpoint以提升性能
        if (rust_journal_checkpoint(ctx->journal) < 0) {
            fprintf(stderr, "modernfs_write: failed to checkpoint journal\n");
            // checkpoint失败不影响写入成功（数据在Journal中是安全的）
        }
    }

    inode_unlock(inode);
    inode_put(ctx->icache, inode);

    ctx->write_count++;

    return bytes_written;
}

int modernfs_unlink(const char *path) {
    fs_context_t *ctx = GET_CTX();

    if (ctx->read_only) {
        return -EROFS;
    }

    // 解析父目录
    inode_t_mem *parent_inode;
    char filename[256];

    if (path_resolve_parent(ctx->icache, ctx->root_inum, ctx->root_inum, path, &parent_inode, filename) < 0) {
        return -ENOENT;
    }

    inode_lock(parent_inode);

    // 查找目标文件
    inode_t target_inum;
    if (dir_lookup(ctx->icache, parent_inode, filename, &target_inum) < 0) {
        inode_unlock(parent_inode);
        inode_put(ctx->icache, parent_inode);
        return -ENOENT;
    }

    inode_t_mem *target_inode = inode_get(ctx->icache, target_inum);
    if (!target_inode) {
        inode_unlock(parent_inode);
        inode_put(ctx->icache, parent_inode);
        return -ENOENT;
    }

    inode_lock(target_inode);

    // 检查是否是文件
    if (target_inode->disk.type == INODE_TYPE_DIR) {
        inode_unlock(target_inode);
        inode_put(ctx->icache, target_inode);
        inode_unlock(parent_inode);
        inode_put(ctx->icache, parent_inode);
        return -EISDIR;
    }

    inode_unlock(target_inode);

    // 从父目录删除
    if (dir_remove(ctx->icache, parent_inode, filename) < 0) {
        inode_put(ctx->icache, target_inode);
        inode_unlock(parent_inode);
        inode_put(ctx->icache, parent_inode);
        return -EIO;
    }

    parent_inode->dirty = true;

    // 减少链接数，如果为0则释放Inode
    inode_free(ctx->icache, target_inode);

    inode_unlock(parent_inode);
    inode_put(ctx->icache, parent_inode);

    return 0;
}

int modernfs_release(const char *path, struct fuse_file_info *fi) {
    (void)path;
    (void)fi;
    // 不需要特殊处理，Inode缓存会自动管理
    return 0;
}

int modernfs_truncate(const char *path, off_t size, struct fuse_file_info *fi) {
    (void)fi;
    fs_context_t *ctx = GET_CTX();

    if (ctx->read_only) {
        return -EROFS;
    }

    // 解析路径
    inode_t_mem *inode = path_resolve(ctx->icache, ctx->root_inum, ctx->root_inum, path, false);
    if (!inode) {
        return -ENOENT;
    }

    inode_lock(inode);

    // 检查是否是文件
    if (inode->disk.type != INODE_TYPE_FILE) {
        inode_unlock(inode);
        inode_put(ctx->icache, inode);
        return -EISDIR;
    }

    // 截断文件
    int ret = inode_truncate(ctx->icache, inode, size);

    if (ret == 0) {
        inode->disk.mtime = time(NULL);
        inode->disk.ctime = inode->disk.mtime;
        inode->dirty = true;
    }

    inode_unlock(inode);
    inode_put(ctx->icache, inode);

    return ret;
}

// ============ 元数据操作 ============

int modernfs_statfs(const char *path, struct statvfs *stbuf) {
    (void)path;
    fs_context_t *ctx = GET_CTX();

    memset(stbuf, 0, sizeof(struct statvfs));

    uint64_t total_blocks, free_blocks, total_inodes, free_inodes;
    fs_context_statfs(ctx, &total_blocks, &free_blocks, &total_inodes, &free_inodes);

    stbuf->f_bsize = BLOCK_SIZE;
    stbuf->f_frsize = BLOCK_SIZE;
    stbuf->f_blocks = total_blocks;
    stbuf->f_bfree = free_blocks;
    stbuf->f_bavail = free_blocks;
    stbuf->f_files = total_inodes;
    stbuf->f_ffree = free_inodes;
    stbuf->f_favail = free_inodes;
    stbuf->f_namemax = 255;

    return 0;
}

int modernfs_utimens(const char *path, const struct timespec tv[2],
                     struct fuse_file_info *fi) {
    (void)fi;
    fs_context_t *ctx = GET_CTX();

    if (ctx->read_only) {
        return -EROFS;
    }

    // 解析路径
    inode_t_mem *inode = path_resolve(ctx->icache, ctx->root_inum, ctx->root_inum, path, false);
    if (!inode) {
        return -ENOENT;
    }

    inode_lock(inode);

    if (tv) {
        inode->disk.atime = tv[0].tv_sec;
        inode->disk.mtime = tv[1].tv_sec;
    } else {
        time_t now = time(NULL);
        inode->disk.atime = now;
        inode->disk.mtime = now;
    }

    inode->disk.ctime = time(NULL);
    inode->dirty = true;

    inode_unlock(inode);
    inode_put(ctx->icache, inode);

    return 0;
}

int modernfs_chmod(const char *path, mode_t mode, struct fuse_file_info *fi) {
    (void)fi;
    fs_context_t *ctx = GET_CTX();

    if (ctx->read_only) {
        return -EROFS;
    }

    // 解析路径
    inode_t_mem *inode = path_resolve(ctx->icache, ctx->root_inum, ctx->root_inum, path, false);
    if (!inode) {
        return -ENOENT;
    }

    inode_lock(inode);

    inode->disk.mode = mode & 0777;
    inode->disk.ctime = time(NULL);
    inode->dirty = true;

    inode_unlock(inode);
    inode_put(ctx->icache, inode);

    return 0;
}

int modernfs_chown(const char *path, uid_t uid, gid_t gid,
                   struct fuse_file_info *fi) {
    (void)fi;
    fs_context_t *ctx = GET_CTX();

    if (ctx->read_only) {
        return -EROFS;
    }

    // 解析路径
    inode_t_mem *inode = path_resolve(ctx->icache, ctx->root_inum, ctx->root_inum, path, false);
    if (!inode) {
        return -ENOENT;
    }

    inode_lock(inode);

    if (uid != (uid_t)-1) {
        inode->disk.uid = uid;
    }
    if (gid != (gid_t)-1) {
        inode->disk.gid = gid;
    }

    inode->disk.ctime = time(NULL);
    inode->dirty = true;

    inode_unlock(inode);
    inode_put(ctx->icache, inode);

    return 0;
}

int modernfs_fsync(const char *path, int datasync, struct fuse_file_info *fi) {
    (void)path;
    (void)datasync;
    (void)fi;

    fs_context_t *ctx = GET_CTX();
    return fs_context_sync(ctx);
}

// ============ 操作表初始化 ============

void modernfs_init_ops(struct fuse_operations *ops) {
    memset(ops, 0, sizeof(struct fuse_operations));

    ops->init = modernfs_init;
    ops->destroy = modernfs_destroy;
    ops->getattr = modernfs_getattr;
    ops->readdir = modernfs_readdir;
    ops->mkdir = modernfs_mkdir;
    ops->rmdir = modernfs_rmdir;
    ops->create = modernfs_create;
    ops->open = modernfs_open;
    ops->read = modernfs_read;
    ops->write = modernfs_write;
    ops->unlink = modernfs_unlink;
    ops->release = modernfs_release;
    ops->truncate = modernfs_truncate;
    ops->statfs = modernfs_statfs;
    ops->utimens = modernfs_utimens;
    ops->chmod = modernfs_chmod;
    ops->chown = modernfs_chown;
    ops->fsync = modernfs_fsync;
}

#ifndef MODERNFS_FUSE_OPS_H
#define MODERNFS_FUSE_OPS_H

#define FUSE_USE_VERSION 31
#include <fuse.h>

/**
 * ModernFS FUSE操作接口
 * 实现标准POSIX文件系统操作
 */

// 初始化和销毁
void* modernfs_init(struct fuse_conn_info *conn, struct fuse_config *cfg);
void modernfs_destroy(void *private_data);

// 文件属性
int modernfs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi);

// 目录操作
int modernfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                     off_t offset, struct fuse_file_info *fi,
                     enum fuse_readdir_flags flags);
int modernfs_mkdir(const char *path, mode_t mode);
int modernfs_rmdir(const char *path);

// 文件操作
int modernfs_create(const char *path, mode_t mode, struct fuse_file_info *fi);
int modernfs_open(const char *path, struct fuse_file_info *fi);
int modernfs_read(const char *path, char *buf, size_t size, off_t offset,
                  struct fuse_file_info *fi);
int modernfs_write(const char *path, const char *buf, size_t size,
                   off_t offset, struct fuse_file_info *fi);
int modernfs_unlink(const char *path);
int modernfs_release(const char *path, struct fuse_file_info *fi);
int modernfs_truncate(const char *path, off_t size, struct fuse_file_info *fi);

// 元数据操作
int modernfs_statfs(const char *path, struct statvfs *stbuf);
int modernfs_utimens(const char *path, const struct timespec tv[2],
                     struct fuse_file_info *fi);
int modernfs_chmod(const char *path, mode_t mode, struct fuse_file_info *fi);
int modernfs_chown(const char *path, uid_t uid, gid_t gid,
                   struct fuse_file_info *fi);

// 同步操作
int modernfs_fsync(const char *path, int datasync, struct fuse_file_info *fi);

/**
 * 获取FUSE操作表
 */
void modernfs_init_ops(struct fuse_operations *ops);

#endif // MODERNFS_FUSE_OPS_H
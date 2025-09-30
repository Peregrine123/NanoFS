#ifndef MODERNFS_DIRECTORY_H
#define MODERNFS_DIRECTORY_H

#include "modernfs/types.h"
#include "modernfs/inode.h"

// ============ 目录项操作 ============

/**
 * 在目录中查找文件
 * @param cache Inode缓存
 * @param dir 目录Inode
 * @param name 文件名
 * @param inum_out 输出文件的Inode号
 * @return 成功返回0，失败返回负数错误码
 */
int dir_lookup(inode_cache_t *cache,
               inode_t_mem *dir,
               const char *name,
               inode_t *inum_out);

/**
 * 在目录中添加文件
 * @param cache Inode缓存
 * @param dir 目录Inode
 * @param name 文件名
 * @param inum 文件Inode号
 * @return 成功返回0，失败返回负数错误码
 */
int dir_add(inode_cache_t *cache,
            inode_t_mem *dir,
            const char *name,
            inode_t inum);

/**
 * 从目录中删除文件
 * @param cache Inode缓存
 * @param dir 目录Inode
 * @param name 文件名
 * @return 成功返回0，失败返回负数错误码
 */
int dir_remove(inode_cache_t *cache,
               inode_t_mem *dir,
               const char *name);

/**
 * 遍历目录
 * @param cache Inode缓存
 * @param dir 目录Inode
 * @param callback 回调函数
 * @param arg 回调参数
 * @return 成功返回0，失败返回负数错误码
 */
typedef int (*dir_iter_callback_t)(const char *name, inode_t inum, void *arg);

int dir_iterate(inode_cache_t *cache,
                inode_t_mem *dir,
                dir_iter_callback_t callback,
                void *arg);

/**
 * 检查目录是否为空
 * @param cache Inode缓存
 * @param dir 目录Inode
 * @return 1表示空，0表示非空，负数表示错误
 */
int dir_is_empty(inode_cache_t *cache, inode_t_mem *dir);

/**
 * 创建目录项的辅助函数
 * @param inum Inode号
 * @param name 文件名
 * @param file_type 文件类型
 * @param dirent_out 输出目录项
 * @return 成功返回0，失败返回负数错误码
 */
int dir_make_entry(inode_t inum,
                   const char *name,
                   uint8_t file_type,
                   dirent_t *dirent_out);

#endif // MODERNFS_DIRECTORY_H
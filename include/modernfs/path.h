#ifndef MODERNFS_PATH_H
#define MODERNFS_PATH_H

#include "modernfs/types.h"
#include "modernfs/inode.h"
#include "modernfs/directory.h"

// ============ 路径解析 ============

/**
 * 解析路径，返回对应的Inode
 * @param cache Inode缓存
 * @param root 根目录Inode号（通常是1）
 * @param cwd 当前工作目录Inode号（用于相对路径）
 * @param path 路径字符串
 * @param follow_symlink 是否跟随符号链接
 * @return 成功返回Inode指针（需要调用inode_put释放），失败返回NULL
 */
inode_t_mem *path_resolve(inode_cache_t *cache,
                          inode_t root,
                          inode_t cwd,
                          const char *path,
                          bool follow_symlink);

/**
 * 解析父目录和文件名
 * @param cache Inode缓存
 * @param root 根目录Inode号
 * @param cwd 当前工作目录Inode号
 * @param path 路径字符串
 * @param parent_out 输出父目录Inode（需要调用inode_put释放）
 * @param name_out 输出文件名（调用者需要提供MAX_FILENAME+1大小的缓冲区）
 * @return 成功返回0，失败返回负数错误码
 */
int path_resolve_parent(inode_cache_t *cache,
                        inode_t root,
                        inode_t cwd,
                        const char *path,
                        inode_t_mem **parent_out,
                        char *name_out);

/**
 * 规范化路径（去除 ".", "..", 多余的 "/"）
 * @param path 输入路径
 * @param normalized_out 输出规范化路径（调用者需要提供足够大小的缓冲区）
 * @param size 缓冲区大小
 * @return 成功返回0，失败返回负数错误码
 */
int path_normalize(const char *path, char *normalized_out, size_t size);

/**
 * 提取路径中的文件名部分
 * @param path 路径字符串
 * @return 文件名指针（指向path中的位置）
 */
const char *path_basename(const char *path);

/**
 * 提取路径中的目录部分
 * @param path 路径字符串
 * @param dir_out 输出目录路径（调用者需要提供足够大小的缓冲区）
 * @param size 缓冲区大小
 * @return 成功返回0，失败返回负数错误码
 */
int path_dirname(const char *path, char *dir_out, size_t size);

#endif // MODERNFS_PATH_H
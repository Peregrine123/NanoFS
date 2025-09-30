#include "modernfs/path.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>

// ============ 辅助函数 ============

// 跳过多余的斜杠
static const char *skip_slashes(const char *p) {
    while (*p == '/') {
        p++;
    }
    return p;
}

// 提取下一个路径组件
static int get_next_component(const char **path, char *component, size_t size) {
    const char *p = skip_slashes(*path);

    if (*p == '\0') {
        return 0; // 路径结束
    }

    size_t len = 0;
    while (*p && *p != '/' && len < size - 1) {
        component[len++] = *p++;
    }
    component[len] = '\0';

    *path = p;

    return len > 0 ? 1 : 0;
}

// ============ 路径规范化 ============

int path_normalize(const char *path, char *normalized_out, size_t size) {
    if (!path || !normalized_out || size == 0) {
        return MODERNFS_EINVAL;
    }

    // 临时缓冲区用于存储路径组件
    char *components[256];
    int count = 0;

    const char *p = path;
    int is_absolute = (*p == '/');

    char component[MAX_FILENAME + 1];

    while (get_next_component(&p, component, sizeof(component))) {
        if (strcmp(component, ".") == 0) {
            // 跳过 "."
            continue;
        } else if (strcmp(component, "..") == 0) {
            // 返回上级目录
            if (count > 0) {
                free(components[--count]);
            }
        } else {
            // 正常组件
            if (count >= 256) {
                // 清理已分配的内存
                for (int i = 0; i < count; i++) {
                    free(components[i]);
                }
                return MODERNFS_EINVAL;
            }

            components[count] = strdup(component);
            if (!components[count]) {
                // 清理已分配的内存
                for (int i = 0; i < count; i++) {
                    free(components[i]);
                }
                return MODERNFS_ERROR;
            }
            count++;
        }
    }

    // 构建规范化路径
    size_t pos = 0;

    if (is_absolute) {
        if (pos + 1 >= size) {
            for (int i = 0; i < count; i++) {
                free(components[i]);
            }
            return MODERNFS_EINVAL;
        }
        normalized_out[pos++] = '/';
    }

    for (int i = 0; i < count; i++) {
        size_t comp_len = strlen(components[i]);

        if (pos + comp_len + (i < count - 1 ? 1 : 0) >= size) {
            for (int j = 0; j < count; j++) {
                free(components[j]);
            }
            return MODERNFS_EINVAL;
        }

        memcpy(normalized_out + pos, components[i], comp_len);
        pos += comp_len;

        if (i < count - 1) {
            normalized_out[pos++] = '/';
        }

        free(components[i]);
    }

    // 如果是绝对路径且只有根目录
    if (is_absolute && count == 0) {
        // 已经写入了 '/'
    } else if (count == 0) {
        // 相对路径且为空，返回 "."
        if (pos + 1 >= size) {
            return MODERNFS_EINVAL;
        }
        normalized_out[pos++] = '.';
    }

    normalized_out[pos] = '\0';

    return MODERNFS_SUCCESS;
}

// ============ 路径解析 ============

inode_t_mem *path_resolve(inode_cache_t *cache,
                          inode_t root,
                          inode_t cwd,
                          const char *path,
                          bool follow_symlink) {
    if (!cache || !path) {
        return NULL;
    }

    const char *p = path;
    int is_absolute = (*p == '/');

    // 从根目录或当前目录开始
    inode_t_mem *current = inode_get(cache, is_absolute ? root : cwd);
    if (!current) {
        return NULL;
    }

    char component[MAX_FILENAME + 1];

    while (get_next_component(&p, component, sizeof(component))) {
        // 检查当前是否为目录
        if (current->disk.type != INODE_TYPE_DIR) {
            inode_put(cache, current);
            errno = ENOTDIR;
            return NULL;
        }

        // 在目录中查找组件
        inode_t next_inum;
        int ret = dir_lookup(cache, current, component, &next_inum);

        if (ret != MODERNFS_SUCCESS) {
            inode_put(cache, current);
            errno = ENOENT;
            return NULL;
        }

        // 释放当前Inode
        inode_put(cache, current);

        // 获取下一个Inode
        current = inode_get(cache, next_inum);
        if (!current) {
            return NULL;
        }

        // 如果是符号链接且需要跟随
        if (follow_symlink && current->disk.type == INODE_TYPE_SYMLINK) {
            // 读取符号链接目标
            char link_target[4096];
            ssize_t link_len = inode_read(cache, current, link_target, 0, sizeof(link_target) - 1);

            if (link_len < 0) {
                inode_put(cache, current);
                return NULL;
            }

            link_target[link_len] = '\0';

            // 递归解析符号链接
            inode_put(cache, current);
            current = path_resolve(cache, root, cwd, link_target, follow_symlink);

            if (!current) {
                return NULL;
            }
        }
    }

    return current;
}

// ============ 解析父目录 ============

int path_resolve_parent(inode_cache_t *cache,
                        inode_t root,
                        inode_t cwd,
                        const char *path,
                        inode_t_mem **parent_out,
                        char *name_out) {
    if (!cache || !path || !parent_out || !name_out) {
        return MODERNFS_EINVAL;
    }

    // 提取目录和文件名
    char dir_path[4096];
    int ret = path_dirname(path, dir_path, sizeof(dir_path));
    if (ret != MODERNFS_SUCCESS) {
        return ret;
    }

    const char *basename = path_basename(path);
    if (!basename || strlen(basename) > MAX_FILENAME) {
        return MODERNFS_EINVAL;
    }

    strcpy(name_out, basename);

    // 解析父目录
    if (strlen(dir_path) == 0 || strcmp(dir_path, ".") == 0) {
        // 当前目录
        *parent_out = inode_get(cache, cwd);
    } else if (strcmp(dir_path, "/") == 0) {
        // 根目录
        *parent_out = inode_get(cache, root);
    } else {
        *parent_out = path_resolve(cache, root, cwd, dir_path, true);
    }

    if (!*parent_out) {
        return MODERNFS_ENOENT;
    }

    // 检查是否为目录
    if ((*parent_out)->disk.type != INODE_TYPE_DIR) {
        inode_put(cache, *parent_out);
        *parent_out = NULL;
        return MODERNFS_EINVAL;
    }

    return MODERNFS_SUCCESS;
}

// ============ 提取文件名 ============

const char *path_basename(const char *path) {
    if (!path || *path == '\0') {
        return ".";
    }

    const char *last_slash = NULL;
    const char *p = path;

    // 跳过末尾的斜杠
    size_t len = strlen(path);
    while (len > 0 && path[len - 1] == '/') {
        len--;
    }

    if (len == 0) {
        return "/";
    }

    // 查找最后一个斜杠
    for (size_t i = 0; i < len; i++) {
        if (path[i] == '/') {
            last_slash = &path[i];
        }
    }

    if (!last_slash) {
        // 没有斜杠，整个路径就是文件名
        static char buf[MAX_FILENAME + 1];
        size_t copy_len = len < MAX_FILENAME ? len : MAX_FILENAME;
        memcpy(buf, path, copy_len);
        buf[copy_len] = '\0';
        return buf;
    }

    // 返回最后一个斜杠之后的部分
    static char buf[MAX_FILENAME + 1];
    size_t copy_len = len - (last_slash - path + 1);
    if (copy_len > MAX_FILENAME) {
        copy_len = MAX_FILENAME;
    }
    memcpy(buf, last_slash + 1, copy_len);
    buf[copy_len] = '\0';

    return buf;
}

// ============ 提取目录部分 ============

int path_dirname(const char *path, char *dir_out, size_t size) {
    if (!path || !dir_out || size == 0) {
        return MODERNFS_EINVAL;
    }

    // 跳过末尾的斜杠
    size_t len = strlen(path);
    while (len > 0 && path[len - 1] == '/') {
        len--;
    }

    if (len == 0) {
        // 只有斜杠
        if (size < 2) {
            return MODERNFS_EINVAL;
        }
        strcpy(dir_out, "/");
        return MODERNFS_SUCCESS;
    }

    // 查找最后一个斜杠
    const char *last_slash = NULL;
    for (size_t i = 0; i < len; i++) {
        if (path[i] == '/') {
            last_slash = &path[i];
        }
    }

    if (!last_slash) {
        // 没有斜杠，目录是 "."
        if (size < 2) {
            return MODERNFS_EINVAL;
        }
        strcpy(dir_out, ".");
        return MODERNFS_SUCCESS;
    }

    // 计算目录长度
    size_t dir_len = last_slash - path;

    if (dir_len == 0) {
        // 根目录
        if (size < 2) {
            return MODERNFS_EINVAL;
        }
        strcpy(dir_out, "/");
        return MODERNFS_SUCCESS;
    }

    if (dir_len >= size) {
        return MODERNFS_EINVAL;
    }

    memcpy(dir_out, path, dir_len);
    dir_out[dir_len] = '\0';

    return MODERNFS_SUCCESS;
}
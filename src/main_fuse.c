#define _GNU_SOURCE
#define FUSE_USE_VERSION 31

#include "modernfs/fuse_ops.h"
#include "modernfs/fs_context.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fuse.h>

/**
 * ModernFS FUSE主程序
 *
 * 用法: modernfs <device> <mountpoint> [options]
 * 例如: modernfs /tmp/test.img /tmp/mnt -f
 */

struct modernfs_config {
    char *device;
    int show_help;
    int read_only;
};

#define MODERNFS_OPT(t, p) { t, offsetof(struct modernfs_config, p), 1 }

static const struct fuse_opt modernfs_opts[] = {
    MODERNFS_OPT("--device=%s", device),
    MODERNFS_OPT("-r", read_only),
    MODERNFS_OPT("--read-only", read_only),
    FUSE_OPT_KEY("-h", 0),
    FUSE_OPT_KEY("--help", 0),
    FUSE_OPT_END
};

static void show_help(const char *progname) {
    printf("Usage: %s <device> <mountpoint> [options]\n\n", progname);
    printf("ModernFS options:\n");
    printf("    --device=<s>         Device path (disk image file)\n");
    printf("    -r, --read-only      Mount filesystem read-only\n");
    printf("\n");
    printf("General options:\n");
    printf("    -h, --help           Show this help message\n");
    printf("    -f                   Run in foreground (debugging)\n");
    printf("    -d                   Enable debug output\n");
    printf("    -s                   Single-threaded mode\n");
    printf("\n");
    printf("Example:\n");
    printf("    %s /tmp/test.img /tmp/mnt -f\n", progname);
    printf("\n");
}

static int modernfs_opt_proc(void *data, const char *arg, int key,
                             struct fuse_args *outargs) {
    struct modernfs_config *config = (struct modernfs_config *)data;

    switch (key) {
    case 0:
        // Help
        show_help(outargs->argv[0]);
        config->show_help = 1;
        return fuse_opt_add_arg(outargs, "-h");

    case FUSE_OPT_KEY_NONOPT:
        // 第一个非选项参数是device
        if (!config->device) {
            config->device = strdup(arg);
            return 0;
        }
        // 其他非选项参数传递给FUSE (mountpoint等)
        return 1;

    default:
        return 1;
    }
}

int main(int argc, char *argv[]) {
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    struct modernfs_config config = {0};
    fs_context_t *ctx = NULL;

    // 解析命令行参数
    if (fuse_opt_parse(&args, &config, modernfs_opts, modernfs_opt_proc) == -1) {
        return 1;
    }

    // 显示帮助
    if (config.show_help) {
        fuse_opt_free_args(&args);
        return 0;
    }

    // 检查device参数
    if (!config.device) {
        fprintf(stderr, "Error: device path is required\n");
        fprintf(stderr, "Try '%s --help' for more information\n", argv[0]);
        fuse_opt_free_args(&args);
        return 1;
    }

    printf("╔════════════════════════════════════════╗\n");
    printf("║       ModernFS FUSE Driver v1.0        ║\n");
    printf("╚════════════════════════════════════════╝\n\n");

    printf("Device: %s\n", config.device);
    printf("Mode: %s\n\n", config.read_only ? "read-only" : "read-write");

    // 初始化文件系统上下文
    ctx = fs_context_init(config.device, config.read_only);
    if (!ctx) {
        fprintf(stderr, "Failed to initialize filesystem context\n");
        free(config.device);
        fuse_opt_free_args(&args);
        return 1;
    }

    // 初始化FUSE操作表
    struct fuse_operations ops;
    modernfs_init_ops(&ops);

    // 启动FUSE主循环
    printf("Starting FUSE main loop...\n");
    printf("Press Ctrl+C to unmount\n\n");

    int ret = fuse_main(args.argc, args.argv, &ops, ctx);

    // 清理
    // 注意: ctx会在modernfs_destroy中被清理
    free(config.device);
    fuse_opt_free_args(&args);

    return ret;
}
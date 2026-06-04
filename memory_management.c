/*
 * ============================================================
 *  操作系统与系统编程 —— 实验4：内存管理模拟实验
 * ============================================================
 *  任务1：动态分区分配模拟（首次适应算法 First Fit）
 *    - 模拟内存的分配与回收过程
 *    - 支持相邻空闲块合并（紧缩）
 *    - 每步操作后统计外部碎片率
 *  任务2：页面置换算法模拟（FIFO / LRU）
 *    - 固定序列测试 + 随机序列测试
 *    - 局部性原理模拟 + Belady 异常验证
 *    - 结果输出到 CSV 文件，便于 Excel 绘图
 *
 *  编译环境：Visual Studio 2022
 *  运行后两个任务按顺序自动执行，无需菜单选择。
 * ============================================================
 */

#define _CRT_SECURE_NO_WARNINGS   /* VS2022 兼容：允许使用 scanf/printf */

/* 标准库头文件 */
#include <stdio.h>    /* printf, fprintf, fopen, fclose 等输入输出函数 */
#include <stdlib.h>   /* malloc, free, rand, srand 等内存/随机数函数 */
#include <string.h>   /* strcmp, strcpy 等字符串操作函数 */
#include <time.h>     /* time() 用于随机数种子 */

#ifdef _WIN32
#include <windows.h>   /* SetConsoleOutputCP —— Windows 控制台 UTF-8 编码支持 */
#endif

/* ============================================================
 *                  公共常量与宏定义
 * ============================================================ */
#define MAX_BLOCKS    100         /* 分区表最大条目数（空闲/已分配各最多 100 项） */
#define TOTAL_MEMORY  640         /* 初始内存大小（地址范围 0~639） */
#define MAX_PAGES     1200        /* 页面引用串最大长度（随机测试用 1000） */
#define MAX_FRAMES    20          /* 物理页帧上限（测试范围 1~10） */

/* ============================================================
 *           任务1：动态分区分配 —— 数据结构
 * ============================================================ */

/*
 * 内存块结构体
 * 每个内存块记录一段连续的内存区域信息，
 * 包括起始地址、大小、状态和占用进程名
 */
typedef struct memory_block {
    int start_addr;               /* 起始地址（在总内存中的偏移量） */
    int size;                     /* 块大小（单位与总内存一致） */
    char status[10];              /* 状态标记："free" 表示空闲，"used" 表示已分配 */
    char process[20];             /* 占用该块的进程名（若为空闲则为空串） */
} MemoryBlock;

/*
 * 全局分区表
 * frees[]   —— 空闲分区表，记录所有未被分配的内存块
 * occupys[] —— 已分配分区表，记录所有被进程占用的内存块
 */
static MemoryBlock frees[MAX_BLOCKS];    /* 空闲分区表 */
static MemoryBlock occupys[MAX_BLOCKS];  /* 已分配分区表 */
static int free_count  = 0;              /* 当前空闲分区的数量 */
static int alloc_count = 0;              /* 当前已分配分区的数量 */

/* ============================================================
 *           任务1：动态分区分配 —— 辅助函数
 * ============================================================ */

/*
 * 按起始地址对空闲分区表排序（升序）
 * 排序后便于合并相邻空闲块
 */
static void sort_free_table(void)
{
    int i, j;
    MemoryBlock temp;  /* 交换用临时变量 */

    /* 冒泡排序：按起始地址升序排列空闲分区 */
    for (i = 0; i < free_count - 1; i++) {
        for (j = 0; j < free_count - 1 - i; j++) {
            /* 若前一块的地址大于后一块，则交换 */
            if (frees[j].start_addr > frees[j + 1].start_addr) {
                temp = frees[j];
                frees[j] = frees[j + 1];
                frees[j + 1] = temp;
            }
        }
    }
}

/*
 * 合并相邻的空闲分区
 * 扫描排序后的空闲表，若两个相邻块的地址连续，则合并为一个大块
 */
static void merge_free_blocks(void)
{
    int i;
    sort_free_table();
    for (i = 0; i < free_count - 1; ) {
        /* 判断当前块的末尾是否恰好等于下一块的起始地址 */
        if (frees[i].start_addr + frees[i].size == frees[i + 1].start_addr) {
            /* 合并：扩大当前块，删除下一块 */
            frees[i].size += frees[i + 1].size;
            int j;
            for (j = i + 1; j < free_count - 1; j++) {
                frees[j] = frees[j + 1];
            }
            free_count--;
            /* 合并后不递增 i，继续检查当前块能否和新的下一块再合并 */
        } else {
            i++;
        }
    }
}

/*
 * 计算并打印外部碎片率
 * 外部碎片 = 除最大空闲块外的所有空闲空间之和
 * 外部碎片率 = 外部碎片 / 总内存 × 100%
 */
static void print_fragmentation(void)
{
    int total_free = 0;    /* 空闲总量 */
    int max_free = 0;      /* 最大空闲块 */
    int i;

    /* 遍历空闲表，累加总空闲量并找到最大空闲块 */
    for (i = 0; i < free_count; i++) {
        total_free += frees[i].size;          /* 累加总空闲 */
        if (frees[i].size > max_free) {
            max_free = frees[i].size;         /* 更新最大空闲块 */
        }
    }

    /*
     * 外部碎片 = 总空闲 - 最大连续空闲块
     * 外部碎片率 = 外部碎片 / 总内存 × 100%
     */
    int external_frag = total_free - max_free;
    double frag_rate = (double)external_frag / TOTAL_MEMORY * 100.0;

    printf("  [碎片统计] 总空闲: %d, 最大连续空闲块: %d, "
           "外部碎片: %d, 外部碎片率: %.1f%%\n",
           total_free, max_free, external_frag, frag_rate);
}

/*
 * 打印当前内存分区状态（空闲分区表 + 已分配分区表 + 碎片率）
 */
static void print_memory_status(void)
{
    int i;

    /* 打印空闲分区表：列出所有未被分配的内存块 */
    printf("  ┌─ 空闲分区表 ─────────────────────────────\n");
    if (free_count == 0) {
        printf("  │  （无空闲分区）\n");  /* 没有空闲块时的提示 */
    }
    for (i = 0; i < free_count; i++) {
        /* 显示每个空闲块的起始地址和大小 */
        printf("  │  [起始地址: %3d, 大小: %3d]\n",
               frees[i].start_addr, frees[i].size);
    }

    /* 打印已分配分区表：列出所有被进程占用的内存块 */
    printf("  ├─ 已分配分区表 ───────────────────────────\n");
    if (alloc_count == 0) {
        printf("  │  （无已分配分区）\n");  /* 没有已分配块时的提示 */
    }
    for (i = 0; i < alloc_count; i++) {
        /* 显示进程名、起始地址和占用大小 */
        printf("  │  [进程 %s: 起始地址: %3d, 大小: %3d]\n",
               occupys[i].process, occupys[i].start_addr, occupys[i].size);
    }
    printf("  └──────────────────────────────────────────\n");

    /* 打印当前的外部碎片率统计信息 */
    print_fragmentation();
    printf("\n");
}

/* ============================================================
 *           任务1：动态分区分配 —— 核心函数
 * ============================================================ */

/*
 * 首次适应算法（First Fit）分配内存
 *
 * 从空闲分区表头开始扫描，找到第一个大小 >= 请求的块：
 *   - 若恰好相等，整块分配
 *   - 若更大，分割为"已分配部分"和"剩余空闲部分"
 *
 * 参数：process_name —— 进程名
 *       req_size    —— 请求的内存大小
 * 返回：分配的起始地址，失败返回 -1
 */
static int allocate_memory(const char *process_name, int req_size)
{
    int i;
    sort_free_table();  /* 先按地址排序，保证首次适应从最低地址开始扫描 */

    /* 线性扫描空闲表，找第一个 size >= req_size 的块 */
    for (i = 0; i < free_count; i++) {
        if (frees[i].size >= req_size) {
            /* 找到第一个足够大的空闲块，记录其起始地址 */
            int addr = frees[i].start_addr;

            /* 将分配信息写入已分配表 */
            occupys[alloc_count].start_addr = addr;   /* 起始地址 */
            occupys[alloc_count].size = req_size;       /* 分配大小 */
            strcpy(occupys[alloc_count].status, "used"); /* 标记为已使用 */
            strcpy(occupys[alloc_count].process, process_name); /* 记录进程名 */
            alloc_count++;                              /* 已分配数+1 */

            if (frees[i].size == req_size) {
                /* 空闲块恰好用完，从空闲表中删除 */
                int j;
                for (j = i; j < free_count - 1; j++) {
                    frees[j] = frees[j + 1];
                }
                free_count--;
            } else {
                /* 空闲块分割：缩小当前块 */
                frees[i].start_addr += req_size;
                frees[i].size -= req_size;
            }
            return addr;  /* 分配成功，返回起始地址 */
        }
    }
    /* 遍历完所有空闲块都没有找到足够大的，分配失败 */
    return -1;
}

/*
 * 回收指定进程的内存
 *
 * 从已分配表中找到该进程，将其占用的块放回空闲表，
 * 然后调用 merge_free_blocks() 合并相邻空闲块。
 *
 * 参数：process_name —— 要回收的进程名
 * 返回：成功返回 0，未找到返回 -1
 */
static int free_memory(const char *process_name)
{
    int i, found = -1;

    /* 线性搜索已分配表，查找目标进程的内存块 */
    for (i = 0; i < alloc_count; i++) {
        if (strcmp(occupys[i].process, process_name) == 0) {
            found = i;  /* 记录找到的位置索引 */
            break;
        }
    }

    if (found == -1) {
        printf("  错误：未找到进程 %s\n", process_name);
        return -1;
    }

    /* 将回收的内存块重新加入空闲分区表 */
    frees[free_count].start_addr = occupys[found].start_addr;
    frees[free_count].size = occupys[found].size;
    strcpy(frees[free_count].status, "free");
    strcpy(frees[free_count].process, "");
    free_count++;  /* 空闲分区数+1 */

    /* 从已分配表中删除该进程（数组元素前移填补） */
    for (i = found; i < alloc_count - 1; i++) {
        occupys[i] = occupys[i + 1];
    }
    alloc_count--;  /* 已分配分区数-1 */

    /* 回收后尝试合并相邻的空闲块，减少外部碎片 */
    merge_free_blocks();

    return 0;
}

/*
 * 执行任务1：动态分区分配模拟
 * 按照指导书的固定测试脚本依次执行 9 步操作
 */
static void task1_dynamic_partition(void)
{
    int addr;

    printf("==========================================================\n");
    printf("  任务1：动态分区分配模拟（首次适应算法 First Fit）\n");
    printf("  初始内存大小: %d（地址范围 0 ~ %d）\n",
           TOTAL_MEMORY, TOTAL_MEMORY - 1);
    printf("==========================================================\n\n");

    /*
     * 初始化内存状态：
     * 整个内存空间作为一个大的空闲分区，
     * 起始地址 = 0，大小 = TOTAL_MEMORY (640)
     */
    frees[0].start_addr = 0;
    frees[0].size = TOTAL_MEMORY;
    strcpy(frees[0].status, "free");
    strcpy(frees[0].process, "");
    free_count = 1;     /* 初始时只有一个空闲分区 */
    alloc_count = 0;    /* 初始时没有已分配分区 */

    /*
     * 下面按指导书的固定测试脚本执行 9 步操作：
     *   分配 P1(130) → P2(60) → P3(100)
     *   回收 P2
     *   分配 P4(200)
     *   回收 P1
     *   分配 P5(140)
     *   回收 P3
     *   查询最终状态
     */

    /* 步骤1：分配进程 P1，请求大小 130 */
    printf(">>> 步骤1：分配进程 P1，请求大小 130\n");
    addr = allocate_memory("P1", 130);
    if (addr >= 0) printf("  分配成功，起始地址: %d\n", addr);
    else           printf("  分配失败！内存不足\n");
    print_memory_status();

    /* 步骤2：分配进程 P2，请求大小 60 */
    printf(">>> 步骤2：分配进程 P2，请求大小 60\n");
    addr = allocate_memory("P2", 60);
    if (addr >= 0) printf("  分配成功，起始地址: %d\n", addr);
    else           printf("  分配失败！内存不足\n");
    print_memory_status();

    /* 步骤3：分配进程 P3，请求大小 100 */
    printf(">>> 步骤3：分配进程 P3，请求大小 100\n");
    addr = allocate_memory("P3", 100);
    if (addr >= 0) printf("  分配成功，起始地址: %d\n", addr);
    else           printf("  分配失败！内存不足\n");
    print_memory_status();

    /* 步骤4：回收进程 P2 */
    printf(">>> 步骤4：回收进程 P2\n");
    free_memory("P2");
    printf("  已回收进程 P2 的内存\n");
    print_memory_status();

    /* 步骤5：分配进程 P4，请求大小 200 */
    printf(">>> 步骤5：分配进程 P4，请求大小 200\n");
    addr = allocate_memory("P4", 200);
    if (addr >= 0) printf("  分配成功，起始地址: %d\n", addr);
    else           printf("  分配失败！内存不足\n");
    print_memory_status();

    /* 步骤6：回收进程 P1 */
    printf(">>> 步骤6：回收进程 P1\n");
    free_memory("P1");
    printf("  已回收进程 P1 的内存\n");
    print_memory_status();

    /* 步骤7：分配进程 P5，请求大小 140 */
    printf(">>> 步骤7：分配进程 P5，请求大小 140\n");
    addr = allocate_memory("P5", 140);
    if (addr >= 0) printf("  分配成功，起始地址: %d\n", addr);
    else           printf("  分配失败！内存不足\n");
    print_memory_status();

    /* 步骤8：回收进程 P3 */
    printf(">>> 步骤8：回收进程 P3\n");
    free_memory("P3");
    printf("  已回收进程 P3 的内存\n");
    print_memory_status();

    /* 步骤9：查询当前状态（额外打印一次最终状态） */
    printf(">>> 步骤9：查询当前最终状态\n");
    print_memory_status();
}

/* ============================================================
 *           任务2：页面置换算法 —— 数据结构与函数
 * ============================================================ */

/*
 * FIFO 页面置换算法
 *
 * 使用循环队列管理物理页帧：
 *   - 新页面进入时，如果帧已满，淘汰队首（最早进入的页面）
 *   - 命中时不做任何操作（FIFO 不关心访问顺序）
 *
 * 参数：pages      —— 页面引用串数组
 *       page_count —— 引用串长度
 *       frame_num  —— 物理页帧数
 *       verbose    —— 是否打印每步详情（1=是, 0=否）
 * 返回：缺页次数
 */
static int fifo_replace(int pages[], int page_count, int frame_num, int verbose)
{
    int *frames;             /* 物理页帧数组 */
    int pointer = 0;         /* 循环队列指针，指向下一个被替换的位置 */
    int fault_count = 0;     /* 缺页次数 */
    int current_size = 0;    /* 当前已装入的页帧数 */
    int i, j;
    int hit;

    /* 动态分配页帧数组，并初始化为 -1（表示空帧） */
    frames = (int *)malloc(sizeof(int) * frame_num);
    for (i = 0; i < frame_num; i++) {
        frames[i] = -1;      /* -1 表示该帧位置还没有装入任何页面 */
    }

    /* 遍历每一个页面访问请求 */
    for (i = 0; i < page_count; i++) {
        /* 线性搜索检查页面是否已在内存中（命中判定） */
        hit = 0;
        for (j = 0; j < current_size; j++) {
            if (frames[j] == pages[i]) {
                hit = 1;     /* 命中，FIFO 不需要做任何更新 */
                break;
            }
        }

        if (!hit) {
            /* 缺页：目标页面不在内存中，需要调入 */
            fault_count++;
            if (current_size < frame_num) {
                /* 帧未满：直接放入下一个空位 */
                frames[current_size] = pages[i];
                current_size++;
            } else {
                /* 帧已满：用循环队列指针淘汰最早进入的页面 */
                frames[pointer] = pages[i];
                pointer = (pointer + 1) % frame_num;  /* 指针往前移动，回绕 */
            }
        }

        /* 如果 verbose=1，打印本次访问后的内存状态快照 */
        if (verbose) {
            printf("  访问页面: %d | 内存状态: ", pages[i]);
            for (j = 0; j < frame_num; j++) {
                if (frames[j] == -1) printf("[ ]");
                else                 printf("[%d]", frames[j]);
            }
            printf(" | %s | 缺页次数: %d\n",
                   hit ? "命中" : "缺页", fault_count);
        }
    }

    free(frames);        /* 释放动态分配的页帧数组 */
    return fault_count;   /* 返回总缺页次数 */
}

/*
 * LRU 页面置换算法
 *
 * 使用"最近使用时间戳"策略：
 *   - 为每个页帧记录最近一次被访问的时间（步数）
 *   - 需要淘汰时，选择时间戳最小（最久未使用）的页面
 *
 * 参数与返回值同 fifo_replace
 */
static int lru_replace(int pages[], int page_count, int frame_num, int verbose)
{
    int *frames;              /* 物理页帧数组 */
    int *last_used;           /* 每个帧的最近访问时间 */
    int fault_count = 0;      /* 缺页次数 */
    int current_size = 0;     /* 当前已装入的页帧数 */
    int i, j;
    int hit;

    /* 动态分配页帧数组和时间戳数组 */
    frames    = (int *)malloc(sizeof(int) * frame_num);
    last_used = (int *)malloc(sizeof(int) * frame_num);
    for (i = 0; i < frame_num; i++) {
        frames[i] = -1;       /* 初始化为空帧 */
        last_used[i] = -1;    /* 时间戳初始化为 -1 */
    }

    /* 遍历每一个页面访问请求 */
    for (i = 0; i < page_count; i++) {
        /* 线性搜索检查页面是否已在内存中 */
        hit = 0;
        for (j = 0; j < current_size; j++) {
            if (frames[j] == pages[i]) {
                hit = 1;
                last_used[j] = i;   /* 命中：更新该页的最近访问时间 */
                break;
            }
        }

        if (!hit) {
            /* 缺页：需要调入新页面 */
            fault_count++;
            if (current_size < frame_num) {
                /* 帧未满：直接放入 */
                frames[current_size] = pages[i];
                last_used[current_size] = i;  /* 记录调入时间 */
                current_size++;
            } else {
                /* 帧已满：找到 last_used 最小的帧（即最久未使用的页面） */
                int min_idx = 0;
                for (j = 1; j < frame_num; j++) {
                    if (last_used[j] < last_used[min_idx]) {
                        min_idx = j;  /* 更新最小时间戳索引 */
                    }
                }
                /* 淘汰最久未使用的页面，装入新页 */
                frames[min_idx] = pages[i];
                last_used[min_idx] = i;
            }
        }

        /* 打印当前步骤的内存状态 */
        if (verbose) {
            printf("  访问页面: %d | 内存状态: ", pages[i]);
            for (j = 0; j < frame_num; j++) {
                if (frames[j] == -1) printf("[ ]");
                else                 printf("[%d]", frames[j]);
            }
            printf(" | %s | 缺页次数: %d\n",
                   hit ? "命中" : "缺页", fault_count);
        }
    }

    free(frames);        /* 释放页帧数组 */
    free(last_used);     /* 释放时间戳数组 */
    return fault_count;  /* 返回总缺页次数 */
}

/*
 * 生成具有时间局部性和空间局部性的页面引用串
 *
 * 策略：以 80% 的概率访问最近 3 个页面附近（±1），
 *       以 20% 的概率随机跳转到任意页面。
 *
 * 参数：seq     —— 输出数组
 *       length  —— 序列长度
 *       max_page—— 页面号上限（0 ~ max_page-1）
 */
static void generate_locality_sequence(int seq[], int length, int max_page)
{
    int i;
    int current = rand() % max_page;  /* 随机选取起始页面 */

    for (i = 0; i < length; i++) {
        if (rand() % 100 < 80) {
            /*
             * 80% 概率：访问当前页面附近（模拟时间+空间局部性）
             * offset 取值 -1, 0, +1，表示访问前一页/当前页/后一页
             */
            int offset = (rand() % 3) - 1;
            current = current + offset;
            /* 边界保护：确保页面号不越界 */
            if (current < 0) current = 0;
            if (current >= max_page) current = max_page - 1;
        } else {
            /* 20% 概率：随机跳转到任意页面（模拟程序跳转） */
            current = rand() % max_page;
        }
        seq[i] = current;  /* 将生成的页面号存入序列 */
    }
}

/*
 * 写入 UTF-8 BOM 到文件开头
 * 这样 Excel 打开 CSV 时能正确识别中文
 */
static void write_utf8_bom(FILE *fp)
{
    if (fp) {
        fputc(0xEF, fp);   /* UTF-8 BOM 字节1: 0xEF */
        fputc(0xBB, fp);   /* UTF-8 BOM 字节2: 0xBB */
        fputc(0xBF, fp);   /* UTF-8 BOM 字节3: 0xBF */
    }
}

/*
 * 执行任务2：页面置换算法模拟
 * 包含固定测试、工程化性能评估和 Belady 异常验证
 */
static void task2_page_replacement(void)
{
    int i, f;
    int faults_fifo, faults_lru;
    FILE *csv_file;

    printf("\n==========================================================\n");
    printf("  任务2：页面置换算法模拟（FIFO / LRU）\n");
    printf("==========================================================\n\n");

    /*
     * ---- 2.1 固定测试脚本 ----
     * 指导书要求的固定页面访问序列，分别在 3 帧和 4 帧下测试
     * 此序列是经典的 Belady 异常序列
     */
    {
        int test_seq[] = {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};
        int test_len = sizeof(test_seq) / sizeof(test_seq[0]);  /* 序列长度 12 */
        int test_frames[] = {3, 4};  /* 分别测试 3 帧和 4 帧 */

        printf("──────────────────────────────────────────\n");
        printf("  固定测试序列: [");
        for (i = 0; i < test_len; i++) {
            printf("%d%s", test_seq[i], (i < test_len - 1) ? "," : "");
        }
        printf("]\n");
        printf("──────────────────────────────────────────\n\n");

        /* 对每种帧数，分别运行 FIFO 和 LRU 并打印详细过程 */
        for (f = 0; f < 2; f++) {
            int nf = test_frames[f];  /* 当前测试的帧数 */

            /* 运行 FIFO 算法，verbose=1 表示打印每步详情 */
            printf("--- FIFO 算法（%d 帧）---\n", nf);
            faults_fifo = fifo_replace(test_seq, test_len, nf, 1);
            printf("  总缺页次数: %d, 缺页率: %.1f%%\n\n",
                   faults_fifo, (double)faults_fifo / test_len * 100.0);

            /* 运行 LRU 算法，verbose=1 表示打印每步详情 */
            printf("--- LRU 算法（%d 帧）---\n", nf);
            faults_lru = lru_replace(test_seq, test_len, nf, 1);
            printf("  总缺页次数: %d, 缺页率: %.1f%%\n\n",
                   faults_lru, (double)faults_lru / test_len * 100.0);
        }

        /*
         * Belady 异常分析：
         * 比较 FIFO 在 3 帧和 4 帧下的缺页次数，
         * 证明增加帧数反而导致缺页增多
         */
        {
            int faults_3, faults_4;  /* 分别记录 3帧和4帧的缺页次数 */
            printf("──────────────────────────────────────────\n");
            printf("  Belady 异常验证\n");
            printf("──────────────────────────────────────────\n");
            printf("  对于序列 [1,2,3,4,1,2,5,1,2,3,4,5]:\n");
            faults_3 = fifo_replace(test_seq, test_len, 3, 0);
            printf("  FIFO 3帧 缺页次数: %d\n", faults_3);
            faults_4 = fifo_replace(test_seq, test_len, 4, 0);
            printf("  FIFO 4帧 缺页次数: %d\n", faults_4);
            printf("  结论：增加帧数后缺页次数反而增加（%d > %d），即 Belady 异常！\n\n",
                   faults_4, faults_3);
        }
    }

    /*
     * ---- 2.2 随机序列测试 ----
     * 生成长度 1000 的随机页面引用串（页面号 0~9），
     * 统计物理块数从 1 增加到 10 时，FIFO 和 LRU 的缺页率变化，
     * 并将结果输出到 CSV 文件
     */
    {
        int random_seq[MAX_PAGES];
        int seq_len = 1000;    /* 引用串长度 */
        int max_page = 10;     /* 页面号范围 0~9 */

        printf("──────────────────────────────────────────\n");
        printf("  随机序列测试（长度 %d，页面号 0~%d）\n", seq_len, max_page - 1);
        printf("──────────────────────────────────────────\n");

        /* 用当前时间作为随机数种子，保证每次运行结果不同 */
        srand((unsigned int)time(NULL));

        /* 生成长度为 1000 的随机页面引用串，页面号范围 0~9 */
        for (i = 0; i < seq_len; i++) {
            random_seq[i] = rand() % max_page;
        }

        /* 打开 CSV 文件，写入表头和数据，便于用 Excel 绘制曲线图 */
        csv_file = fopen("page_fault_data.csv", "w");
        if (csv_file) {
            write_utf8_bom(csv_file);
            fprintf(csv_file, "物理块数,FIFO缺页次数,FIFO缺页率(%%),LRU缺页次数,LRU缺页率(%%)\n");
        }

        printf("  物理块数 | FIFO缺页次数 | FIFO缺页率 | LRU缺页次数 | LRU缺页率\n");
        printf("  ---------+--------------+------------+-------------+-----------\n");

        /* 物理块数从 1 到 10，逐步测试两种算法 */
        for (f = 1; f <= 10; f++) {
            double rate_fifo, rate_lru;
            /* verbose=0 不打印每步详情，只统计缺页次数 */
            faults_fifo = fifo_replace(random_seq, seq_len, f, 0);
            faults_lru  = lru_replace(random_seq, seq_len, f, 0);
            /* 计算缺页率 = 缺页次数 / 总访问次数 × 100% */
            rate_fifo = (double)faults_fifo / seq_len * 100.0;
            rate_lru  = (double)faults_lru / seq_len * 100.0;

            /* 打印到控制台 */
            printf("  %5d    | %8d     | %7.1f%%   | %8d    | %7.1f%%\n",
                   f, faults_fifo, rate_fifo, faults_lru, rate_lru);

            /* 同时写入 CSV 文件 */
            if (csv_file) {
                fprintf(csv_file, "%d,%d,%.1f,%d,%.1f\n",
                        f, faults_fifo, rate_fifo, faults_lru, rate_lru);
            }
        }
        printf("\n");

        if (csv_file) {
            fclose(csv_file);  /* 关闭 CSV 文件 */
            printf("  [CSV] 随机序列测试数据已保存到 page_fault_data.csv\n\n");
        }
    }

    /*
     * ---- 2.3 局部性原理模拟 ----
     * 生成具有时间局部性和空间局部性的页面访问序列，
     * 对比 LRU 和 FIFO 在此场景下的表现差异
     */
    {
        int locality_seq[MAX_PAGES];  /* 局部性序列存储数组 */
        int seq_len = 1000;           /* 序列长度 */
        int max_page = 10;            /* 页面号范围 */
        FILE *csv_locality;           /* 局部性测试 CSV 文件指针 */

        printf("──────────────────────────────────────────\n");
        printf("  局部性原理模拟（具有时间/空间局部性的序列）\n");
        printf("──────────────────────────────────────────\n");

        /* 生成具有局部性的页面访问序列 */
        generate_locality_sequence(locality_seq, seq_len, max_page);

        /* 打开局部性测试结果 CSV 文件 */
        csv_locality = fopen("page_fault_locality.csv", "w");
        if (csv_locality) {
            write_utf8_bom(csv_locality);
            fprintf(csv_locality, "物理块数,FIFO缺页次数,FIFO缺页率(%%),LRU缺页次数,LRU缺页率(%%)\n");
        }

        printf("  物理块数 | FIFO缺页次数 | FIFO缺页率 | LRU缺页次数 | LRU缺页率\n");
        printf("  ---------+--------------+------------+-------------+-----------\n");

        /* 物理块数 1~10，测试局部性序列下两种算法的表现 */
        for (f = 1; f <= 10; f++) {
            double rate_fifo, rate_lru;
            faults_fifo = fifo_replace(locality_seq, seq_len, f, 0);
            faults_lru  = lru_replace(locality_seq, seq_len, f, 0);
            /* 计算缺页率并输出 */
            rate_fifo = (double)faults_fifo / seq_len * 100.0;
            rate_lru  = (double)faults_lru / seq_len * 100.0;

            printf("  %5d    | %8d     | %7.1f%%   | %8d    | %7.1f%%\n",
                   f, faults_fifo, rate_fifo, faults_lru, rate_lru);

            /* 写入局部性测试 CSV 文件 */
            if (csv_locality) {
                fprintf(csv_locality, "%d,%d,%.1f,%d,%.1f\n",
                        f, faults_fifo, rate_fifo, faults_lru, rate_lru);
            }
        }
        printf("\n");

        if (csv_locality) {
            fclose(csv_locality);  /* 关闭局部性 CSV 文件 */
            printf("  [CSV] 局部性序列测试数据已保存到 page_fault_locality.csv\n\n");
        }

        printf("  [分析] 在具有局部性的序列中，LRU 通常优于 FIFO，\n");
        printf("         因为 LRU 能利用时间局部性保留近期频繁访问的页面。\n\n");
    }

    /*
     * ---- 2.4 Belady 异常专项验证 ----
     * 用经典 Belady 序列，逐步增加帧数（从 1 到 6），
     * 对比 FIFO 和 LRU 的缺页趋势，突出 FIFO 的反常现象
     */
    {
        /* Belady 异常经典序列：在 3→4 帧时 FIFO 缺页次数反增 */
        int belady_seq[] = {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};
        int belady_len = sizeof(belady_seq) / sizeof(belady_seq[0]);
        FILE *csv_belady;  /* Belady 测试 CSV 文件指针 */

        printf("──────────────────────────────────────────\n");
        printf("  Belady 异常专项验证\n");
        printf("  测试序列: [1,2,3,4,1,2,5,1,2,3,4,5]\n");
        printf("──────────────────────────────────────────\n");

        /* 打开 CSV 文件，记录 Belady 异常测试数据 */
        csv_belady = fopen("page_fault_belady.csv", "w");
        if (csv_belady) {
            write_utf8_bom(csv_belady);   /* 写 BOM 便于 Excel 识别中文 */
            fprintf(csv_belady, "帧数,FIFO缺页次数,LRU缺页次数\n");
        }

        /* 表头 */
        printf("  帧数 | FIFO缺页次数 | LRU缺页次数\n");
        printf("  -----+--------------+-------------\n");

        /* 帧数从 1 到 6，比较 FIFO 和 LRU 的缺页次数 */
        for (f = 1; f <= 6; f++) {
            faults_fifo = fifo_replace(belady_seq, belady_len, f, 0);
            faults_lru  = lru_replace(belady_seq, belady_len, f, 0);

            printf("  %3d  | %8d     | %8d\n", f, faults_fifo, faults_lru);

            /* 写入 Belady CSV 文件 */
            if (csv_belady) {
                fprintf(csv_belady, "%d,%d,%d\n", f, faults_fifo, faults_lru);
            }
        }
        printf("\n");

        if (csv_belady) {
            fclose(csv_belady);  /* 关闭 Belady CSV 文件 */
            printf("  [CSV] Belady 异常数据已保存到 page_fault_belady.csv\n\n");
        }

        printf("  [结论] FIFO 在 3帧→4帧时缺页次数从 9 增加到 10，\n");
        printf("         证明了 Belady 异常。而 LRU 作为栈算法，\n");
        printf("         不会出现此异常——帧数增加时缺页次数单调不增。\n\n");
    }
}

/* ============================================================
 *                        主函数
 * ============================================================
 * 程序入口点。
 * 依次执行任务1和任务2，两个任务自动连续运行。
 * 运行结束后在当前目录生成 3 个 CSV 文件。
 * ============================================================ */
int main(void)
{
#ifdef _WIN32
    /* 设置 Windows 控制台为 UTF-8 编码，确保中文正常显示 */
    SetConsoleOutputCP(65001);
#endif

    /* 打印实验标题 */
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║     操作系统与系统编程 —— 实验4：内存管理模拟实验       ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n\n");

    /* 执行任务1：动态分区分配模拟（首次适应算法 + 空闲块合并 + 碎片率统计） */
    task1_dynamic_partition();

    /* 执行任务2：页面置换算法模拟（FIFO/LRU + 随机测试 + 局部性 + Belady） */
    /* 任务2 会自动生成 3 个 CSV 文件，用于 Excel 绘制对比曲线图 */
    task2_page_replacement();

    printf("==========================================================\n");
    printf("  实验结束。CSV 数据文件已生成，可导入 Excel 绘制曲线图。\n");
    printf("==========================================================\n");

    return 0;
}

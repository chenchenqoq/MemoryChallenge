#define _CRT_SECURE_NO_WARNINGS

/*
 * rank.c 排行榜：成绩保存 + 排行榜页面
 *
 * ============================================================
 * 这个文件负责两件事
 *   1. 成绩的存取：saveScore() 把一局成绩记进排行榜并写文件
 *   2. 排行榜页面：reset / update / draw 三个函数，配合 main 的逐帧循环
 * ============================================================
 *
 * 成绩文件 ranks.txt 的格式（纯文本，一行一条）
 *   难度 用户名 用时
 *   例如：2 player1 38    表示中级，玩家 player1，用了 38 秒
 *
 * 两条必须记住的规则
 *   规则一：每个难度最多保留 5 名，多出来的被挤掉
 *   规则二：同一个玩家在同一难度只保留"用时更短"的那次
 *           用时相同不算更好，所以不会刷新自己的记录
 *
 * 两个容易搞混的编号
 *   难度 level ：对外统一用 1 / 2 / 3 表示初级 / 中级 / 高级
 *   数组下标 k ：C 语言数组从 0 开始，所以 k = level - 1
 *   文件里存的难度也用 1 / 2 / 3，方便人直接看
 *
 * ============================================================
 * 建议阅读顺序（从简单的开始看）
 *   1. levelName()          难度编号转中文，最简单
 *   2. sortLevel()          把一档成绩按用时排序
 *   3. loadRanks()          读文件到内存
 *   4. writeRanks()         内存写回文件
 *   5. saveScore()          核心：判断该不该进榜、插到哪
 *   6. reset/update/draw    排行榜页面
 * ============================================================
 */

#include <errno.h>     // errno：区分"文件不存在"和"文件打不开"
#include <stdio.h>
#include <string.h>

#include "rank.h"     // 本模块对外声明的函数
#include "ui.h"       // 用 drawButton / isButtonClicked 画按钮

/* ---------------- 几个固定的数值 ---------------- */

#define RANK_LEVELS 3     // 难度个数：初级、中级、高级
#define RANK_TOP    5     // 每个难度保留前几名
#define RANK_NAME   32    // 用户名字节数：最多存 31 字节 + 结尾的 '\0'
#define RANK_FILE   "ranks.txt"   // 成绩文件名，和程序在同一目录

/*
 * 排行榜页面的布局参数
 * 绘制和点击判断都用这一组宏，改一处就能同时生效
 * 写死成 800x600 是因为初稿窗口固定这个尺寸
 */
#define PAGE_BTN_W    160.0f   // 难度按钮宽度
#define PAGE_BTN_H    44.0f    // 按钮高度
#define PAGE_BTN_Y    112.0f   // 三个难度按钮的纵坐标（离窗口顶部）
#define PAGE_BTN_GAP  20.0f    // 按钮之间的空隙
#define PAGE_BTN_X0   60.0f    // 第一个难度按钮的横坐标
#define PAGE_BACK_X   40.0f    // 返回按钮横坐标
#define PAGE_BACK_Y   520.0f   // 返回按钮纵坐标

/*
 * 一条成绩记录
 * name    玩家名字，最长 31 字节
 * seconds 通关用时（秒），越小越好，0 秒也算合法成绩
 */
typedef struct
{
    char name[RANK_NAME];
    int  seconds;
} Score;

/* ---------------- 模块内部的全局数据 ---------------- */

// 二维数组：第一维是难度（0 初 1 中 2 高），第二维是名次（0 第一 1 第二 …）
// 用 static 是为了"只在本文件可见"，外面的代码只能通过函数访问，改不了数组
static Score g_rank[RANK_LEVELS][RANK_TOP];

// 每个难度当前已经存了几条成绩
// 数组固定 5 格，但可能只存了 2 条，所以要单独记个数，避免把空格当成有效成绩
static int g_count[RANK_LEVELS];

// 是否已经从文件读过一次
// 0 表示还没读，1 表示读过，作用是避免每次调用都读盘
static int g_loaded = 0;

// 上次读取是否出错
// 出错时不再允许写文件，否则会把原有的成绩覆盖坏
static int g_readError = 0;

// 排行榜页面当前显示哪一档难度（1 初 2 中 3 高）
static int g_pageLevel = 1;

// 页面是否已经准备过
// 用来实现"只在必要时读文件"：进入页面时读一次，之后切换难度不再重复读
static int g_pageReady = 0;

/*
 * 功能：将难度编号换成显示用的名称
 * 输入：level 是难度编号 1、2、3
 * 输出：不修改排行榜
 * 返回值：对应的名称字符串
 */
static const char *levelName(int level)
{
    // 集中在这里转中文，其他地方就不用重复写 if
    if (level == 1) return "初级";
    if (level == 2) return "中级";
    if (level == 3) return "高级";
    return "未知";    // 正常情况下不会走到这里
}

/*
 * 功能：将某一难度的成绩按用时从短到长排序
 * 输入：k 是难度在数组中的下标，从 0 开始
 * 输出：直接调整该难度的成绩数组，用时相同的记录保持原顺序
 * 返回值：无
 */
static void sortLevel(int k)
{
    int i;
    int j;
    Score tmp;

    // 这里用插入排序，非常适合"最多 5 条"这么小的数据量
    // 相比自带的 qsort，插入排序是稳定的：用时相同不会交换顺序
    // 而我们的规则要求"同用时先入榜的排在前面"，稳定排序正好满足
    for (i = 1; i < g_count[k]; i++)
    {
        tmp = g_rank[k][i];    // 把第 i 条先拿出来，空出一个位置
        j = i;

        // 只要前一条比它慢（用 > 而不是 >=），就把前一条往后挪一格
        // 用 > 是"稳定"的关键：一样快时不挪，原来的先后顺序保持不变
        while (j > 0 && g_rank[k][j - 1].seconds > tmp.seconds)
        {
            g_rank[k][j] = g_rank[k][j - 1];
            j--;
        }

        g_rank[k][j] = tmp;    // 把 tmp 放进空出来的正确位置
    }
}

/*
 * 功能：读取各难度的成绩并排序
 * 输入：无参数，从 ranks.txt 读取
 * 输出：更新成绩数组、记录个数和读取状态；文件不存在时按空榜处理
 * 返回值：无
 */
static void loadRanks(void)
{
    FILE *fp;          // 文件句柄，打开失败时是 NULL
    char line[128];    // 一行文字的缓冲区
    int k;

    // 先把内存清空
    // 本函数可能被调用多次（例如保存失败后重新读盘），
    // 不清空会把数据叠成两份
    for (k = 0; k < RANK_LEVELS; k++)
    {
        g_count[k] = 0;
        memset(g_rank[k], 0, sizeof(g_rank[k]));
    }

    g_readError = 0;

    // 每次打开前先清掉上一次的错误号，才能判断本次失败的原因
    errno = 0;
    fp = fopen(RANK_FILE, "r");    // "r" 表示只读打开

    if (fp == NULL)
    {
        // 只有"文件真的不存在"才能按空榜处理
        // 这是第一次运行的正常情况，此时允许保存
        if (errno == ENOENT)
        {
            g_loaded = 1;
            return;
        }

        // 其它原因都属于读取失败，例如
        //   - 文件被别的程序独占打开
        //   - 没有读取权限
        //   - 路径其实是个目录
        // 这时绝不能当成空榜，否则一保存就会用空数组把原有成绩覆盖掉
        g_readError = 1;
        g_loaded = 0;      // 保留重试机会：下次调用会重新尝试读取
        return;
    }

    // 一行一行读到文件结束
    while (fgets(line, sizeof(line), fp) != NULL)
    {
        char name[RANK_NAME];
        int level;
        int seconds;

        // sscanf 按 "难度 名字 用时" 拆分
        // 返回 3 表示三个字段都读到了；不是 3 说明这行是坏的（乱码、空行等）
        // 坏行直接跳过，不能让整个加载失败
        if (sscanf(line, "%d %31s %d", &level, name, &seconds) != 3) continue;

        // 难度必须是 1~3，否则这行数据没有意义
        if (level < 1 || level > RANK_LEVELS) continue;

        // 用时不能是负数
        if (seconds < 0) continue;

        k = level - 1;    // 文件里是 1/2/3，数组下标要减 1

        // 这一档已经存满 5 条时，后面的直接丢弃
        // 目的是防止文件被手工改成 100 行，读进数组时越界
        if (g_count[k] >= RANK_TOP) continue;

        // strncpy 最多复制 RANK_NAME-1 个字符，防止名字过长写爆数组
        strncpy(g_rank[k][g_count[k]].name, name, RANK_NAME - 1);

        // strncpy 在源串太长时不会自动补 '\0'，必须自己补，
        // 否则后面用字符串函数会读到数组之外的垃圾
        g_rank[k][g_count[k]].name[RANK_NAME - 1] = '\0';
        g_rank[k][g_count[k]].seconds = seconds;
        g_count[k]++;
    }

    // ferror 判断读到一半是否真出错（比如磁盘问题）
    // 这和"文件不存在"是两回事：前者要报错，后者按空榜处理
    if (ferror(fp)) g_readError = 1;

    fclose(fp);

    // 读进来的顺序不一定是按用时排的（文件可能被手工改过），
    // 所以统一再排序一次，保证数组随时有序
    for (k = 0; k < RANK_LEVELS; k++) sortLevel(k);

    g_loaded = 1;
}

/*
 * 功能：保存当前排行榜
 * 输入：无参数，使用本文件中的成绩数组
 * 输出：将三个难度的成绩写入 ranks.txt
 * 返回值：成功返回 1，失败返回 0
 */
static int writeRanks(void)
{
    FILE *fp;
    int k;
    int i;

    // 上次读取就出过错，说明文件状态不明，此时不能覆盖它
    if (g_readError) return 0;

    // "w" 表示覆盖写：先清空原文件，再把内存内容整体写回去
    // 之所以整体重写而不是改其中一行，是因为文本文件改一行要处理长度变化，
    // 容易写乱，而总共最多 15 行，整体重写几乎不花时间
    fp = fopen(RANK_FILE, "w");
    if (fp == NULL) return 0;

    for (k = 0; k < RANK_LEVELS; k++)
    {
        for (i = 0; i < g_count[k]; i++)
        {
            // 写出 "难度 用户名 用时"，这里 k+1 转回 1/2/3
            if (fprintf(fp, "%d %s %d\n", k + 1, g_rank[k][i].name, g_rank[k][i].seconds) < 0)
            {
                fclose(fp);
                return 0;    // 写到一半失败，直接报错
            }
        }
    }

    // fclose 才是真正把缓冲区刷进磁盘的动作，
    // 所以它失败也要当成写失败处理
    if (fclose(fp) != 0) return 0;

    return 1;
}

/*
 * 功能：提交玩家成绩，保留各难度前五名和同一玩家的最好成绩
 * 输入：name 是玩家名称，level 是难度 1、2、3，seconds 是非负用时秒数
 * 输出：需要更新时修改成绩数组并写入 ranks.txt
 * 返回值：更新并保存成功返回 1，未刷新或未进前五返回 0，参数或文件读写错误返回 -1
 */
int saveScore(const char name[], int level, int seconds)
{
    int k;      // 数组下标（难度减 1）
    int i;      // 循环变量
    int pos;    // 新成绩应该插入的位置
    int cnt;    // 插入后的条数
    int dup;    // 同名玩家原来的位置，-1 表示榜上还没有这个名字

    /* ---- 第 1 步：检查参数 ---- */
    // 名字不能为空，也不能是空指针
    if (name == NULL || name[0] == '\0') return -1;

    // 难度只接受 1、2、3
    if (level < 1 || level > RANK_LEVELS) return -1;

    // 用时不能是负数
    // 注意 0 秒是合法成绩（通关非常快），"中途退出"由调用方用返回值区分，
    // 不要用 0 秒来表示退出
    if (seconds < 0) return -1;

    // 第一次用到才读文件，避免每次保存都读盘
    if (!g_loaded) loadRanks();

    // 文件读取有问题时不能瞎写，直接报错
    if (g_readError) return -1;

    k = level - 1;

    /* ---- 第 2 步：在榜上找同名玩家 ---- */
    dup = -1;
    for (i = 0; i < g_count[k]; i++)
    {
        if (strcmp(g_rank[k][i].name, name) == 0)
        {
            dup = i;
            break;    // 找到就可以停了
        }
    }

    // 名字已在榜上，且新成绩不比旧成绩快（>= 包含"一样快"）
    // 一样快也不算刷新，因为榜上那条出现得更早，按规则保留旧的
    if (dup >= 0 && seconds >= g_rank[k][dup].seconds) return 0;

    /* ---- 第 3 步：确定插入位置 ---- */
    // 数组是按用时从小到大排好的，
    // 从左往右找"第一个比自己慢的人"，插在他前面即可
    // 用 <= 而不是 <，是为了让"一样快"的人排在自己后面（后来者靠后）
    pos = 0;
    while (pos < g_count[k] && g_rank[k][pos].seconds <= seconds) pos++;

    // pos 走到 5（或更大）说明榜满了，而且自己比第五名还慢，
    // 这种情况下进不了榜，返回 0
    if (pos >= RANK_TOP) return 0;

    /* ---- 第 4 步：如果自己原来已在榜上，先把旧记录删掉 ---- */
    // 不删的话同一个人会占两行，还可能把别人挤掉
    if (dup >= 0)
    {
        // 从 dup 位置开始，把后面的记录依次往前盖一格
        for (i = dup; i < g_count[k] - 1; i++) g_rank[k][i] = g_rank[k][i + 1];

        g_count[k]--;    // 少了一条

        // 删掉的记录如果排在自己新位置前面，插入位置要往前挪一格，
        // 因为整个数组已经向前移动过了
        if (pos > dup) pos--;
    }

    /* ---- 第 5 步：腾出空位并插入 ---- */
    cnt = g_count[k];

    // 还没满 5 条就多一条；已经满了就保持 5，最后一格会被后面的挪动覆盖掉，
    // 相当于自动把第五名挤出去
    if (cnt < RANK_TOP) cnt++;

    // 从末尾往前，把 pos 及之后的记录都往后挪一格
    // 必须从后往前挪，从前往后会把自己的数据覆盖掉
    for (i = cnt - 1; i > pos; i--) g_rank[k][i] = g_rank[k][i - 1];

    // 把新成绩写进空出来的位置
    strncpy(g_rank[k][pos].name, name, RANK_NAME - 1);
    g_rank[k][pos].name[RANK_NAME - 1] = '\0';    // 手动补结束符
    g_rank[k][pos].seconds = seconds;
    g_count[k] = cnt;

    /* ---- 第 6 步：写回文件 ---- */
    if (!writeRanks())
    {
        // 写盘失败时，内存里那条"以为保存成功"的成绩必须撤掉，
        // 否则下一次提交会被它误导（例如被判成"没变快"而返回 0）
        // 最简单的做法是重新从文件读一遍，让内存和磁盘恢复一致
        g_loaded = 0;
        loadRanks();
        return -1;
    }

    // 通知页面：下次进入时重新读一次，保证能看到刚保存的成绩
    g_pageReady = 0;
    return 1;
}

/*
 * 功能：检查某一难度是否有成绩
 * 输入：level 是难度 1、2、3
 * 输出：必要时读取排行榜文件
 * 返回值：有成绩返回 1，全为空返回 0，读取失败返回 -1
 */
static int hasRankData(int level)
{
    if (!g_loaded) loadRanks();
    if (g_readError) return -1;
    if (level < 1 || level > RANK_LEVELS) return 0;

    // 只要这一档的条数大于 0，就说明有成绩
    return (g_count[level - 1] > 0) ? 1 : 0;
}

/*
 * 功能：准备排行榜页面
 * 输入：无
 * 输出：默认选择初级，必要时读取一次成绩文件
 * 返回值：无
 */
void resetRankPage(void)
{
    // 每次进入页面都从初级开始看，这是产品要求
    g_pageLevel = 1;

    // 只在第一次进入页面时读文件；
    // 之后切换难度只是在内存里换数组下标，不需要重读磁盘
    if (!g_pageReady)
    {
        if (!g_loaded) loadRanks();
        g_pageReady = 1;
    }
}

/*
 * 功能：取得某一难度按钮的位置
 * 输入：level 是难度 1、2、3
 * 输出：无
 * 返回值：按钮矩形
 */
static Rectangle levelButtonRect(int level)
{
    Rectangle rect;    // Rectangle 是 raylib 的结构体：x、y、width、height

    // 三个按钮横向排列：
    // 第 1 个在 x=60，第 2 个在 x=240，第 3 个在 x=420
    rect.x = PAGE_BTN_X0 + (level - 1) * (PAGE_BTN_W + PAGE_BTN_GAP);
    rect.y = PAGE_BTN_Y;
    rect.width = PAGE_BTN_W;
    rect.height = PAGE_BTN_H;

    return rect;
}

/*
 * 功能：取得返回按钮的位置
 * 输入：无
 * 输出：无
 * 返回值：按钮矩形
 */
static Rectangle backButtonRect(void)
{
    Rectangle rect;

    rect.x = PAGE_BACK_X;
    rect.y = PAGE_BACK_Y;
    rect.width = PAGE_BTN_W;
    rect.height = PAGE_BTN_H;

    return rect;
}

/*
 * 功能：处理难度选择和返回按钮
 * 输入：无，通过本帧鼠标操作
 * 输出：修改当前显示的难度
 * 返回值：0 留在排行榜，1 返回玩家菜单
 */
int updateRankPage(void)
{
    int level;

    // 逐个检查三个难度按钮有没有被点
    // isButtonClicked 内部只认"本帧刚按下"，所以按住不放不会连续触发
    for (level = 1; level <= RANK_LEVELS; level++)
    {
        if (isButtonClicked(levelButtonRect(level)))
        {
            g_pageLevel = level;    // 只改状态，具体画什么由 draw 负责
            return 0;
        }
    }

    // 返回按钮：告诉 main 该回玩家菜单了
    // 注意本函数不负责切换页面，切换是 main 的事，我们只返回意图
    if (isButtonClicked(backButtonRect())) return 1;

    return 0;
}

/*
 * 功能：绘制当前难度排行榜
 * 输入：font 为加载成功的中文字体
 * 输出：名次、用户名、用时，或暂无成绩、读取失败提示
 * 返回值：无
 */
void drawRankPage(Font font)
{
    int level;
    int k;
    int i;
    float textWidth;    // 用来做水平居中
    char text[128];     // 拼数字用的临时缓冲

    /* ---- 标题 ---- */
    // MeasureTextEx 返回这段文字画出来有多宽，
    // 用 (窗口宽 - 文字宽) / 2 就能让标题正好居中
    textWidth = MeasureTextEx(font, "排行榜", 36.0f, 2.0f).x;
    DrawTextEx(font, "排行榜", (Vector2){ (800.0f - textWidth) / 2.0f, 40.0f },
               36.0f, 2.0f, (Color){ 30, 40, 55, 255 });

    /* ---- 三个难度按钮 ---- */
    for (level = 1; level <= RANK_LEVELS; level++)
    {
        Rectangle rect = levelButtonRect(level);

        // 按钮本体交给公共 ui 模块画，保证和其他页面风格一致
        drawButton(rect, levelName(level), font);

        // 当前选中的难度，在按钮下面画一条蓝色横线作为标记
        if (level == g_pageLevel)
        {
            DrawRectangle((int)rect.x, (int)(rect.y + rect.height + 4.0f),
                          (int)rect.width, 3, (Color){ 70, 130, 190, 255 });
        }
    }

    // 返回按钮放在左下角
    drawButton(backButtonRect(), "返回", font);

    k = g_pageLevel - 1;

    /* ---- 读取失败时，页面上给一句提示，不要画一个空表 ---- */
    if (g_readError)
    {
        textWidth = MeasureTextEx(font, "排行榜文件读取失败", 24.0f, 1.0f).x;
        DrawTextEx(font, "排行榜文件读取失败",
                   (Vector2){ (800.0f - textWidth) / 2.0f, 300.0f },
                   24.0f, 1.0f, (Color){ 190, 60, 60, 255 });
        return;
    }

    /* ---- 这一档没有成绩 ---- */
    // 关键点：只是画一行字，页面依然可以切换难度、可以点返回，
    // 不能像旧版那样停 3 秒再自动跳走
    if (g_count[k] == 0)
    {
        textWidth = MeasureTextEx(font, "暂无成绩", 24.0f, 1.0f).x;
        DrawTextEx(font, "暂无成绩", (Vector2){ (800.0f - textWidth) / 2.0f, 300.0f },
                   24.0f, 1.0f, (Color){ 120, 125, 135, 255 });
        return;
    }

    /* ---- 表头与分隔线 ---- */
    DrawTextEx(font, "名次", (Vector2){ 150.0f, 196.0f }, 22.0f, 1.0f, (Color){ 90, 95, 105, 255 });
    DrawTextEx(font, "用户名", (Vector2){ 250.0f, 196.0f }, 22.0f, 1.0f, (Color){ 90, 95, 105, 255 });
    DrawTextEx(font, "用时", (Vector2){ 480.0f, 196.0f }, 22.0f, 1.0f, (Color){ 90, 95, 105, 255 });
    DrawRectangle(130, 226, 420, 1, (Color){ 200, 205, 215, 255 });

    /* ---- 成绩列表 ---- */
    for (i = 0; i < g_count[k]; i++)
    {
        float y = 244.0f + i * 44.0f;    // 每条记录间隔 44 像素

        // 名次：数组下标 i 从 0 开始，显示时加 1
        sprintf(text, "%d", i + 1);
        DrawTextEx(font, text, (Vector2){ 150.0f, y }, 22.0f, 1.0f, (Color){ 30, 40, 55, 255 });

        // 用户名
        DrawTextEx(font, g_rank[k][i].name, (Vector2){ 250.0f, y },
                   22.0f, 1.0f, (Color){ 30, 40, 55, 255 });

        // 用时：先拼成 "12 秒" 再画
        // 注意 "秒" 这个字必须包含在 ui.c 的字形集合里，否则会显示成空白
        sprintf(text, "%d 秒", g_rank[k][i].seconds);
        DrawTextEx(font, text, (Vector2){ 480.0f, y }, 22.0f, 1.0f, (Color){ 30, 40, 55, 255 });
    }
}

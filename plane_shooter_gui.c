/* plane_shooter_gui.c - 天天打飞机：Win32 GDI 卡通窗口版
 * 单文件、零第三方依赖：蓝天白云 + 圆滚滚卡通飞机 + 爆炸粒子 + 鼠标按钮。
 * 编译：tcc plane_shooter_gui.c -o plane_shooter_gui.exe -luser32 -lgdi32 -lmsvcrt
 *      gcc plane_shooter_gui.c -o plane_shooter_gui.exe
 * 操作：A/D 或 ←/→ 左右移动，W/S 或 ↑/↓ 上下移动，空格射击，P 暂停，鼠标点按钮。
 * 掉血规则：只有被敌机撞到或被敌方红色子弹打中才掉血；漏掉敌机不扣血。
 */

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef CP_UTF8
#define CP_UTF8 65001
#endif
#ifndef MultiByteToWideChar
int WINAPI MultiByteToWideChar(unsigned int codePage, unsigned int flags,
                               const char *multiByte, int multiByteLen,
                               wchar_t *wide, int wideLen);
#endif
typedef BOOL (WINAPI *PFN_SET_DPI_AWARE)(void);

#define WIN_W 1200
#define WIN_H 800
#define FIELD_X 24
#define FIELD_Y 92
#define FIELD_W 900
#define FIELD_H 650
#define SIDE_X 950

/* 顶部横幅上的暂停 / 继续按钮 */
#define PAUSE_BTN_X 798
#define PAUSE_BTN_Y 21
#define PAUSE_BTN_W 112
#define PAUSE_BTN_H 38

#define MAX_BULLETS 240
#define MAX_EBULLETS 40
#define MAX_ENEMIES 30
#define MAX_PARTICLES 300
#define BEST_FILE "plane_shooter_best.txt"
#define PROGRESS_FILE "plane_shooter_progress.txt"
#define COIN_FILE "plane_shooter_coins.txt"

/* 技能：火力 / 侧翼炮 / 护盾 / 生命 / 炸弹 */
#define SK_FIRE   0
#define SK_WING   1
#define SK_SHIELD 2
#define SK_LIFE   3
#define SK_BOMB   4
#define SK_COUNT  5

#define ST_START 0
#define ST_PLAY  1
#define ST_PAUSE 2
#define ST_CLEAR 3
#define ST_OVER  4
#define ST_UPGRADE 5

#define C_NAVY    RGB(38, 70, 110)
#define C_TEXT    RGB(40, 62, 88)
#define C_PANEL   RGB(255, 252, 242)
#define C_GOLD    RGB(246, 186, 52)
#define C_RED     RGB(232, 84, 84)
#define C_GREEN   RGB(92, 186, 108)

typedef struct { float x, y, vx, vy; int alive; } Bullet;
typedef struct {
    float x, y, speed;
    int hp, type, alive;
    double hitT, nextFire;
} Enemy;
typedef struct {
    float x, y, vx, vy;
    int life, maxLife, size;
    COLORREF color;
} Particle;
typedef struct { float x, y, speed, size, phase; } Cloud;

/* 关底 BOSS：打满本关目标后登场，只朝下攻击，击落有额外金币 */
typedef struct {
    float x, y, vx;
    int alive, hp, maxHp, reward;
    double nextFire, hitT;
    int shots;
} Boss;


typedef struct {
    float x, y;
    int lives, score, kills, level, best;
    int levelKills, levelTarget;   /* 本关击落数 / 本关目标 */
    int state;
    int coinsEarned, bombsLeft;    /* 本局金币 / 剩余炸弹 */
    double lastShot, lastSpawn, invulnUntil, flashT;
    double shieldUntil, shieldReadyAt, lastWing, bombFlash;
    Bullet bullets[MAX_BULLETS];
    Bullet ebullets[MAX_EBULLETS];
    Enemy enemies[MAX_ENEMIES];
    Particle parts[MAX_PARTICLES];
    Boss boss;                     /* 关底 BOSS */
    int bossSpawned;               /* 本关 BOSS 是否已登场 */
    double bossIntroT;             /* BOSS 登场提示剩余时间 */
    double bossRewardT;            /* 击败奖励提示剩余时间 */
    double bossClearDelay;         /* BOSS 爆炸后延迟结算通关 */
    int bossRewardCoins;           /* 击败 BOSS 获得的金币 */
} Game;

static Game g;
static HWND g_hwnd;
static int g_running = 1;
static int g_maxLevel = 1;    /* 已解锁的最高关卡 */
static int g_selected = 1;    /* 开始界面选中的关卡 */
static int g_coins = 0;       /* 金币总数（跨局保存） */
static int g_skill[SK_COUNT]; /* 各技能等级 */
static HFONT hFontTitle, hFontBig, hFontMid, hFontSmall;
static int mouseX = -1, mouseY = -1;
static Cloud clouds[8];

/* ---------------- GDI 小工具 ---------------- */

static void fillRect(HDC hdc, int x, int y, int w, int h, COLORREF c)
{
    HBRUSH br = CreateSolidBrush(c);
    RECT r;
    r.left = x; r.top = y; r.right = x + w; r.bottom = y + h;
    FillRect(hdc, &r, br);
    DeleteObject(br);
}

static void ellipseFill(HDC hdc, int x1, int y1, int x2, int y2, COLORREF fill, COLORREF line)
{
    HPEN pen = CreatePen(PS_SOLID, 2, line);
    HBRUSH br = CreateSolidBrush(fill);
    HPEN op = (HPEN)SelectObject(hdc, pen);
    HBRUSH ob = (HBRUSH)SelectObject(hdc, br);
    Ellipse(hdc, x1, y1, x2, y2);
    SelectObject(hdc, op); SelectObject(hdc, ob);
    DeleteObject(pen); DeleteObject(br);
}

static void roundBox(HDC hdc, int x, int y, int w, int h, COLORREF fill, COLORREF line, int r)
{
    HPEN pen = CreatePen(PS_SOLID, 2, line);
    HBRUSH br = CreateSolidBrush(fill);
    HPEN op = (HPEN)SelectObject(hdc, pen);
    HBRUSH ob = (HBRUSH)SelectObject(hdc, br);
    RoundRect(hdc, x, y, x + w, y + h, r, r);
    SelectObject(hdc, op); SelectObject(hdc, ob);
    DeleteObject(pen); DeleteObject(br);
}

static void poly(HDC hdc, POINT *p, int n, COLORREF fill, COLORREF line)
{
    HPEN pen = CreatePen(PS_SOLID, 2, line);
    HBRUSH br = CreateSolidBrush(fill);
    HPEN op = (HPEN)SelectObject(hdc, pen);
    HBRUSH ob = (HBRUSH)SelectObject(hdc, br);
    Polygon(hdc, p, n);
    SelectObject(hdc, op); SelectObject(hdc, ob);
    DeleteObject(pen); DeleteObject(br);
}

static void lineTo(HDC hdc, int x1, int y1, int x2, int y2, COLORREF c, int w)
{
    HPEN pen = CreatePen(PS_SOLID, w, c);
    HPEN op = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, x1, y1, NULL);
    LineTo(hdc, x2, y2);
    SelectObject(hdc, op);
    DeleteObject(pen);
}

static void textOut(HDC hdc, int x, int y, const char *utf8, HFONT font, COLORREF color)
{
    wchar_t buf[512];
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, buf, 512);
    HFONT old;
    if (n <= 0) return;
    old = (HFONT)SelectObject(hdc, font);
    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);
    TextOutW(hdc, x, y, buf, n - 1);
    SelectObject(hdc, old);
}

/* 窗口标题走 Unicode，避免 UTF-8 源码在标题栏显示成乱码 */
static void setWindowTitleUtf8(HWND hwnd, const char *utf8)
{
    wchar_t buf[256];
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, buf, 256);
    if (n > 0) SetWindowTextW(hwnd, buf);
}

static HFONT makeFont(int h, int weight){
    return CreateFontA(h, 0, 0, 0, weight, 0, 0, 0,
                       DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                       ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                       "Microsoft YaHei");
}

/* ---------------- 游戏逻辑 ---------------- */

static void loadBest(void)
{
    FILE *f = fopen(BEST_FILE, "r");
    g.best = 0;
    if (f) {
        if (fscanf(f, "%d", &g.best) != 1) g.best = 0;
        fclose(f);
    }
}

static void saveBest(void)
{
    FILE *f;
    if (g.score <= g.best) return;
    g.best = g.score;
    f = fopen(BEST_FILE, "w");
    if (f) { fprintf(f, "%d\n", g.best); fclose(f); }
}

/* 已解锁关卡存档：打完第 N 关才解锁第 N+1 关 */
static void loadProgress(void)
{
    FILE *f = fopen(PROGRESS_FILE, "r");
    g_maxLevel = 1;
    if (f) {
        if (fscanf(f, "%d", &g_maxLevel) != 1) g_maxLevel = 1;
        fclose(f);
    }
    if (g_maxLevel < 1) g_maxLevel = 1;
    g_selected = g_maxLevel;
}

static void saveProgress(void)
{
    FILE *f = fopen(PROGRESS_FILE, "w");
    if (f) { fprintf(f, "%d\n", g_maxLevel); fclose(f); }
}

/* 每关目标：第 1 关 12 架，之后每关多 4 架 */
static int targetForLevel(int lv)
{
    return 8 + lv * 4;
}

/* ---------------- 技能与金币 ---------------- */

static int skillMaxLv(int s)
{
    switch (s) {
    case SK_FIRE:   return 5;
    case SK_WING:   return 3;
    case SK_SHIELD: return 3;
    case SK_LIFE:   return 2;
    default:        return 3;   /* SK_BOMB */
    }
}

/* 从当前等级升到下一级要花的金币 */
static int skillCost(int s, int lv)
{
    static const int cost[SK_COUNT][5] = {
        { 20, 35, 55, 80, 110 },   /* 火力 */
        { 25, 45, 70,  0,   0 },   /* 侧翼炮 */
        { 30, 50, 70,  0,   0 },   /* 护盾 */
        { 45, 80,  0,  0,   0 },   /* 生命 */
        { 35, 60, 90,  0,   0 }    /* 炸弹 */
    };
    if (s < 0 || s >= SK_COUNT || lv < 0 || lv >= 5) return 0;
    return cost[s][lv];
}

static const char *skillName(int s)
{
    static const char *names[SK_COUNT] = { "火力强化", "侧翼副炮", "能量护盾", "生命上限", "清屏炸弹" };
    return (s >= 0 && s < SK_COUNT) ? names[s] : "";
}

/* 当前等级的效果说明 */
static const char *skillDesc(int s, int lv)
{
    static const char *fire[6] = {
        "单发子弹", "双发齐射", "三向散射", "三向散射 · 射速提升",
        "四发扇射 · 覆盖面更大", "五发扇射 · 射速最快"
    };
    static const char *wing[4] = {
        "未解锁", "机身两侧各射一道斜弹", "侧射更密集", "双侧各两发 · 弹幕更宽"
    };
    static const char *shield[4] = {
        "未解锁", "按 X 开盾：3.3 秒无敌，冷却 12 秒",
        "按 X 开盾：4.1 秒无敌，冷却 10 秒",
        "按 X 开盾：4.9 秒无敌，冷却 8 秒"
    };
    static const char *life[3] = {
        "初始 3 条命", "初始 4 条命", "初始 5 条命"
    };
    static const char *bomb[4] = {
        "未解锁", "按 B 清屏，每关 1 次", "按 B 清屏，每关 2 次", "按 B 清屏，每关 3 次"
    };
    switch (s) {
    case SK_FIRE:   return fire[lv];
    case SK_WING:   return wing[lv];
    case SK_SHIELD: return shield[lv];
    case SK_LIFE:   return life[lv];
    default:        return bomb[lv];
    }
}

static void saveCoins(void)
{
    FILE *f = fopen(COIN_FILE, "w");
    int i;
    if (!f) return;
    fprintf(f, "%d\n", g_coins);
    for (i = 0; i < SK_COUNT; i++) fprintf(f, "%d ", g_skill[i]);
    fprintf(f, "\n");
    fclose(f);
}

static void loadCoins(void)
{
    FILE *f = fopen(COIN_FILE, "r");
    int i;
    g_coins = 0;
    for (i = 0; i < SK_COUNT; i++) g_skill[i] = 0;
    if (!f) return;
    if (fscanf(f, "%d", &g_coins) != 1) g_coins = 0;
    if (g_coins < 0) g_coins = 0;
    for (i = 0; i < SK_COUNT; i++) {
        if (fscanf(f, "%d", &g_skill[i]) != 1) g_skill[i] = 0;
        if (g_skill[i] < 0) g_skill[i] = 0;
        if (g_skill[i] > skillMaxLv(i)) g_skill[i] = skillMaxLv(i);
    }
    fclose(f);
}

/* 尝试把某个技能升一级，成功返回 1 */
static int buyUpgrade(int s)
{
    int lv, cost;
    if (s < 0 || s >= SK_COUNT) return 0;
    lv = g_skill[s];
    if (lv >= skillMaxLv(s)) return 0;
    cost = skillCost(s, lv);
    if (g_coins < cost) return 0;
    g_coins -= cost;
    g_skill[s] = lv + 1;
    saveCoins();
    return 1;
}

/* 开始指定关卡：清空场上单位，重置位置并给 1 秒入场保护 */
static void startLevel(int lv)
{
    double t = (double)GetTickCount() / 1000.0;
    int i;
    g.level = lv;
    g.levelKills = 0;
    g.levelTarget = targetForLevel(lv);
    g.x = FIELD_W / 2;
    g.y = FIELD_H - 118;   /* 给底部技能栏留出空间 */
    for (i = 0; i < MAX_BULLETS; i++) g.bullets[i].alive = 0;
    for (i = 0; i < MAX_EBULLETS; i++) g.ebullets[i].alive = 0;
    for (i = 0; i < MAX_ENEMIES; i++) g.enemies[i].alive = 0;
    g.invulnUntil = t + 1.0;
    g.lastSpawn = t + 0.5;
    /* 每关刷新炸弹次数与护盾冷却 */
    g.bombsLeft = g_skill[SK_BOMB];
    g.shieldUntil = 0;
    g.shieldReadyAt = 0;
    g.lastWing = t;
    g.bombFlash = 0;
    /* 关底 BOSS 重新待命 */
    memset(&g.boss, 0, sizeof(g.boss));
    g.bossSpawned = 0;
    g.bossIntroT = 0;
    g.bossRewardT = 0;
    g.bossClearDelay = 0;
    g.bossRewardCoins = 0;
}

static void initGameAt(int lv)
{
    int i;
    g.lives = 3 + g_skill[SK_LIFE];
    g.score = 0;
    g.kills = 0;
    g.coinsEarned = 0;
    g.lastShot = 0;
    g.flashT = 0;
    for (i = 0; i < MAX_PARTICLES; i++) g.parts[i].life = 0;
    startLevel(lv);
}

static void boom(float x, float y, COLORREF c, int n)
{
    /* 16 个方向的单位向量，避免依赖 libm */
    static const float dxs[16] = {
        1.0f, 0.92f, 0.71f, 0.38f, 0.0f, -0.38f, -0.71f, -0.92f,
        -1.0f, -0.92f, -0.71f, -0.38f, 0.0f, 0.38f, 0.71f, 0.92f
    };
    static const float dys[16] = {
        0.0f, 0.38f, 0.71f, 0.92f, 1.0f, 0.92f, 0.71f, 0.38f,
        0.0f, -0.38f, -0.71f, -0.92f, -1.0f, -0.92f, -0.71f, -0.38f
    };
    int i, k;
    for (k = 0; k < n; k++) {
        for (i = 0; i < MAX_PARTICLES; i++) {
            if (g.parts[i].life <= 0) {
                int d = rand() % 16;
                float sp = 40.0f + (float)(rand() % 150);
                g.parts[i].x = x;
                g.parts[i].y = y;
                g.parts[i].vx = dxs[d] * sp;
                g.parts[i].vy = dys[d] * sp;
                g.parts[i].maxLife = 24 + rand() % 22;
                g.parts[i].life = g.parts[i].maxLife;
                g.parts[i].size = 3 + rand() % 6;
                g.parts[i].color = (k % 3 == 0) ? RGB(255, 245, 180) : c;
                break;
            }
        }
    }
}

/* 放一颗玩家子弹（供主炮和侧翼炮复用） */
static void addBullet(float x, float y, float vx, float vy)
{
    int i;
    for (i = 0; i < MAX_BULLETS; i++) {
        if (!g.bullets[i].alive) {
            g.bullets[i].x = x;
            g.bullets[i].y = y;
            g.bullets[i].vx = vx;
            g.bullets[i].vy = vy;
            g.bullets[i].alive = 1;
            return;
        }
    }
}

/* 主炮：技能等级决定发数、散射角与射速 */
static void shoot(void)
{
    double t = (double)GetTickCount() / 1000.0;
    int lv = g_skill[SK_FIRE];
    double interval = (lv >= 5) ? 0.10 : ((lv >= 3) ? 0.11 : 0.13);
    float ox[5], vx[5];
    int n = 1, i;

    if (g.state != ST_PLAY || t - g.lastShot < interval) return;
    if (lv == 0) {
        ox[0] = 0; vx[0] = 0; n = 1;
    } else if (lv == 1) {
        ox[0] = -9; ox[1] = 9; vx[0] = 0; vx[1] = 0; n = 2;
    } else if (lv <= 3) {
        ox[0] = -13; ox[1] = 0; ox[2] = 13;
        vx[0] = -70; vx[1] = 0; vx[2] = 70; n = 3;
    } else if (lv == 4) {
        ox[0] = -20; ox[1] = -7; ox[2] = 7; ox[3] = 20;
        vx[0] = -80; vx[1] = -25; vx[2] = 25; vx[3] = 80; n = 4;
    } else {
        ox[0] = -22; ox[1] = -11; ox[2] = 0; ox[3] = 11; ox[4] = 22;
        vx[0] = -110; vx[1] = -55; vx[2] = 0; vx[3] = 55; vx[4] = 110; n = 5;
    }
    for (i = 0; i < n; i++) {
        /* 先算成 float 变量再传参：tcc 直接传复合表达式会算错坐标 */
        float bx = g.x + ox[i];
        float by = g.y - 26.0f;
        addBullet(bx, by, vx[i], -540.0f);
    }
    g.lastShot = t;
}

/* 侧翼副炮：自动向左右斜射，等级越高越密 */
static void wingFire(double t)
{
    int lv = g_skill[SK_WING];
    double interval;
    if (lv <= 0) return;
    interval = 0.95 - lv * 0.18;
    if (t - g.lastWing < interval) return;
    g.lastWing = t;
    {
        float dx = 150.0f + (float)lv * 40.0f;
        float by = g.y + 6.0f;
        float lx = g.x - 26.0f;
        float rx = g.x + 26.0f;
        addBullet(lx, by, -dx, -470.0f);
        addBullet(rx, by, dx, -470.0f);
    }
    if (lv >= 3) {
        float ty = g.y - 5.0f;
        float lx = g.x - 18.0f;
        float rx = g.x + 18.0f;
        addBullet(lx, ty, -300.0f, -430.0f);
        addBullet(rx, ty, 300.0f, -430.0f);
    }
}

/* 护盾：按 X 开启，持续时间内免疫伤害 */
static void castShield(void)
{
    double t = (double)GetTickCount() / 1000.0;
    int lv = g_skill[SK_SHIELD];
    if (g.state != ST_PLAY || lv <= 0) return;
    if (t < g.shieldReadyAt) return;
    g.shieldUntil = t + 3.3 + (lv - 1) * 0.8;
    g.shieldReadyAt = g.shieldUntil + (12.0 - (lv - 1) * 2.0);
    boom(g.x, g.y, RGB(150, 225, 255), 14);
}

/* 关底 BOSS 登场（实现见下） */
static void spawnBoss(void);

/* 达成关卡目标：先召唤关底 BOSS，击败它才算通关 */
static void checkLevelClear(void)
{
    int k;
    if (g.levelKills < g.levelTarget) return;

    /* 目标刚好达成：清掉残余杂兵，BOSS 登场 */
    if (!g.bossSpawned) {
        for (k = 0; k < MAX_ENEMIES; k++) g.enemies[k].alive = 0;
        for (k = 0; k < MAX_EBULLETS; k++) g.ebullets[k].alive = 0;
        spawnBoss();
        return;
    }
    if (g.boss.alive) return;

    for (k = 0; k < MAX_ENEMIES; k++) g.enemies[k].alive = 0;
    for (k = 0; k < MAX_EBULLETS; k++) g.ebullets[k].alive = 0;
    if (g.level + 1 > g_maxLevel) {
        g_maxLevel = g.level + 1;
        saveProgress();
    }
    saveCoins();
    g.state = ST_CLEAR;
}

/* 炸弹：清掉全场敌机与敌机子弹（只给分，不给金币，避免刷钱） */
static void castBomb(void)
{
    int i;
    if (g.state != ST_PLAY || g_skill[SK_BOMB] <= 0 || g.bombsLeft <= 0) return;
    g.bombsLeft--;
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (!g.enemies[i].alive) continue;
        boom(g.enemies[i].x, g.enemies[i].y, RGB(255, 208, 120), 16);
        g.score += g.enemies[i].type == 2 ? 30 : (g.enemies[i].type == 1 ? 20 : 10);
        g.enemies[i].alive = 0;
        g.kills++;
        g.levelKills++;
    }
    for (i = 0; i < MAX_EBULLETS; i++) g.ebullets[i].alive = 0;
    g.bombFlash = 0.45;
    checkLevelClear();
}

/* 敌机只朝正前方（笔直向下）开火，速度慢，命中玩家才掉血 */
static void enemyFire(int idx, double t)
{
    Bullet *b;
    int i;
    for (i = 0; i < MAX_EBULLETS; i++) {
        if (g.ebullets[i].alive) continue;
        b = &g.ebullets[i];
        b->x = g.enemies[idx].x;
        b->y = g.enemies[idx].y + 20;
        b->vx = 0;
        b->vy = 130.0f + (float)(g.level - 1) * 4.0f;
        b->alive = 1;
        break;
    }
    {
        double interval = g.enemies[idx].type == 2 ? 3.8 :
                          (g.enemies[idx].type == 1 ? 4.4 : 5.2);
        interval -= (double)(g.level - 1) * 0.15;
        if (interval < 2.4) interval = 2.4;
        g.enemies[idx].nextFire = t + interval + (double)(rand() % 80) / 100.0;
    }
}

static void spawnEnemy(void)
{
    int i, r, armored, fast;
    double t = (double)GetTickCount() / 1000.0;
    double fireDelay;
    /* 关卡越高，装甲机/快速机越多，下落越快，开火越早 */
    armored = 12 + (g.level - 1) * 4;
    if (armored > 40) armored = 40;
    fast = 22 + (g.level - 1) * 3;
    if (fast > 40) fast = 40;
    fireDelay = 2.5 - (double)(g.level - 1) * 0.15;
    if (fireDelay < 1.2) fireDelay = 1.2;
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (!g.enemies[i].alive) {
            r = rand() % 100;
            g.enemies[i].type = r < armored ? 2 : (r < armored + fast ? 1 : 0);
            g.enemies[i].hp = g.enemies[i].type == 2 ? 2 : 1;
            g.enemies[i].speed = (g.enemies[i].type == 0 ? 105.0f :
                                  g.enemies[i].type == 1 ? 165.0f : 78.0f) *
                                 (1.0f + (float)(g.level - 1) * 0.15f);
            g.enemies[i].x = 34 + (float)(rand() % (FIELD_W - 68));
            g.enemies[i].y = -30;
            g.enemies[i].hitT = 0;
            g.enemies[i].nextFire = t + fireDelay + (double)(rand() % 100) / 100.0;
            g.enemies[i].alive = 1;
            return;
        }
    }
}

/* 敌机子弹通用入口（BOSS 也复用；坐标先落变量再传参） */
static void addEBullet(float x, float y, float vy)
{
    int i;
    for (i = 0; i < MAX_EBULLETS; i++) {
        if (g.ebullets[i].alive) continue;
        g.ebullets[i].x = x;
        g.ebullets[i].y = y;
        g.ebullets[i].vx = 0;
        g.ebullets[i].vy = vy;
        g.ebullets[i].alive = 1;
        return;
    }
}

/* 关底 BOSS 登场：从上方飞入，血量与奖励随关卡增长 */
static void spawnBoss(void)
{
    double t = (double)GetTickCount() / 1000.0;
    Boss *b = &g.boss;
    b->maxHp = 24 + g.level * 12;        /* 第 1 关 36 血 */
    b->hp = b->maxHp;
    b->x = FIELD_W / 2.0f;
    b->y = -110.0f;
    b->vx = (g.level % 2 == 0) ? -84.0f : 84.0f;
    b->alive = 1;
    b->hitT = 0;
    b->shots = 0;
    b->nextFire = t + 2.4;
    b->reward = 18 + g.level * 12;       /* 第 1 关 30 金币，远高于杂兵 */
    g.bossSpawned = 1;
    g.bossIntroT = 2.6;
}

/* BOSS 被击落：连环爆炸 + 大量金币，随后结算通关 */
static void killBoss(void)
{
    Boss *b = &g.boss;
    int reward = b->reward;
    float mx = b->x, my = b->y;
    float x1 = mx - 42.0f, x2 = mx + 42.0f;
    float y1 = my + 12.0f;

    b->alive = 0;
    boom(mx, my, RGB(255, 206, 130), 22);
    boom(x1, y1, RGB(255, 150, 120), 16);
    boom(x2, y1, RGB(255, 226, 150), 16);
    g.score += 200 + g.level * 80;
    g_coins += reward;
    g.coinsEarned += reward;
    g.bossRewardCoins = reward;
    g.bossRewardT = 3.4;
    g.bossClearDelay = 1.6;      /* 让爆炸和金币提示先飞一会儿再结算 */
    g.bombFlash = 0.4;
}

/* BOSS 行为：缓慢左右巡航，只朝正下方开火 */
static void updateBoss(double dt, double t)
{
    Boss *b = &g.boss;
    int i, n;
    float bx, by, vy, off;
    double interval;

    if (!b->alive) return;

    if (b->y < 118.0f) {                 /* 飞入阶段不攻击 */
        b->y += 120.0f * (float)dt;
        if (b->y > 118.0f) b->y = 118.0f;
        return;
    }
    b->x += b->vx * (float)dt;
    if (b->x < 92.0f) { b->x = 92.0f; b->vx = -b->vx; }
    if (b->x > FIELD_W - 92.0f) { b->x = FIELD_W - 92.0f; b->vx = -b->vx; }
    if (b->hitT > 0) b->hitT -= dt;

    if (t < b->nextFire) return;
    b->shots++;
    n = (b->shots % 3 == 0) ? 5 : 3;      /* 每三波来一轮五连发 */
    for (i = 0; i < n; i++) {
        off = ((float)i - (float)(n - 1) / 2.0f) * 52.0f;
        bx = b->x + off;
        by = b->y + 46.0f;
        vy = 145.0f + (float)g.level * 6.0f;
        addEBullet(bx, by, vy);
    }
    interval = 2.1 - (double)(g.level - 1) * 0.12;
    if (interval < 1.1) interval = 1.1;
    b->nextFire = t + interval;
}

static void hurtPlayer(void)
{
    double t = (double)GetTickCount() / 1000.0;
    if (t < g.invulnUntil) return;
    /* 护盾生效时免疫，只弹一下火花 */
    if (t < g.shieldUntil) {
        boom(g.x, g.y, RGB(180, 235, 255), 8);
        g.invulnUntil = t + 0.2;
        return;
    }
    boom(g.x, g.y, RGB(120, 210, 255), 22);
    g.lives--;
    g.invulnUntil = t + 1.8;
    g.flashT = 0.4;
    if (g.lives <= 0) {
        g.lives = 0;
        g.state = ST_OVER;
        saveBest();
        saveCoins();
    }
}

static void update(double dt)
{
    double t = (double)GetTickCount() / 1000.0;
    int i, j;
    double spawnInterval;

    /* 暂停时整幅画面冻结，恢复后不会出现跳帧 */
    if (g.state == ST_PAUSE) return;

    /* 云朵飘动 */
    for (i = 0; i < 8; i++) {
        clouds[i].x += clouds[i].speed * (float)dt;
        if (clouds[i].x > WIN_W + 80) clouds[i].x = -100 - clouds[i].size;
    }

    /* 粒子 */
    for (i = 0; i < MAX_PARTICLES; i++) {
        if (g.parts[i].life <= 0) continue;
        g.parts[i].x += g.parts[i].vx * (float)dt;
        g.parts[i].y += g.parts[i].vy * (float)dt;
        g.parts[i].vy += 220.0f * (float)dt;
        g.parts[i].vx *= 0.97f;
        g.parts[i].life--;
    }

    if (g.flashT > 0) g.flashT -= dt;
    if (g.bombFlash > 0) g.bombFlash -= dt;
    if (g.bossIntroT > 0) g.bossIntroT -= dt;
    if (g.bossRewardT > 0) g.bossRewardT -= dt;
    if (g.state != ST_PLAY) return;
    if (dt > 0.06) dt = 0.06;

    /* 键盘持续移动 */
    if (GetKeyState(VK_LEFT) & 0x8000 || GetKeyState('A') & 0x8000) g.x -= 300.0f * (float)dt;
    if (GetKeyState(VK_RIGHT) & 0x8000 || GetKeyState('D') & 0x8000) g.x += 300.0f * (float)dt;
    if (GetKeyState(VK_UP) & 0x8000 || GetKeyState('W') & 0x8000) g.y -= 260.0f * (float)dt;
    if (GetKeyState(VK_DOWN) & 0x8000 || GetKeyState('S') & 0x8000) g.y += 260.0f * (float)dt;
    if (g.x < 24) g.x = 24;
    if (g.x > FIELD_W - 24) g.x = FIELD_W - 24;
    if (g.y < 34) g.y = 34;
    if (g.y > FIELD_H - 112) g.y = FIELD_H - 112;
    if (GetKeyState(VK_SPACE) & 0x8000) shoot();
    wingFire(t);

    /* 子弹 */
    for (i = 0; i < MAX_BULLETS; i++) {
        if (!g.bullets[i].alive) continue;
        g.bullets[i].y += g.bullets[i].vy * (float)dt;
        if (g.bullets[i].y < -20) g.bullets[i].alive = 0;
    }

    /* 敌机 */
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (!g.enemies[i].alive) continue;
        g.enemies[i].y += g.enemies[i].speed * (float)dt;
        if (g.enemies[i].hitT > 0) g.enemies[i].hitT -= dt;
        /* 漏掉的敌机直接飞走，不扣生命 */
        if (g.enemies[i].y > FIELD_H + 30) {
            g.enemies[i].alive = 0;
            continue;
        }
        if (g.enemies[i].y > 30 && g.enemies[i].y < FIELD_H - 140 && t >= g.enemies[i].nextFire)
            enemyFire(i, t);
        {
            float dx = g.enemies[i].x - g.x, dy = g.enemies[i].y - g.y;
            if (dx * dx + dy * dy < 26 * 26) {
                boom(g.enemies[i].x, g.enemies[i].y, RGB(255, 160, 90), 14);
                g.enemies[i].alive = 0;
                hurtPlayer();
            }
        }
    }

    /* 关底 BOSS：巡航、开火，撞到玩家同样掉血 */
    updateBoss(dt, t);
    if (g.bossSpawned && !g.boss.alive) {
        /* BOSS 已击落：等爆炸演完再结算通关 */
        if (g.bossClearDelay > 0) g.bossClearDelay -= dt;
        else checkLevelClear();
    }
    if (g.boss.alive && g.boss.y > 30.0f) {
        float bdx = g.boss.x - g.x;
        float bdy = g.boss.y - g.y;
        if (bdx * bdx + bdy * bdy < 58.0f * 58.0f) hurtPlayer();
    }

    /* 敌机子弹：飞出战场即消失，只有打中玩家才掉血 */
    for (i = 0; i < MAX_EBULLETS; i++) {
        float dx, dy;
        if (!g.ebullets[i].alive) continue;
        g.ebullets[i].x += g.ebullets[i].vx * (float)dt;
        g.ebullets[i].y += g.ebullets[i].vy * (float)dt;
        if (g.ebullets[i].y > FIELD_H + 20 || g.ebullets[i].x < -20 || g.ebullets[i].x > FIELD_W + 20) {
            g.ebullets[i].alive = 0;
            continue;
        }
        dx = g.ebullets[i].x - g.x;
        dy = g.ebullets[i].y - g.y;
        if (dx * dx + dy * dy < 22 * 22) {
            g.ebullets[i].alive = 0;
            hurtPlayer();
        }
    }

    /* 碰撞：子弹 vs 敌机 */
    for (i = 0; i < MAX_BULLETS; i++) {
        if (!g.bullets[i].alive) continue;
        for (j = 0; j < MAX_ENEMIES; j++) {
            float dx, dy;
            if (!g.enemies[j].alive) continue;
            dx = g.bullets[i].x - g.enemies[j].x;
            dy = g.bullets[i].y - g.enemies[j].y;
            if (dx * dx + dy * dy < 20 * 20) {
                g.bullets[i].alive = 0;
                g.enemies[j].hp--;
                g.enemies[j].hitT = 0.12;
                if (g.enemies[j].hp <= 0) {
                    COLORREF c = g.enemies[j].type == 2 ? RGB(255, 196, 60) :
                                 (g.enemies[j].type == 1 ? RGB(210, 130, 240) : RGB(255, 130, 110));
                    boom(g.enemies[j].x, g.enemies[j].y, c, 18);
                    g.enemies[j].alive = 0;
                    g.score += g.enemies[j].type == 2 ? 30 : (g.enemies[j].type == 1 ? 20 : 10);
                    g.kills++;
                    g.levelKills++;
                    /* 击落得金币：普通 1 · 快速 2 · 装甲 3 */
                    {
                        int coin = g.enemies[j].type == 2 ? 3 : (g.enemies[j].type == 1 ? 2 : 1);
                        g_coins += coin;
                        g.coinsEarned += coin;
                    }
                    /* 达成目标：清场并进入通关面板，必须确认才进下一关 */
                    checkLevelClear();
                }
                break;
            }
        }
    }

    /* 碰撞：子弹 vs BOSS（打空血条才算击败） */
    if (g.boss.alive) {
        for (i = 0; i < MAX_BULLETS; i++) {
            float bdx, bdy;
            if (!g.bullets[i].alive) continue;
            bdx = g.bullets[i].x - g.boss.x;
            bdy = g.bullets[i].y - g.boss.y;
            if (bdx > -64.0f && bdx < 64.0f && bdy > -52.0f && bdy < 52.0f) {
                g.bullets[i].alive = 0;
                g.boss.hp--;
                g.boss.hitT = 0.1;
                if (g.boss.hp <= 0) {
                    killBoss();
                    break;
                }
            }
        }
    }

    if (g.state != ST_PLAY || g.bossSpawned) return;   /* BOSS 阶段不再刷杂兵 */

    /* 生成敌机：关卡越高越密集，第 2 关起可能双机齐飞 */
    spawnInterval = 0.95 - (double)(g.level - 1) * 0.08;
    if (spawnInterval < 0.30) spawnInterval = 0.30;
    if (t - g.lastSpawn >= spawnInterval) {
        g.lastSpawn = t;
        spawnEnemy();
        if (g.level >= 2 && rand() % 100 < 15 + (g.level - 2) * 8) spawnEnemy();
    }
}

/* ---------------- 卡通绘制 ---------------- */

static void drawCloud(HDC hdc, int x, int y, int s)
{
    COLORREF white = RGB(255, 255, 255);
    COLORREF shade = RGB(224, 240, 252);
    ellipseFill(hdc, x - s, y, x + s, y + s / 2 + 12, shade, shade);
    ellipseFill(hdc, x - s, y - 10, x + s / 2 + 6, y + s / 2, white, white);
    ellipseFill(hdc, x - s / 2, y - s / 2, x + s / 2 + 8, y + s / 2, white, white);
    ellipseFill(hdc, x + s / 3, y - s / 3, x + s + 4, y + s / 2, white, white);
}

static void drawBackground(HDC hdc)
{
    int y, i, x;
    COLORREF top = RGB(126, 202, 246), bot = RGB(222, 244, 255);
    /* 天空渐变 */
    for (y = 0; y < WIN_H; y++) {
        int k = y * 255 / WIN_H;
        COLORREF c = RGB((int)(126 + (222 - 126) * k / 255.0),
                         (int)(202 + (244 - 202) * k / 255.0),
                         (int)(246 + (255 - 246) * k / 255.0));
        fillRect(hdc, 0, y, WIN_W, 1, c);
    }
    (void)top; (void)bot;

    /* 云 */
    for (i = 0; i < 8; i++)
        drawCloud(hdc, (int)clouds[i].x, (int)clouds[i].y, (int)clouds[i].size);

    /* 远山 */
    {
        int gy = WIN_H - 40;
        POINT m1[4] = {{0, gy}, {WIN_W / 9, gy - 120}, {WIN_W / 3, gy}, {WIN_W / 3, WIN_H}};
        POINT m2[4] = {{WIN_W / 4, gy}, {WIN_W * 7 / 20, gy - 110}, {WIN_W / 2, gy}, {WIN_W / 2, WIN_H}};
        POINT m3[4] = {{WIN_W * 7 / 16, gy}, {WIN_W * 5 / 8, gy - 130}, {WIN_W, gy}, {WIN_W, WIN_H}};
        poly(hdc, m1, 4, RGB(168, 214, 172), RGB(168, 214, 172));
        poly(hdc, m2, 4, RGB(150, 204, 158), RGB(150, 204, 158));
        poly(hdc, m3, 4, RGB(168, 214, 172), RGB(168, 214, 172));
    }
    /* 草地 */
    fillRect(hdc, 0, WIN_H - 40, WIN_W, 40, RGB(126, 198, 96));
    for (x = 0; x < WIN_W; x += 18)
        lineTo(hdc, x, WIN_H - 40, x + 6, WIN_H - 48, RGB(96, 170, 78), 2);
}

static void drawPlayer(HDC hdc, int x, int y)
{
    double t = (double)GetTickCount() / 1000.0;
    POINT wl[4], wr[4], tail[4];

    /* 影子 */
    ellipseFill(hdc, x - 30, y + 22, x + 30, y + 38, RGB(150, 205, 225), RGB(150, 205, 225));

    /* 机翼 */
    wl[0].x = x - 4; wl[0].y = y - 8;
    wl[1].x = x - 38; wl[1].y = y + 12;
    wl[2].x = x - 34; wl[2].y = y + 22;
    wl[3].x = x - 4; wl[3].y = y + 10;
    wr[0].x = x + 4; wr[0].y = y - 8;
    wr[1].x = x + 38; wr[1].y = y + 12;
    wr[2].x = x + 34; wr[2].y = y + 22;
    wr[3].x = x + 4; wr[3].y = y + 10;
    poly(hdc, wl, 4, RGB(86, 174, 236), C_NAVY);
    poly(hdc, wr, 4, RGB(86, 174, 236), C_NAVY);

    /* 尾翼 */
    tail[0].x = x - 5; tail[0].y = y + 12;
    tail[1].x = x - 20; tail[1].y = y + 30;
    tail[2].x = x + 20; tail[2].y = y + 30;
    tail[3].x = x + 5; tail[3].y = y + 12;
    poly(hdc, tail, 4, RGB(120, 200, 250), C_NAVY);

    /* 机身 */
    ellipseFill(hdc, x - 17, y - 30, x + 17, y + 28, RGB(245, 252, 255), C_NAVY);
    /* 机头螺旋桨 */
    ellipseFill(hdc, x - 6, y - 36, x + 6, y - 24, RGB(255, 206, 72), C_NAVY);
    lineTo(hdc, x - 15, y - 30, x + 15, y - 30, RGB(90, 110, 130), 3);
    if (((int)(t * 12)) % 2 == 0)
        lineTo(hdc, x - 15, y - 27, x + 15, y - 27, RGB(150, 168, 184), 2);

    /* 座舱 */
    ellipseFill(hdc, x - 11, y - 14, x + 11, y + 4, RGB(154, 224, 250), C_NAVY);
    ellipseFill(hdc, x - 7, y - 11, x - 2, y - 5, RGB(255, 255, 255), RGB(255, 255, 255));

    /* 笑脸 */
    ellipseFill(hdc, x - 7, y + 8, x - 4, y + 11, C_NAVY, C_NAVY);
    ellipseFill(hdc, x + 4, y + 8, x + 7, y + 11, C_NAVY, C_NAVY);
    {
        HPEN pen = CreatePen(PS_SOLID, 2, C_NAVY);
        HPEN op = (HPEN)SelectObject(hdc, pen);
        Arc(hdc, x - 8, y + 9, x + 8, y + 21, x - 6, y + 15, x + 6, y + 15);
        SelectObject(hdc, op); DeleteObject(pen);
    }
}

static void drawEnemy(HDC hdc, Enemy *e)
{
    int x = (int)e->x, y = (int)e->y;
    COLORREF body = e->type == 0 ? RGB(255, 132, 120) :
                    (e->type == 1 ? RGB(205, 136, 240) : RGB(255, 196, 76));
    COLORREF dark = e->type == 0 ? RGB(206, 74, 74) :
                    (e->type == 1 ? RGB(148, 82, 190) : RGB(186, 132, 36));
    POINT wl[4], wr[4];
    if (e->hitT > 0) body = RGB(255, 255, 255);
    if (e->type == 1) {
        /* 快速小飞机：尖尖的 */
        wl[0].x = x - 3; wl[0].y = y + 6;
        wl[1].x = x - 28; wl[1].y = y - 12;
        wl[2].x = x - 24; wl[2].y = y - 2;
        wl[3].x = x - 3; wl[3].y = y + 12;
        wr[0].x = x + 3; wr[0].y = y + 6;
        wr[1].x = x + 28; wr[1].y = y - 12;
        wr[2].x = x + 24; wr[2].y = y - 2;
        wr[3].x = x + 3; wr[3].y = y + 12;
    } else {
        wl[0].x = x - 4; wl[0].y = y + 6;
        wl[1].x = x - 32; wl[1].y = y - 10;
        wl[2].x = x - 30; wl[2].y = y + 2;
        wl[3].x = x - 4; wl[3].y = y + 14;
        wr[0].x = x + 4; wr[0].y = y + 6;
        wr[1].x = x + 32; wr[1].y = y - 10;
        wr[2].x = x + 30; wr[2].y = y + 2;
        wr[3].x = x + 4; wr[3].y = y + 14;
    }
    poly(hdc, wl, 4, dark, C_NAVY);
    poly(hdc, wr, 4, dark, C_NAVY);
    ellipseFill(hdc, x - 16, y - 22, x + 16, y + 22, body, C_NAVY);
    /* 护甲板 */
    if (e->type == 2) {
        lineTo(hdc, x - 13, y - 4, x + 13, y - 4, RGB(120, 88, 30), 3);
        if (e->hp == 2) ellipseFill(hdc, x - 5, y - 16, x + 5, y - 8, RGB(255, 245, 190), C_NAVY);
    }
    /* 生气的小眼睛 */
    ellipseFill(hdc, x - 10, y - 6, x - 4, y, RGB(255, 255, 255), C_NAVY);
    ellipseFill(hdc, x + 4, y - 6, x + 10, y, RGB(255, 255, 255), C_NAVY);
    ellipseFill(hdc, x - 8, y - 4, x - 5, y - 1, C_NAVY, C_NAVY);
    ellipseFill(hdc, x + 5, y - 4, x + 8, y - 1, C_NAVY, C_NAVY);
    lineTo(hdc, x - 11, y - 9, x - 4, y - 6, C_NAVY, 2);
    lineTo(hdc, x + 11, y - 9, x + 4, y - 6, C_NAVY, 2);
    /* 撅嘴 */
    lineTo(hdc, x - 5, y + 10, x + 5, y + 10, C_NAVY, 2);
}

/* 关底 BOSS：比杂兵大一圈的红色大飞机，朝下进攻 */
static void drawBoss(HDC hdc, Boss *b)
{
    double tt = (double)GetTickCount() / 1000.0;
    int x = (int)b->x, y = (int)b->y;
    int hit = (b->hitT > 0);
    COLORREF body = hit ? RGB(255, 255, 255) : RGB(228, 98, 110);
    COLORREF dark = hit ? RGB(255, 216, 216) : RGB(160, 52, 70);
    COLORREF glass = RGB(152, 214, 246);
    POINT wl[5], wr[5], fin[4];

    /* 影子 */
    ellipseFill(hdc, x - 58, y + 44, x + 58, y + 64, RGB(152, 206, 226), RGB(152, 206, 226));

    /* 巨大的后掠翼 */
    wl[0].x = x - 10; wl[0].y = y - 20;
    wl[1].x = x - 72; wl[1].y = y + 2;
    wl[2].x = x - 66; wl[2].y = y + 22;
    wl[3].x = x - 32; wl[3].y = y + 26;
    wl[4].x = x - 10; wl[4].y = y + 14;
    wr[0].x = x + 10; wr[0].y = y - 20;
    wr[1].x = x + 72; wr[1].y = y + 2;
    wr[2].x = x + 66; wr[2].y = y + 22;
    wr[3].x = x + 32; wr[3].y = y + 26;
    wr[4].x = x + 10; wr[4].y = y + 14;
    poly(hdc, wl, 5, dark, C_NAVY);
    poly(hdc, wr, 5, dark, C_NAVY);

    /* 尾翼 */
    fin[0].x = x - 12; fin[0].y = y - 34;
    fin[1].x = x - 34; fin[1].y = y - 50;
    fin[2].x = x + 34; fin[2].y = y - 50;
    fin[3].x = x + 12; fin[3].y = y - 34;
    poly(hdc, fin, 4, dark, C_NAVY);

    /* 两侧推进器 */
    ellipseFill(hdc, x - 68, y - 10, x - 36, y + 26, dark, C_NAVY);
    ellipseFill(hdc, x + 36, y - 10, x + 68, y + 26, dark, C_NAVY);
    if (((int)(tt * 10)) % 2 == 0) {
        ellipseFill(hdc, x - 62, y + 16, x - 42, y + 30, RGB(255, 214, 120), RGB(240, 160, 60));
        ellipseFill(hdc, x + 42, y + 16, x + 62, y + 30, RGB(255, 214, 120), RGB(240, 160, 60));
    } else {
        ellipseFill(hdc, x - 60, y + 16, x - 44, y + 28, RGB(255, 238, 170), RGB(240, 160, 60));
        ellipseFill(hdc, x + 44, y + 16, x + 60, y + 28, RGB(255, 238, 170), RGB(240, 160, 60));
    }

    /* 机身 */
    ellipseFill(hdc, x - 36, y - 52, x + 36, y + 48, body, C_NAVY);
    /* 机身铆钉带 */
    lineTo(hdc, x - 30, y + 18, x + 30, y + 18, RGB(150, 52, 66), 3);

    /* 机头下方的双炮管 */
    lineTo(hdc, x - 20, y + 34, x - 20, y + 50, RGB(90, 60, 70), 7);
    lineTo(hdc, x + 20, y + 34, x + 20, y + 50, RGB(90, 60, 70), 7);
    ellipseFill(hdc, x - 25, y + 46, x - 15, y + 54, RGB(70, 46, 56), C_NAVY);
    ellipseFill(hdc, x + 15, y + 46, x + 25, y + 54, RGB(70, 46, 56), C_NAVY);

    /* 座舱 */
    ellipseFill(hdc, x - 24, y - 34, x + 24, y + 2, glass, C_NAVY);
    ellipseFill(hdc, x - 16, y - 28, x - 4, y - 14, RGB(255, 255, 255), RGB(255, 255, 255));

    /* 凶狠的眼睛 */
    ellipseFill(hdc, x - 20, y + 4, x - 8, y + 16, RGB(255, 246, 200), C_NAVY);
    ellipseFill(hdc, x + 8, y + 4, x + 20, y + 16, RGB(255, 246, 200), C_NAVY);
    ellipseFill(hdc, x - 17, y + 7, x - 11, y + 13, C_NAVY, C_NAVY);
    ellipseFill(hdc, x + 11, y + 7, x + 17, y + 13, C_NAVY, C_NAVY);
    lineTo(hdc, x - 24, y - 1, x - 8, y + 3, C_NAVY, 3);
    lineTo(hdc, x + 24, y - 1, x + 8, y + 3, C_NAVY, 3);

    /* 咧开的大嘴 + 尖牙 */
    {
        HPEN pen = CreatePen(PS_SOLID, 3, C_NAVY);
        HPEN op = (HPEN)SelectObject(hdc, pen);
        Arc(hdc, x - 18, y + 14, x + 18, y + 34, x - 16, y + 20, x + 16, y + 20);
        SelectObject(hdc, op);
        DeleteObject(pen);
    }
    lineTo(hdc, x - 12, y + 22, x - 12, y + 28, RGB(255, 255, 255), 3);
    lineTo(hdc, x - 2, y + 24, x - 2, y + 30, RGB(255, 255, 255), 3);
    lineTo(hdc, x + 8, y + 24, x + 8, y + 30, RGB(255, 255, 255), 3);
}

/* BOSS 血条：横在战场顶部，血量随受击缩短 */
static void drawBossHud(HDC hdc)
{
    int bw = 560, bh = 16;
    int bx = FIELD_X + (FIELD_W - bw) / 2;
    int by = FIELD_Y + 34;
    int w;
    COLORREF c;

    if (!g.bossSpawned) return;
    if (!g.boss.alive && g.bossRewardT <= 0) return;

    roundBox(hdc, bx - 16, by - 26, bw + 32, 52, RGB(255, 247, 247), RGB(220, 156, 156), 12);
    textOut(hdc, bx, by - 23, "关底 BOSS", hFontSmall, RGB(198, 58, 72));

    fillRect(hdc, bx, by, bw, bh, RGB(238, 216, 216));
    if (g.boss.hp > 0 && g.boss.maxHp > 0) {
        w = bw * g.boss.hp / g.boss.maxHp;
        c = (g.boss.hp * 3 > g.boss.maxHp) ? RGB(232, 88, 96) : RGB(248, 150, 70);
        fillRect(hdc, bx, by, w, bh, c);
        fillRect(hdc, bx, by, w, bh / 3, RGB(255, 186, 186));
    }
}

static void drawBullet(HDC hdc, Bullet *b)
{
    int x = (int)b->x, y = (int)b->y;
    lineTo(hdc, x, y + 16, x, y + 4, RGB(255, 238, 150), 3);
    ellipseFill(hdc, x - 6, y - 9, x + 6, y + 6, RGB(255, 246, 190), RGB(244, 176, 42));
    ellipseFill(hdc, x - 3, y - 6, x + 1, y - 2, RGB(255, 255, 255), RGB(255, 255, 255));
}

/* 敌方子弹：橙红色，尾迹朝上，和玩家的黄色子弹区分 */
static void drawEnemyBullet(HDC hdc, Bullet *b)
{
    int x = (int)b->x, y = (int)b->y;
    lineTo(hdc, x, y - 16, x, y - 4, RGB(255, 176, 140), 3);
    ellipseFill(hdc, x - 7, y - 7, x + 7, y + 7, RGB(255, 108, 96), RGB(178, 38, 52));
    ellipseFill(hdc, x - 3, y - 3, x + 1, y + 1, RGB(255, 235, 220), RGB(255, 235, 220));
}

static void drawParticles(HDC hdc)
{
    int i;
    for (i = 0; i < MAX_PARTICLES; i++) {
        if (g.parts[i].life <= 0) continue;
        int a = g.parts[i].life * 255 / g.parts[i].maxLife;
        int r = g.parts[i].size * g.parts[i].life / g.parts[i].maxLife + 1;
        COLORREF c = g.parts[i].color;
        /* 用颜色渐淡模拟淡出 */
        if (a < 120) c = RGB(GetRValue(c) * a / 120 + 180 * (120 - a) / 120,
                             GetGValue(c) * a / 120 + 225 * (120 - a) / 120,
                             GetBValue(c) * a / 120 + 240 * (120 - a) / 120);
        ellipseFill(hdc, (int)g.parts[i].x - r, (int)g.parts[i].y - r,
                    (int)g.parts[i].x + r, (int)g.parts[i].y + r, c, c);
    }
}

static void drawHeart(HDC hdc, int x, int y, int full)
{
    POINT p[12];
    COLORREF c = full ? RGB(240, 86, 104) : RGB(210, 218, 228);
    p[0].x = x;      p[0].y = y + 15;
    p[1].x = x - 15; p[1].y = y + 2;
    p[2].x = x - 15; p[2].y = y - 6;
    p[3].x = x - 9;  p[3].y = y - 12;
    p[4].x = x - 2;  p[4].y = y - 9;
    p[5].x = x;      p[5].y = y - 5;
    p[6].x = x + 2;  p[6].y = y - 9;
    p[7].x = x + 9;  p[7].y = y - 12;
    p[8].x = x + 15; p[8].y = y - 6;
    p[9].x = x + 15; p[9].y = y + 2;
    p[10].x = x;     p[10].y = y + 15;
    poly(hdc, p, 11, c, RGB(170, 60, 80));
}

static void drawButton(HDC hdc, int x, int y, int w, int h, const char *label, int hover)
{
    roundBox(hdc, x, y + (hover ? 2 : 4), w, h, RGB(188, 132, 42), RGB(150, 100, 30), 16);
    roundBox(hdc, x, y + (hover ? 0 : 2), w, h,
             hover ? RGB(255, 216, 110) : RGB(255, 196, 72), RGB(180, 124, 30), 16);
    textOut(hdc, x + w / 2 - 42, y + 14, label, hFontMid, RGB(110, 66, 20));
}

static int inRect(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && x <= rx + rw && y >= ry && y <= ry + rh;
}

/* 护盾泡：生效时套在飞机外面 */
static void drawShield(HDC hdc)
{
    double t = (double)GetTickCount() / 1000.0;
    int cx, cy, r;
    if (t >= g.shieldUntil) return;
    cx = FIELD_X + (int)g.x;
    cy = FIELD_Y + (int)g.y;
    r = 46 + (int)((t * 6.0 - (int)(t * 6.0)) * 3.0);   /* 轻微呼吸 */
    ellipseFill(hdc, cx - r, cy - r, cx + r, cy + r, RGB(150, 220, 255), RGB(84, 168, 228));
    /* 两处高光，画出球面感 */
    ellipseFill(hdc, cx - 26, cy - 32, cx - 12, cy - 20, RGB(228, 248, 255), RGB(228, 248, 255));
    ellipseFill(hdc, cx + 6, cy + 8, cx + 16, cy + 16, RGB(228, 248, 255), RGB(228, 248, 255));
}

/* 战场底部的技能快捷栏：护盾（X）与炸弹（B） */
static void drawSkillBar(HDC hdc)
{
    double t = (double)GetTickCount() / 1000.0;
    char buf[80];
    int y = FIELD_Y + FIELD_H - 82;   /* 放在战场内下部，小屏也不会被裁掉 */
    int lv, avail;
    COLORREF bg, bd, fg;

    /* 护盾 */
    lv = g_skill[SK_SHIELD];
    if (lv <= 0) {
        bg = RGB(232, 236, 240); bd = RGB(206, 214, 222); fg = RGB(150, 160, 172);
        sprintf(buf, "护盾未解锁");
    } else if (t < g.shieldUntil) {
        bg = RGB(212, 245, 224); bd = RGB(120, 200, 150); fg = RGB(40, 130, 80);
        sprintf(buf, "护盾生效 %.1fs", g.shieldUntil - t);
    } else if (t < g.shieldReadyAt) {
        bg = RGB(232, 236, 240); bd = RGB(206, 214, 222); fg = RGB(140, 150, 164);
        sprintf(buf, "护盾冷却 %d 秒", (int)(g.shieldReadyAt - t) + 1);
    } else {
        bg = RGB(208, 238, 255); bd = RGB(110, 180, 226); fg = RGB(30, 100, 150);
        sprintf(buf, "X  护盾就绪");
    }
    roundBox(hdc, 40, y, 232, 36, bg, bd, 16);
    textOut(hdc, 62, y + 8, buf, hFontSmall, fg);

    /* 炸弹 */
    lv = g_skill[SK_BOMB];
    avail = g.bombsLeft;
    if (lv <= 0) {
        bg = RGB(232, 236, 240); bd = RGB(206, 214, 222); fg = RGB(150, 160, 172);
        sprintf(buf, "炸弹未解锁");
    } else if (avail <= 0) {
        bg = RGB(232, 236, 240); bd = RGB(206, 214, 222); fg = RGB(140, 150, 164);
        sprintf(buf, "炸弹本关用完");
    } else {
        bg = RGB(255, 238, 208); bd = RGB(226, 168, 74); fg = RGB(150, 92, 20);
        sprintf(buf, "B  炸弹 ×%d", avail);
    }
    roundBox(hdc, 296, y, 232, 36, bg, bd, 16);
    textOut(hdc, 318, y + 8, buf, hFontSmall, fg);

    /* 金币提示 */
    sprintf(buf, "金币 %d", g_coins);
    roundBox(hdc, 548, y, 152, 36, RGB(255, 248, 226), RGB(224, 190, 116), 16);
    textOut(hdc, 570, y + 8, buf, hFontSmall, RGB(172, 122, 30));
}

static void drawPauseButton(HDC hdc, int paused, int hover)
{
    int x = PAUSE_BTN_X, y = PAUSE_BTN_Y, w = PAUSE_BTN_W, h = PAUSE_BTN_H;
    COLORREF bg = hover ? RGB(255, 228, 158) : RGB(250, 242, 220);
    COLORREF bd = hover ? RGB(232, 160, 40) : RGB(212, 194, 158);
    COLORREF fg = RGB(196, 138, 36);

    roundBox(hdc, x, y, w, h, bg, bd, 14);
    if (paused) {
        POINT t[3];
        t[0].x = x + 24; t[0].y = y + 11;
        t[1].x = x + 40; t[1].y = y + 19;
        t[2].x = x + 24; t[2].y = y + 27;
        poly(hdc, t, 3, fg, fg);
        textOut(hdc, x + 50, y + 8, "继续", hFontSmall, RGB(140, 96, 30));
    } else {
        fillRect(hdc, x + 24, y + 11, 5, 16, fg);
        fillRect(hdc, x + 34, y + 11, 5, 16, fg);
        textOut(hdc, x + 50, y + 8, "暂停", hFontSmall, RGB(140, 96, 30));
    }
}

/* 选关用的左右箭头按钮 */
static void drawArrow(HDC hdc, int x, int y, int s, int dir, int hover)
{
    POINT p[3];
    roundBox(hdc, x, y, s, s,
             hover ? RGB(255, 236, 190) : RGB(255, 252, 242),
             hover ? RGB(232, 160, 40) : RGB(186, 208, 226), 12);
    if (dir < 0) {
        p[0].x = x + s * 2 / 3; p[0].y = y + s / 4;
        p[1].x = x + s / 3;     p[1].y = y + s / 2;
        p[2].x = x + s * 2 / 3; p[2].y = y + s * 3 / 4;
    } else {
        p[0].x = x + s / 3;     p[0].y = y + s / 4;
        p[1].x = x + s * 2 / 3; p[1].y = y + s / 2;
        p[2].x = x + s / 3;     p[2].y = y + s * 3 / 4;
    }
    poly(hdc, p, 3, C_NAVY, C_NAVY);
}

static void drawOverlay(HDC hdc)
{
    char buf[80];
    int cx = FIELD_X + FIELD_W / 2;
    int bx = cx - 100;
    int by = FIELD_Y + 350;
    int arrowY = FIELD_Y + 222;
    int hover;
    int i, k;

    /* 暂停时只盖一块居中面板，四周仍能看到战场形势 */
    if (g.state == ST_PAUSE)
        roundBox(hdc, cx - 300, FIELD_Y + 108, 600, 336, RGB(252, 253, 255), RGB(190, 212, 232), 24);
    else
        fillRect(hdc, FIELD_X + 14, FIELD_Y + 14, FIELD_W - 28, FIELD_H - 28, RGB(245, 250, 255));

    if (g.state == ST_START) {
        textOut(hdc, cx - 258, FIELD_Y + 74, "天天打飞机 · 卡通版", hFontTitle, C_NAVY);
        textOut(hdc, cx - 145, FIELD_Y + 142, "开着小飞机，把坏蛋赶跑！", hFontMid, RGB(90, 130, 170));

        /* 选关区 */
        textOut(hdc, cx - 72, FIELD_Y + 188, "选择起始关卡", hFontSmall, RGB(110, 140, 170));
        hover = inRect(mouseX, mouseY, cx - 178, arrowY, 56, 56);
        drawArrow(hdc, cx - 178, arrowY, 56, -1, hover);
        hover = inRect(mouseX, mouseY, cx + 122, arrowY, 56, 56);
        drawArrow(hdc, cx + 122, arrowY, 56, 1, hover);
        sprintf(buf, "第 %d 关", g_selected);
        textOut(hdc, cx - (g_selected >= 10 ? 76 : 56), arrowY + 8, buf, hFontBig, RGB(80, 150, 190));
        sprintf(buf, "已解锁到第 %d 关", g_maxLevel);
        textOut(hdc, cx - 74, arrowY + 72, buf, hFontSmall, RGB(160, 120, 70));

        hover = inRect(mouseX, mouseY, bx, by, 200, 56);
        drawButton(hdc, bx, by, 200, 56, "开始游戏", hover);
        hover = inRect(mouseX, mouseY, bx + 230, by, 200, 56);
        drawButton(hdc, bx + 230, by, 200, 56, "技能升级", hover);

        textOut(hdc, cx - 360, FIELD_Y + 444,
                "A/D 或 ←/→ 移动 · W/S 或 ↑/↓ 上下 · 空格射击 · P 暂停", hFontSmall, RGB(96, 126, 156));
        textOut(hdc, cx - 250, FIELD_Y + 478,
                "V 普通 10 分   v 快速 20 分   W 装甲 30 分", hFontSmall, RGB(150, 100, 90));
        textOut(hdc, cx - 385, FIELD_Y + 512,
                "注意：只有被敌机撞到或被红色子弹打中才会掉血，放跑敌机没关系", hFontSmall, RGB(178, 88, 88));
        textOut(hdc, cx - 340, FIELD_Y + 546,
                "击落敌机得金币，金币可在“技能升级”里永久变强", hFontSmall, RGB(96, 126, 156));
        textOut(hdc, cx - 232, FIELD_Y + 580,
                "每关打满目标后会出现关底 BOSS，击败它金币翻倍！", hFontSmall, RGB(200, 116, 36));
    } else if (g.state == ST_UPGRADE) {
        textOut(hdc, cx - 96, FIELD_Y + 40, "技能升级", hFontTitle, C_NAVY);
        sprintf(buf, "金币 %d", g_coins);
        textOut(hdc, FIELD_X + 690, FIELD_Y + 46, buf, hFontMid, RGB(196, 146, 44));
        for (i = 0; i < SK_COUNT; i++) {
            int ry = FIELD_Y + 132 + i * 84;
            int lv = g_skill[i], mx = skillMaxLv(i);
            roundBox(hdc, FIELD_X + 46, ry, FIELD_W - 92, 78,
                     RGB(252, 253, 255), RGB(208, 224, 238), 16);
            textOut(hdc, FIELD_X + 68, ry + 8, skillName(i), hFontMid, C_NAVY);
            for (k = 0; k < mx; k++)
                ellipseFill(hdc, FIELD_X + 184 + k * 20, ry + 14,
                            FIELD_X + 196 + k * 20, ry + 26,
                            k < lv ? RGB(246, 186, 52) : RGB(228, 234, 240),
                            k < lv ? RGB(212, 148, 28) : RGB(212, 220, 228));
            textOut(hdc, FIELD_X + 68, ry + 44, skillDesc(i, lv), hFontSmall, RGB(112, 138, 164));

            if (lv >= mx) {
                roundBox(hdc, FIELD_X + 664, ry + 17, 170, 44, RGB(232, 236, 240), RGB(206, 214, 222), 14);
                textOut(hdc, FIELD_X + 714, ry + 27, "已满级", hFontSmall, RGB(150, 160, 172));
            } else {
                int cost = skillCost(i, lv);
                int can = g_coins >= cost;
                int hov = inRect(mouseX, mouseY, FIELD_X + 664, ry + 17, 170, 44);
                roundBox(hdc, FIELD_X + 664, ry + 17 + (hov ? 1 : 3), 170, 44,
                         RGB(188, 132, 42), RGB(150, 100, 30), 14);
                roundBox(hdc, FIELD_X + 664, ry + 17 + (hov ? 0 : 1), 170, 44,
                         can ? (hov ? RGB(255, 226, 130) : RGB(255, 206, 90)) : RGB(242, 240, 232),
                         can ? RGB(190, 138, 36) : RGB(212, 206, 192), 14);
                sprintf(buf, "升级 %d 金币", cost);
                textOut(hdc, FIELD_X + 690, ry + 27, buf, hFontSmall,
                        can ? RGB(110, 66, 20) : RGB(162, 158, 148));
            }
        }
        hover = inRect(mouseX, mouseY, cx - 90, FIELD_Y + 560, 180, 50);
        drawButton(hdc, cx - 90, FIELD_Y + 560, 180, 50, "返回", hover);
        textOut(hdc, cx - 210, FIELD_Y + 612,
                "金币通关后保留 · 按 U 或 Esc 返回", hFontSmall, RGB(120, 145, 170));
    } else if (g.state == ST_PAUSE) {
        textOut(hdc, cx - 72, FIELD_Y + 150, "暂停中", hFontTitle, C_NAVY);
        textOut(hdc, cx - 104, FIELD_Y + 216, "喝口水，休息一下～", hFontMid, RGB(90, 130, 170));
        textOut(hdc, cx - 140, FIELD_Y + 268, "按 P / Esc / 空格 或点下方按钮继续", hFontSmall, RGB(120, 145, 170));
        hover = inRect(mouseX, mouseY, bx - 110, by, 200, 56);
        drawButton(hdc, bx - 110, by, 200, 56, "继续游戏", hover);
        hover = inRect(mouseX, mouseY, bx + 110, by, 200, 56);
        drawButton(hdc, bx + 110, by, 200, 56, "重新选关", hover);
    } else if (g.state == ST_CLEAR) {
        sprintf(buf, "第 %d 关完成！", g.level);
        textOut(hdc, FIELD_X + 300, FIELD_Y + 118, buf, hFontTitle, C_GREEN);
        sprintf(buf, "本关击落 %d 架 · 累计得分 %d", g.levelKills, g.score);
        textOut(hdc, FIELD_X + 275, FIELD_Y + 186, buf, hFontMid, C_TEXT);
        if (g.bossRewardCoins > 0)
            sprintf(buf, "本局金币 +%d（共 %d）· 关底 BOSS 额外 +%d",
                    g.coinsEarned, g_coins, g.bossRewardCoins);
        else
            sprintf(buf, "本局金币 +%d（共 %d）", g.coinsEarned, g_coins);
        textOut(hdc, FIELD_X + 240, FIELD_Y + 222, buf, hFontSmall, RGB(190, 140, 40));
        sprintf(buf, "已解锁第 %d 关 · 下关目标 %d 架，还有更强的 BOSS 等着",
                g.level + 1, targetForLevel(g.level + 1));
        textOut(hdc, FIELD_X + 205, FIELD_Y + 256, buf, hFontSmall, RGB(110, 140, 170));
        hover = inRect(mouseX, mouseY, bx, by, 200, 56);
        drawButton(hdc, bx, by, 200, 56, "进入下一关", hover);
    } else if (g.state == ST_OVER) {
        textOut(hdc, FIELD_X + 360, FIELD_Y + 118, "游戏结束", hFontTitle, C_RED);
        sprintf(buf, "本局得分：%d    最高分：%d", g.score, g.best);
        textOut(hdc, FIELD_X + 295, FIELD_Y + 186, buf, hFontMid, C_TEXT);
        sprintf(buf, "共击落 %d 架 · 到达第 %d 关", g.kills, g.level);
        textOut(hdc, FIELD_X + 300, FIELD_Y + 224, buf, hFontSmall, RGB(110, 140, 170));
        sprintf(buf, "本局金币 +%d · 金币已保存，可去升级技能", g.coinsEarned);
        textOut(hdc, FIELD_X + 250, FIELD_Y + 260, buf, hFontSmall, RGB(190, 140, 40));
        hover = inRect(mouseX, mouseY, bx - 110, by, 200, 56);
        drawButton(hdc, bx - 110, by, 200, 56, "重新选关", hover);
        hover = inRect(mouseX, mouseY, bx + 110, by, 200, 56);
        drawButton(hdc, bx + 110, by, 200, 56, "退出游戏", hover);
    }
}

static void drawSidebar(HDC hdc)
{
    char buf[80];
    const char *title;
    int i;
    roundBox(hdc, SIDE_X, 24, 220, 740, C_PANEL, RGB(186, 208, 226), 20);
    title = (g.state == ST_START) ? "关卡选择" :
            (g.state == ST_UPGRADE ? "技能升级" : "飞行仪表盘");
    textOut(hdc, SIDE_X + 34, 48, title, hFontBig, C_NAVY);
    lineTo(hdc, SIDE_X + 20, 84, SIDE_X + 200, 84, RGB(200, 216, 230), 2);

    if (g.state == ST_START) {
        textOut(hdc, SIDE_X + 26, 110, "当前选中", hFontSmall, RGB(110, 140, 170));
        sprintf(buf, "第 %d 关", g_selected);
        textOut(hdc, SIDE_X + 26, 138, buf, hFontBig, RGB(80, 150, 190));
        sprintf(buf, "已解锁 %d 关", g_maxLevel);
        textOut(hdc, SIDE_X + 26, 190, buf, hFontMid, C_TEXT);
        lineTo(hdc, SIDE_X + 20, 236, SIDE_X + 200, 236, RGB(200, 216, 230), 2);
        textOut(hdc, SIDE_X + 26, 254, "← →  切换关卡", hFontSmall, RGB(120, 145, 170));
        textOut(hdc, SIDE_X + 26, 284, "空格 / 回车  开始", hFontSmall, RGB(120, 145, 170));
        textOut(hdc, SIDE_X + 26, 314, "鼠标点箭头也可以", hFontSmall, RGB(120, 145, 170));
        if (g_selected >= g_maxLevel)
            textOut(hdc, SIDE_X + 26, 360, "最新关卡", hFontSmall, RGB(200, 140, 50));
        else
            textOut(hdc, SIDE_X + 26, 360, "可挑战已解锁关卡", hFontSmall, RGB(200, 140, 50));
        sprintf(buf, "金币 %d", g_coins);
        textOut(hdc, SIDE_X + 26, 400, buf, hFontMid, RGB(196, 146, 44));
        textOut(hdc, SIDE_X + 26, 440, "按 U 或点按钮升级技能", hFontSmall, RGB(120, 145, 170));
        return;
    }

    if (g.state == ST_UPGRADE) {
        int yy = 214;
        textOut(hdc, SIDE_X + 26, 110, "可用金币", hFontSmall, RGB(110, 140, 170));
        sprintf(buf, "%d", g_coins);
        textOut(hdc, SIDE_X + 26, 134, buf, hFontBig, RGB(200, 150, 44));
        lineTo(hdc, SIDE_X + 20, 182, SIDE_X + 200, 182, RGB(200, 216, 230), 2);
        textOut(hdc, SIDE_X + 26, 196, "已学技能", hFontSmall, RGB(110, 140, 170));
        for (i = 0; i < SK_COUNT; i++) {
            if (g_skill[i] <= 0) continue;
            sprintf(buf, "%s Lv.%d", skillName(i), g_skill[i]);
            textOut(hdc, SIDE_X + 26, yy, buf, hFontSmall, RGB(88, 122, 152));
            yy += 30;
        }
        if (yy == 214) {
            textOut(hdc, SIDE_X + 26, 220, "还没学技能", hFontSmall, RGB(158, 162, 168));
            textOut(hdc, SIDE_X + 26, 248, "先打几关攒金币", hFontSmall, RGB(158, 162, 168));
        }
        return;
    }

    textOut(hdc, SIDE_X + 26, 104, "得分", hFontSmall, RGB(110, 140, 170));
    sprintf(buf, "%d", g.score);
    textOut(hdc, SIDE_X + 26, 128, buf, hFontBig, RGB(230, 140, 40));

    textOut(hdc, SIDE_X + 26, 172, "金币", hFontSmall, RGB(110, 140, 170));
    sprintf(buf, "%d", g_coins);
    textOut(hdc, SIDE_X + 26, 194, buf, hFontMid, RGB(196, 146, 44));
    if (g.coinsEarned > 0) {
        sprintf(buf, "本局 +%d", g.coinsEarned);
        textOut(hdc, SIDE_X + 116, 200, buf, hFontSmall, RGB(172, 150, 100));
    }

    textOut(hdc, SIDE_X + 26, 234, "最高分", hFontSmall, RGB(110, 140, 170));
    sprintf(buf, "%d", g.best);
    textOut(hdc, SIDE_X + 26, 256, buf, hFontMid, C_TEXT);

    textOut(hdc, SIDE_X + 26, 296, "生命", hFontSmall, RGB(110, 140, 170));
    {
        int maxLives = 3 + g_skill[SK_LIFE];
        for (i = 0; i < maxLives; i++) drawHeart(hdc, SIDE_X + 32 + i * 38, 334, i < g.lives);
    }

    lineTo(hdc, SIDE_X + 20, 366, SIDE_X + 200, 366, RGB(200, 216, 230), 2);

    textOut(hdc, SIDE_X + 26, 384, "当前关卡", hFontSmall, RGB(110, 140, 170));
    sprintf(buf, "第 %d 关", g.level);
    textOut(hdc, SIDE_X + 26, 406, buf, hFontBig, RGB(80, 150, 190));
    sprintf(buf, "本关 %d / %d 架", g.levelKills, g.levelTarget);
    textOut(hdc, SIDE_X + 26, 444, buf, hFontSmall, RGB(120, 145, 170));

    lineTo(hdc, SIDE_X + 20, 480, SIDE_X + 200, 480, RGB(200, 216, 230), 2);
    textOut(hdc, SIDE_X + 26, 496, "敌机图鉴", hFontMid, C_NAVY);
    textOut(hdc, SIDE_X + 74, 540, "普通 · 10 分", hFontSmall, RGB(180, 90, 80));
    textOut(hdc, SIDE_X + 74, 588, "快速 · 20 分", hFontSmall, RGB(150, 90, 190));
    textOut(hdc, SIDE_X + 74, 636, "装甲 · 30 分", hFontSmall, RGB(190, 140, 40));
    /* 图鉴小图标 */
    {
        Enemy e1, e2, e3;
        memset(&e1, 0, sizeof(e1)); memset(&e2, 0, sizeof(e2)); memset(&e3, 0, sizeof(e3));
        e1.type = 0; e1.x = SIDE_X + 34; e1.y = 546; e1.hp = 1;
        e2.type = 1; e2.x = SIDE_X + 34; e2.y = 594; e2.hp = 1;
        e3.type = 2; e3.x = SIDE_X + 34; e3.y = 642; e3.hp = 2;
        drawEnemy(hdc, &e1); drawEnemy(hdc, &e2); drawEnemy(hdc, &e3);
    }
}

static void drawScene(HDC hdc)
{
    int i;
    drawBackground(hdc);
    drawParticles(hdc);

    if (g.state == ST_PLAY || g.state == ST_PAUSE) {
        for (i = 0; i < MAX_BULLETS; i++) {
            Bullet tmp;
            if (!g.bullets[i].alive) continue;
            tmp = g.bullets[i];
            tmp.x += FIELD_X;
            tmp.y += FIELD_Y;
            drawBullet(hdc, &tmp);
        }
        for (i = 0; i < MAX_EBULLETS; i++) {
            Bullet tmp;
            if (!g.ebullets[i].alive) continue;
            tmp = g.ebullets[i];
            tmp.x += FIELD_X;
            tmp.y += FIELD_Y;
            drawEnemyBullet(hdc, &tmp);
        }
        for (i = 0; i < MAX_ENEMIES; i++) {
            if (!g.enemies[i].alive) continue;
            {
                Enemy tmp = g.enemies[i];
                tmp.x += FIELD_X;
                tmp.y += FIELD_Y;
                drawEnemy(hdc, &tmp);
            }
        }
        if (g.boss.alive) {
            Boss btmp = g.boss;
            btmp.x += FIELD_X;
            btmp.y += FIELD_Y;
            drawBoss(hdc, &btmp);
        }
        drawShield(hdc);
        if (!((double)GetTickCount() / 1000.0 < g.invulnUntil && ((int)(GetTickCount() / 90)) % 2 == 0))
            drawPlayer(hdc, FIELD_X + (int)g.x, FIELD_Y + (int)g.y);
        /* BOSS 血条与提示 */
        drawBossHud(hdc);
        if (g.bossIntroT > 0 && ((int)(g.bossIntroT * 6)) % 2 == 0)
            textOut(hdc, FIELD_X + FIELD_W / 2 - 150, FIELD_Y + 226, "BOSS 出现！", hFontBig, RGB(212, 52, 64));
        if (g.bossRewardT > 0) {
            char rbuf[64];
            sprintf(rbuf, "击败 BOSS！金币 +%d", g.bossRewardCoins);
            textOut(hdc, FIELD_X + FIELD_W / 2 - 196, FIELD_Y + 226, rbuf, hFontBig, RGB(210, 142, 24));
        }
        /* 炸弹闪屏 */
        if (g.bombFlash > 0.33)
            fillRect(hdc, FIELD_X, FIELD_Y, FIELD_W, FIELD_H, RGB(255, 252, 232));
    }

    /* 顶部横幅 */
    roundBox(hdc, 16, 14, WIN_W - 32, 50, RGB(255, 252, 242), RGB(186, 208, 226), 16);
    textOut(hdc, 34, 26, "✈ 天天打飞机 · 卡通版", hFontBig, C_NAVY);
    if (g.state == ST_START)
        textOut(hdc, 440, 30, "← → 选关 · 空格开始 · Q 退出", hFontSmall, RGB(120, 145, 170));
    else if (g.state == ST_PAUSE)
        textOut(hdc, 440, 30, "按 P / Esc 继续 · R 重新选关", hFontSmall, RGB(120, 145, 170));
    else if (g.state == ST_PLAY)
        textOut(hdc, 440, 30, "空格射击 · 点右上角暂停", hFontSmall, RGB(120, 145, 170));
    else
        textOut(hdc, 440, 30, "空格射击 · P 暂停 · Q 退出", hFontSmall, RGB(120, 145, 170));

    if (g.state == ST_PLAY || g.state == ST_PAUSE)
        drawPauseButton(hdc, g.state == ST_PAUSE,
                        inRect(mouseX, mouseY, PAUSE_BTN_X, PAUSE_BTN_Y, PAUSE_BTN_W, PAUSE_BTN_H));

    drawSidebar(hdc);
    if (g.state == ST_PLAY || g.state == ST_PAUSE) drawSkillBar(hdc);
    if (g.state != ST_PLAY) drawOverlay(hdc);
    else if (g.flashT > 0) {
        fillRect(hdc, FIELD_X, FIELD_Y, FIELD_W, 8, RGB(255, 90, 90));
    }
}

static void paintWindow(HDC hdc)
{
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, WIN_W, WIN_H);
    HGDIOBJ old = SelectObject(mem, bmp);
    drawScene(mem);
    BitBlt(hdc, 0, 0, WIN_W, WIN_H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
}

/* ---------------- 窗口与输入 ---------------- */


static void onMouseDown(int x, int y)
{
    int cx = FIELD_X + FIELD_W / 2;
    int bx = cx - 100;
    int by = FIELD_Y + 350;
    int arrowY = FIELD_Y + 222;
    /* 顶部横幅的暂停 / 继续按钮，任何时候优先响应 */
    if ((g.state == ST_PLAY || g.state == ST_PAUSE) &&
        inRect(x, y, PAUSE_BTN_X, PAUSE_BTN_Y, PAUSE_BTN_W, PAUSE_BTN_H)) {
        g.state = (g.state == ST_PLAY) ? ST_PAUSE : ST_PLAY;
        return;
    }
    /* 战场底部的技能按钮（仅游戏中可点） */
    if (g.state == ST_PLAY) {
        if (inRect(x, y, 40, FIELD_Y + FIELD_H - 82, 232, 36)) { castShield(); return; }
        if (inRect(x, y, 296, FIELD_Y + FIELD_H - 82, 232, 36)) { castBomb(); return; }
    }
    if (g.state == ST_START) {
        if (inRect(x, y, cx - 178, arrowY, 56, 56)) {
            if (g_selected > 1) g_selected--;
        } else if (inRect(x, y, cx + 122, arrowY, 56, 56)) {
            if (g_selected < g_maxLevel) g_selected++;
        } else if (inRect(x, y, bx, by, 200, 56)) {
            initGameAt(g_selected);
            g.state = ST_PLAY;
        } else if (inRect(x, y, bx + 230, by, 200, 56)) {
            g.state = ST_UPGRADE;
        }
    } else if (g.state == ST_UPGRADE) {
        int i;
        if (inRect(x, y, cx - 90, FIELD_Y + 560, 180, 50)) {
            g.state = ST_START;
        } else {
            for (i = 0; i < SK_COUNT; i++) {
                int ry = FIELD_Y + 132 + i * 84;
                if (inRect(x, y, FIELD_X + 664, ry + 17, 170, 44)) {
                    buyUpgrade(i);
                    break;
                }
            }
        }
    } else if (g.state == ST_PAUSE) {
        if (inRect(x, y, bx - 110, by, 200, 56)) {
            g.state = ST_PLAY;
        } else if (inRect(x, y, bx + 110, by, 200, 56)) {
            g.state = ST_START;
            g_selected = g_maxLevel;
        }
    } else if (g.state == ST_CLEAR && inRect(x, y, bx, by, 200, 56)) {
        startLevel(g.level + 1);
        g.state = ST_PLAY;
    } else if (g.state == ST_OVER) {
        if (inRect(x, y, bx - 110, by, 200, 56)) {
            /* 回到选关界面 */
            g.state = ST_START;
            g_selected = g_maxLevel;
        } else if (inRect(x, y, bx + 110, by, 200, 56)) {
            g_running = 0;
            PostQuitMessage(0);
        }
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT ps;
    HDC hdc;
    switch (msg) {
    case WM_KEYDOWN:
        if (g.state == ST_UPGRADE && (wParam == 'U' || wParam == VK_ESCAPE)) {
            g.state = ST_START;
        } else if (wParam == 'U' && g.state == ST_START) {
            g.state = ST_UPGRADE;
        } else if (wParam == 'X' && g.state == ST_PLAY) {
            castShield();
        } else if (wParam == 'B' && g.state == ST_PLAY) {
            castBomb();
        } else if (wParam == 'P' || wParam == VK_PAUSE || wParam == VK_ESCAPE) {
            if (g.state == ST_PLAY) g.state = ST_PAUSE;
            else if (g.state == ST_PAUSE) g.state = ST_PLAY;
        } else if (wParam == 'Q') {
            g_running = 0;
            saveCoins();
            PostQuitMessage(0);
        } else if (g.state == ST_START && (wParam == VK_LEFT || wParam == 'A')) {
            if (g_selected > 1) g_selected--;
        } else if (g.state == ST_START && (wParam == VK_RIGHT || wParam == 'D')) {
            if (g_selected < g_maxLevel) g_selected++;
        } else if ((wParam == VK_RETURN || wParam == VK_SPACE) && g.state == ST_START) {
            initGameAt(g_selected);
            g.state = ST_PLAY;
        } else if (g.state == ST_PAUSE &&
                   (wParam == VK_SPACE || wParam == VK_RETURN || wParam == VK_ESCAPE)) {
            g.state = ST_PLAY;
        } else if (g.state == ST_PAUSE && wParam == 'R') {
            g.state = ST_START;
            g_selected = g_maxLevel;
        } else if ((wParam == VK_RETURN || wParam == VK_SPACE) && g.state == ST_CLEAR) {
            startLevel(g.level + 1);
            g.state = ST_PLAY;
        } else if (wParam == 'R' && g.state == ST_OVER) {
            g.state = ST_START;
            g_selected = g_maxLevel;
        } else if (wParam == VK_SPACE && g.state == ST_PLAY) {
            shoot();
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        onMouseDown((int)LOWORD(lParam), (int)HIWORD(lParam));
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_MOUSEMOVE:
        mouseX = (int)LOWORD(lParam);
        mouseY = (int)HIWORD(lParam);
        return 0;
    case WM_SIZE:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_PAINT:
        hdc = BeginPaint(hwnd, &ps);
        paintWindow(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    case WM_CLOSE:
        g_running = 0;
        saveCoins();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        g_running = 0;
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int nShow)
{
    WNDCLASSA wc;
    PFN_SET_DPI_AWARE setDpi = (PFN_SET_DPI_AWARE)GetProcAddress(
        GetModuleHandleA("user32.dll"), "SetProcessDPIAware");
    if (setDpi) setDpi();
    MSG msg;
    RECT rc;
    DWORD last;
    int i, winW, winH;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.lpszClassName = "PlaneShooterGUI";
    if (!RegisterClassA(&wc)) {
        MessageBoxA(NULL, "RegisterClass failed", "debug", MB_OK);
        return 1;
    }

    /* 画布直接放大到 1200x800，窗口按画布大小创建 */
    rc.left = 0; rc.top = 0; rc.right = WIN_W; rc.bottom = WIN_H;
    AdjustWindowRectEx(&rc, WS_OVERLAPPEDWINDOW, FALSE, 0);
    winW = rc.right - rc.left;
    winH = rc.bottom - rc.top;

    g_hwnd = CreateWindowExA(0, "PlaneShooterGUI", "天天打飞机",
                             WS_OVERLAPPEDWINDOW,
                             40, 40, winW, winH,
                             NULL, NULL, hInst, NULL);
    if (!g_hwnd) {
        MessageBoxA(NULL, "CreateWindow failed", "debug", MB_OK);
        return 1;
    }
    setWindowTitleUtf8(g_hwnd, "天天打飞机 - 卡通版");

    hFontTitle = makeFont(48, FW_BOLD);
    hFontBig   = makeFont(32, FW_BOLD);
    hFontMid   = makeFont(23, FW_NORMAL);
    hFontSmall = makeFont(18, FW_NORMAL);

    ShowWindow(g_hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(g_hwnd);
    UpdateWindow(g_hwnd);
    (void)nShow;

    srand((unsigned int)time(NULL));
    memset(&g, 0, sizeof(g));
    loadBest();
    loadProgress();
    loadCoins();
    for (i = 0; i < 8; i++) {
        clouds[i].x = (float)(i * 140 - 60);
        clouds[i].y = (float)(90 + (i % 4) * 125);
        clouds[i].speed = 7.0f + (float)(i % 3) * 4.0f;
        clouds[i].size = (float)(34 + (i % 3) * 12);
    }
    initGameAt(1);
    g.state = ST_START;
    /* 仅供截图验证：带 -play 参数直接进入游戏并放几架敌机 */
    if (cmd && strstr(cmd, "-play")) {
        initGameAt(g_selected);
        g.state = ST_PLAY;
        for (i = 0; i < 6; i++) spawnEnemy();
    }

    last = GetTickCount();
    while (g_running) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        {
            DWORD now = GetTickCount();
            double dt = (double)(now - last) / 1000.0;
            last = now;
            update(dt);
            InvalidateRect(g_hwnd, NULL, FALSE);
        }
        Sleep(25);
    }

    DeleteObject(hFontTitle);
    DeleteObject(hFontBig);
    DeleteObject(hFontMid);
    DeleteObject(hFontSmall);
    return 0;
}

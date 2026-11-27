/* plane_shooter.c - 天天打飞机：C 语言控制台版
 * 编译：tcc plane_shooter.c -o plane_shooter.exe -luser32  或  gcc plane_shooter.c -o plane_shooter.exe
 * 操作：A/D 或 ←/→ 移动，W/S 或 ↑/↓ 微调，空格射击，P 暂停，Q 退出，结束后 R 重玩
 * 玩法：敌机从上方落下并会开火；漏掉敌机不扣血，被撞到或被红色子弹打中才掉一条命；每击落 12 架升一级。
 */

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifndef CP_UTF8
#define CP_UTF8 65001
#endif

#define SW 90
#define SH 32
#define FW 62
#define FH 20
#define FX 2
#define FY 4

#define MAX_BULLETS 64
#define MAX_EBULLETS 20
#define MAX_ENEMIES 24
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

#define COL_WHITE  (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)
#define COL_DIM    (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)
#define COL_GREEN  (FOREGROUND_GREEN | FOREGROUND_INTENSITY)
#define COL_YELLOW (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY)
#define COL_RED    (FOREGROUND_RED | FOREGROUND_INTENSITY)
#define COL_CYAN   (FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY)
#define COL_MAGENTA (FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY)

typedef struct {
    float x, y;
    float vx, vy;
    int alive;
} Bullet;

typedef struct {
    float x, y;
    float speed;
    int hp;
    int type;   /* 0 普通 1 快速 2 装甲 */
    int alive;
    double nextFire;
} Enemy;

/* 关底 BOSS：打满本关目标后登场，只朝下攻击，击落有额外金币 */
typedef struct {
    float x, y, vx;
    int alive, hp, maxHp, reward;
    double nextFire, hitT;
    int shots;
} Boss;

typedef struct {
    float x, y;
    int lives;
    int score;
    int kills;
    int level;
    int levelKills, levelTarget;
    int over, cleared;
    int backToSelect;
    int coinsEarned, bombsLeft;
    double lastShot;
    double invulnUntil;
    double lastSpawn;
    double shieldUntil, shieldReadyAt, lastWing;
    Bullet bullets[MAX_BULLETS];
    Bullet ebullets[MAX_EBULLETS];
    Enemy enemies[MAX_ENEMIES];
    Boss boss;                     /* 关底 BOSS */
    int bossSpawned;
    double bossIntroT, bossRewardT, bossClearDelay;
    int bossRewardCoins;
} Game;

static Game g;
static HANDLE hOut;
static int bestScore = 0;
static int g_maxLevel = 1;    /* 已解锁的最高关卡 */
static int g_selected = 1;    /* 选关界面选中的关卡 */
static int g_coins = 0;       /* 金币（跨局保存） */
static int g_skill[SK_COUNT];

/* ---------- 基础控制台操作 ---------- */

static double nowSec(void)
{
    return (double)GetTickCount() / 1000.0;
}

static void gotoxy(int x, int y)
{
    COORD c;
    c.X = (SHORT)x;
    c.Y = (SHORT)y;
    SetConsoleCursorPosition(hOut, c);
}

static void setColor(int attr)
{
    SetConsoleTextAttribute(hOut, (WORD)attr);
}

static void hideCursor(void)
{
    CONSOLE_CURSOR_INFO ci;
    ci.dwSize = 1;
    ci.bVisible = FALSE;
    SetConsoleCursorInfo(hOut, &ci);
}

/* ---------- 双缓冲：仅在字符/颜色变化时写控制台，避免闪烁 ---------- */

static char scr[SH][SW + 1];
static int scrAttr[SH][SW];
static char shadow[SH][SW + 1];
static int shadowAttr[SH][SW];

static void clearBuffer(void)
{
    int x, y;
    for (y = 0; y < SH; y++) {
        for (x = 0; x < SW; x++) {
            scr[y][x] = ' ';
            scrAttr[y][x] = COL_WHITE;
        }
    }
}

static void putC(int x, int y, char ch, int attr)
{
    if (x >= 0 && x < SW && y >= 0 && y < SH) {
        scr[y][x] = ch;
        scrAttr[y][x] = attr;
    }
}

static void putS(int x, int y, const char *s, int attr)
{
    int i;
    for (i = 0; s[i] != '\0'; i++)
        putC(x + i, y, s[i], attr);
}

static void flushBuffer(void)
{
    int x, y;
    for (y = 0; y < SH; y++) {
        for (x = 0; x < SW; x++) {
            if (scr[y][x] != shadow[y][x] || scrAttr[y][x] != shadowAttr[y][x]) {
                gotoxy(x, y);
                setColor(scrAttr[y][x]);
                printf("%c", scr[y][x]);
                shadow[y][x] = scr[y][x];
                shadowAttr[y][x] = scrAttr[y][x];
            }
        }
    }
}

static void initShadow(void)
{
    int x, y;
    for (y = 0; y < SH; y++) {
        for (x = 0; x < SW; x++) {
            shadow[y][x] = ' ';
            shadowAttr[y][x] = -1;
        }
    }
}

/* ---------- 存档：仅保存最高分 ---------- */

static void loadBest(void)
{
    FILE *f = fopen(BEST_FILE, "r");
    if (f) {
        if (fscanf(f, "%d", &bestScore) != 1) bestScore = 0;
        fclose(f);
    }
}

static void saveBest(void)
{
    FILE *f;
    if (g.score <= bestScore) return;
    bestScore = g.score;
    f = fopen(BEST_FILE, "w");
    if (f) {
        fprintf(f, "%d\n", bestScore);
        fclose(f);
    }
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
    if (f) {
        fprintf(f, "%d\n", g_maxLevel);
        fclose(f);
    }
}

/* ---------- 游戏逻辑 ---------- */

/* 每关目标：第 1 关 12 架，之后每关多 4 架 */
static int targetForLevel(int lv)
{
    return 8 + lv * 4;
}

/* ---------- 技能与金币 ---------- */

static int skillMaxLv(int s)
{
    switch (s) {
    case SK_FIRE:   return 5;
    case SK_WING:   return 3;
    case SK_SHIELD: return 3;
    case SK_LIFE:   return 2;
    default:        return 3;
    }
}

static int skillCost(int s, int lv)
{
    static const int cost[SK_COUNT][5] = {
        { 20, 35, 55, 80, 110 },
        { 25, 45, 70,  0,   0 },
        { 30, 50, 70,  0,   0 },
        { 45, 80,  0,  0,   0 },
        { 35, 60, 90,  0,   0 }
    };
    if (s < 0 || s >= SK_COUNT || lv < 0 || lv >= 5) return 0;
    return cost[s][lv];
}

static const char *skillName(int s)
{
    static const char *names[SK_COUNT] = { "火力强化", "侧翼副炮", "能量护盾", "生命上限", "清屏炸弹" };
    return (s >= 0 && s < SK_COUNT) ? names[s] : "";
}

static const char *skillDesc(int s, int lv)
{
    static const char *fire[6] = {
        "单发子弹", "双发齐射", "三向散射", "三向散射·射速提升",
        "四发扇射", "五发扇射·射速最快"
    };
    static const char *wing[4] = { "未解锁", "两侧斜射", "侧射更密", "双侧各两发" };
    static const char *shield[4] = {
        "未解锁", "X 开盾 3.3 秒 / 冷却 12 秒",
        "X 开盾 4.1 秒 / 冷却 10 秒",
        "X 开盾 4.9 秒 / 冷却 8 秒"
    };
    static const char *life[3] = { "初始 3 条命", "初始 4 条命", "初始 5 条命" };
    static const char *bomb[4] = { "未解锁", "B 清屏 每关 1 次", "B 清屏 每关 2 次", "B 清屏 每关 3 次" };
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
    double t = nowSec();
    int i;
    g.level = lv;
    g.levelKills = 0;
    g.levelTarget = targetForLevel(lv);
    g.x = FW / 2;
    g.y = FH - 2;
    g.cleared = 0;
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
    g.lives = 3 + g_skill[SK_LIFE];
    g.score = 0;
    g.kills = 0;
    g.coinsEarned = 0;
    g.over = 0;
    g.backToSelect = 0;
    g.lastShot = 0;
    startLevel(lv);
}

/* 放一颗玩家子弹（主炮与侧翼炮共用） */
static int addBullet(float x, float y, float vx)
{
    int i;
    for (i = 0; i < MAX_BULLETS; i++) {
        if (!g.bullets[i].alive) {
            g.bullets[i].x = x;
            g.bullets[i].y = y;
            g.bullets[i].vx = vx;
            g.bullets[i].alive = 1;
            return 1;
        }
    }
    return 0;
}

/* 主炮：技能等级决定发数、散射与射速 */
static void tryShoot(void)
{
    double t = nowSec();
    int lv = g_skill[SK_FIRE];
    double interval = (lv >= 5) ? 0.12 : ((lv >= 3) ? 0.14 : 0.16);
    int n = 1, i, used = 0;
    float ox[5], vx[5];

    if (g.over || t - g.lastShot < interval) return;
    if (lv == 0) {
        ox[0] = 0; vx[0] = 0; n = 1;
    } else if (lv == 1) {
        ox[0] = -1; ox[1] = 1; vx[0] = 0; vx[1] = 0; n = 2;
    } else if (lv <= 3) {
        ox[0] = -2; ox[1] = 0; ox[2] = 2;
        vx[0] = -3; vx[1] = 0; vx[2] = 3; n = 3;
    } else if (lv == 4) {
        ox[0] = -3; ox[1] = -1; ox[2] = 1; ox[3] = 3;
        vx[0] = -4; vx[1] = -1; vx[2] = 1; vx[3] = 4; n = 4;
    } else {
        ox[0] = -4; ox[1] = -2; ox[2] = 0; ox[3] = 2; ox[4] = 4;
        vx[0] = -6; vx[1] = -3; vx[2] = 0; vx[3] = 3; vx[4] = 6; n = 5;
    }
    for (i = 0; i < n; i++) {
        /* 先算成 float 变量再传参，避免 tcc 对复合表达式传参算错坐标 */
        float bx = g.x + ox[i];
        float by = g.y - 1.0f;
        if (addBullet(bx, by, vx[i])) used++;
    }
    if (used) g.lastShot = t;
}

/* 侧翼副炮：自动向两侧斜射 */
static void wingFire(double t)
{
    int lv = g_skill[SK_WING];
    double interval;
    if (lv <= 0) return;
    interval = 0.95 - lv * 0.18;
    if (t - g.lastWing < interval) return;
    g.lastWing = t;
    {
        float dx = 6.0f + (float)lv * 2.0f;
        float lx = g.x - 1.0f;
        float rx = g.x + 1.0f;
        addBullet(lx, g.y, -dx);
        addBullet(rx, g.y, dx);
    }
    if (lv >= 3) {
        float ty = g.y - 1.0f;
        float lx = g.x - 2.0f;
        float rx = g.x + 2.0f;
        addBullet(lx, ty, -9.0f);
        addBullet(rx, ty, 9.0f);
    }
}

/* 护盾：X 键开盾，持续时间内免疫伤害 */
static void castShield(void)
{
    double t = nowSec();
    int lv = g_skill[SK_SHIELD];
    if (g.over || g.cleared || lv <= 0) return;
    if (t < g.shieldReadyAt) return;
    g.shieldUntil = t + 3.3 + (lv - 1) * 0.8;
    g.shieldReadyAt = g.shieldUntil + (12.0 - (lv - 1) * 2.0);
}

/* 关底 BOSS 登场（实现见下） */
static void spawnBoss(void);

/* 达成关卡目标：先召唤关底 BOSS，击败它才算通关 */
static void checkLevelClear(void)
{
    int k;
    if (g.levelKills < g.levelTarget) return;
    if (!g.bossSpawned) {
        for (k = 0; k < MAX_ENEMIES; k++) g.enemies[k].alive = 0;
        for (k = 0; k < MAX_EBULLETS; k++) g.ebullets[k].alive = 0;
        spawnBoss();
        return;
    }
    if (g.boss.alive) return;
    for (k = 0; k < MAX_ENEMIES; k++) g.enemies[k].alive = 0;
    for (k = 0; k < MAX_EBULLETS; k++) g.ebullets[k].alive = 0;
    if (g.level + 1 > g_maxLevel) { g_maxLevel = g.level + 1; saveProgress(); }
    saveCoins();
    g.cleared = 1;
}

/* 炸弹：清掉全场敌机与敌机子弹 */
static void castBomb(void)
{
    int i;
    if (g.over || g.cleared || g_skill[SK_BOMB] <= 0 || g.bombsLeft <= 0) return;
    g.bombsLeft--;
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (!g.enemies[i].alive) continue;
        g.score += (g.enemies[i].type == 2 ? 30 : (g.enemies[i].type == 1 ? 20 : 10));
        g.enemies[i].alive = 0;
        g.kills++;
        g.levelKills++;
    }
    for (i = 0; i < MAX_EBULLETS; i++) g.ebullets[i].alive = 0;
    checkLevelClear();
}

/* 敌机只朝正前方（笔直向下）开火，速度慢、可以躲开 */
static void enemyFire(int idx, double t)
{
    int i;
    for (i = 0; i < MAX_EBULLETS; i++) {
        if (g.ebullets[i].alive) continue;
        g.ebullets[i].x = g.enemies[idx].x;
        g.ebullets[i].y = g.enemies[idx].y + 1;
        g.ebullets[i].vx = 0;
        g.ebullets[i].vy = 7.0f + (float)(g.level - 1) * 0.5f;
        g.ebullets[i].alive = 1;
        break;
    }
    {
        double interval = g.enemies[idx].type == 2 ? 3.2 :
                          (g.enemies[idx].type == 1 ? 3.8 : 4.6);
        interval -= (double)(g.level - 1) * 0.15;
        if (interval < 2.2) interval = 2.2;
        g.enemies[idx].nextFire = t + interval + (double)(rand() % 80) / 100.0;
    }
}

static void spawnEnemy(void)
{
    int i, r, armored, fast;
    double fireDelay;
    /* 关卡越高，装甲机/快速机越多，下落越快，开火越早 */
    armored = 12 + (g.level - 1) * 4;
    if (armored > 40) armored = 40;
    fast = 22 + (g.level - 1) * 3;
    if (fast > 40) fast = 40;
    fireDelay = 1.4 - (double)(g.level - 1) * 0.1;
    if (fireDelay < 0.7) fireDelay = 0.7;
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (!g.enemies[i].alive) {
            r = rand() % 100;
            g.enemies[i].type = r < armored ? 2 : (r < armored + fast ? 1 : 0);
            g.enemies[i].hp = (g.enemies[i].type == 2) ? 2 : 1;
            g.enemies[i].speed = (g.enemies[i].type == 0 ? 7.0f :
                                  (g.enemies[i].type == 1 ? 12.0f : 5.0f)) *
                                 (1.0f + (float)(g.level - 1) * 0.15f);
            g.enemies[i].x = 1 + rand() % (FW - 2);
            g.enemies[i].y = 0;
            g.enemies[i].nextFire = nowSec() + fireDelay + (double)(rand() % 100) / 100.0;
            g.enemies[i].alive = 1;
            return;
        }
    }
}

/* 敌机子弹通用入口（BOSS 复用） */
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

/* 关底 BOSS 登场：从上方压下来，血量与奖励随关卡增长 */
static void spawnBoss(void)
{
    double t = nowSec();
    Boss *b = &g.boss;
    b->maxHp = 16 + g.level * 8;         /* 第 1 关 24 血 */
    b->hp = b->maxHp;
    b->x = FW / 2.0f;
    b->y = -3.0f;
    b->vx = (g.level % 2 == 0) ? -8.0f : 8.0f;
    b->alive = 1;
    b->hitT = 0;
    b->shots = 0;
    b->nextFire = t + 2.0;
    b->reward = 15 + g.level * 10;       /* 第 1 关 25 金币 */
    g.bossSpawned = 1;
    g.bossIntroT = 2.4;
}

/* BOSS 被击落：额外金币，随后结算通关 */
static void killBoss(void)
{
    Boss *b = &g.boss;
    int reward = b->reward;
    b->alive = 0;
    g.score += 150 + g.level * 60;
    g_coins += reward;
    g.coinsEarned += reward;
    g.bossRewardCoins = reward;
    g.bossRewardT = 3.0;
    g.bossClearDelay = 1.4;
}

/* BOSS 行为：左右巡航、只朝正下方开火 */
static void updateBoss(double dt, double t)
{
    Boss *b = &g.boss;
    int i, n;
    float bx, by, vy, off;
    double interval;

    if (!b->alive) return;
    if (b->y < 3.0f) {                   /* 压入阶段不攻击 */
        b->y += 9.0f * (float)dt;
        if (b->y > 3.0f) b->y = 3.0f;
        return;
    }
    b->x += b->vx * (float)dt;
    if (b->x < 6.0f) { b->x = 6.0f; b->vx = -b->vx; }
    if (b->x > FW - 6.0f) { b->x = FW - 6.0f; b->vx = -b->vx; }
    if (b->hitT > 0) b->hitT -= dt;

    if (t < b->nextFire) return;
    b->shots++;
    n = (b->shots % 3 == 0) ? 5 : 3;
    for (i = 0; i < n; i++) {
        off = ((float)i - (float)(n - 1) / 2.0f) * 3.0f;
        bx = b->x + off;
        by = b->y + 2.0f;
        vy = 8.5f + (float)g.level * 0.4f;
        addEBullet(bx, by, vy);
    }
    interval = 2.4 - (double)(g.level - 1) * 0.1;
    if (interval < 1.2) interval = 1.2;
    b->nextFire = t + interval;
}

static void hitPlayer(void)
{
    double t = nowSec();
    if (t < g.invulnUntil) return;
    /* 护盾生效时免疫 */
    if (t < g.shieldUntil) {
        g.invulnUntil = t + 0.2;
        return;
    }
    g.lives--;
    g.invulnUntil = t + 1.8;
    if (g.lives <= 0) {
        g.lives = 0;
        g.over = 1;
        saveBest();
        saveCoins();
    }
}

static void update(double dt)
{
    double t = nowSec();
    int i, j;
    int bx, by, ex, ey;
    double spawnInterval;

    if (g.over || g.cleared) return;
    if (dt > 0.08) dt = 0.08;
    if (g.bossIntroT > 0) g.bossIntroT -= dt;
    if (g.bossRewardT > 0) g.bossRewardT -= dt;

    /* 子弹上升（带横向速度，用于侧翼斜射） */
    for (i = 0; i < MAX_BULLETS; i++) {
        if (!g.bullets[i].alive) continue;
        g.bullets[i].y -= (float)(40.0 * dt);
        g.bullets[i].x += g.bullets[i].vx * (float)dt;
        if (g.bullets[i].y < -1 || g.bullets[i].x < 0 || g.bullets[i].x >= FW)
            g.bullets[i].alive = 0;
    }
    wingFire(t);

    /* 敌机下落 */
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (!g.enemies[i].alive) continue;
        g.enemies[i].y += (float)(g.enemies[i].speed * dt);
        /* 漏掉的敌机直接飞走，不扣生命 */
        if (g.enemies[i].y > FH) {
            g.enemies[i].alive = 0;
            continue;
        }
        if (g.enemies[i].y > 1 && g.enemies[i].y < FH - 1 && t >= g.enemies[i].nextFire)
            enemyFire(i, t);
        /* 撞到玩家 */
        ex = (int)(g.enemies[i].x + 0.5f);
        ey = (int)(g.enemies[i].y + 0.5f);
        if (ex == (int)g.x && ey == (int)g.y) {
            g.enemies[i].alive = 0;
            hitPlayer();
        }
    }

    /* 关底 BOSS：巡航、开火，撞上玩家同样掉血 */
    updateBoss(dt, t);
    if (g.boss.alive && g.boss.y > -1.0f) {
        ex = (int)(g.boss.x + 0.5f);
        ey = (int)(g.boss.y + 0.5f);
        if ((int)g.x >= ex - 5 && (int)g.x <= ex + 5 &&
            (int)g.y >= ey - 1 && (int)g.y <= ey + 1) hitPlayer();
    }
    if (g.bossSpawned && !g.boss.alive) {
        if (g.bossClearDelay > 0) g.bossClearDelay -= dt;
        else checkLevelClear();
    }

    /* 敌方子弹：飞出战场消失，命中玩家才掉血 */
    for (i = 0; i < MAX_EBULLETS; i++) {
        if (!g.ebullets[i].alive) continue;
        g.ebullets[i].x += g.ebullets[i].vx * (float)dt;
        g.ebullets[i].y += g.ebullets[i].vy * (float)dt;
        if (g.ebullets[i].y > FH || g.ebullets[i].x < 0 || g.ebullets[i].x >= FW) {
            g.ebullets[i].alive = 0;
            continue;
        }
        if ((int)(g.ebullets[i].x + 0.5f) == (int)g.x &&
            (int)(g.ebullets[i].y + 0.5f) == (int)g.y) {
            g.ebullets[i].alive = 0;
            hitPlayer();
        }
    }

    /* 子弹与敌机碰撞（按屏幕格子判定） */
    for (i = 0; i < MAX_BULLETS; i++) {
        if (!g.bullets[i].alive) continue;
        bx = (int)(g.bullets[i].x + 0.5f);
        by = (int)(g.bullets[i].y + 0.5f);
        for (j = 0; j < MAX_ENEMIES; j++) {
            if (!g.enemies[j].alive) continue;
            ex = (int)(g.enemies[j].x + 0.5f);
            ey = (int)(g.enemies[j].y + 0.5f);
            /* 字符画面里允许左右各差一格，斜射子弹才好命中 */
            if ((bx - ex) * (bx - ex) <= 1 && by == ey) {
                g.bullets[i].alive = 0;
                g.enemies[j].hp--;
                if (g.enemies[j].hp <= 0) {
                    g.enemies[j].alive = 0;
                    g.score += (g.enemies[j].type == 2 ? 30 :
                                (g.enemies[j].type == 1 ? 20 : 10));
                    g.kills++;
                    g.levelKills++;
                    /* 击落得金币：普通 1 · 快速 2 · 装甲 3 */
                    {
                        int coin = g.enemies[j].type == 2 ? 3 : (g.enemies[j].type == 1 ? 2 : 1);
                        g_coins += coin;
                        g.coinsEarned += coin;
                    }
                    /* 达成目标：清场并等待玩家确认进入下一关 */
                    checkLevelClear();
                }
                break;
            }
        }
    }

    /* 子弹 vs BOSS：打空血条才算击败 */
    if (g.boss.alive) {
        for (i = 0; i < MAX_BULLETS; i++) {
            if (!g.bullets[i].alive) continue;
            bx = (int)(g.bullets[i].x + 0.5f);
            by = (int)(g.bullets[i].y + 0.5f);
            ex = (int)(g.boss.x + 0.5f);
            ey = (int)(g.boss.y + 0.5f);
            if (bx >= ex - 5 && bx <= ex + 5 && by >= ey - 1 && by <= ey + 1) {
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

    if (g.cleared || g.bossSpawned) return;   /* BOSS 阶段不再刷杂兵 */

    /* 生成敌机：关卡越高越密集，第 2 关起可能双机齐飞 */
    spawnInterval = 1.15 - (double)(g.level - 1) * 0.08;
    if (spawnInterval < 0.30) spawnInterval = 0.30;
    if (t - g.lastSpawn >= spawnInterval) {
        g.lastSpawn = t;
        spawnEnemy();
        if (g.level >= 2 && rand() % 100 < 15 + (g.level - 2) * 8) spawnEnemy();
    }
}

static void draw(void)
{
    int i, x, y;
    double t = nowSec();
    char buf[80];

    clearBuffer();

    /* 顶部信息 */
    putS(2, 1, "天天打飞机  |  击落敌机得分，躲开红色子弹和撞击！", COL_CYAN);
    sprintf(buf, "得分:%d  金币:%d  最高:%d  生命:%d  第%d关(%d/%d)",
            g.score, g_coins, bestScore, g.lives, g.level, g.levelKills, g.levelTarget);
    putS(2, 2, buf, COL_YELLOW);
    putS(64, 2, "P 暂停  Q 退出", COL_DIM);

    /* 战场边框 */
    putC(FX - 1, FY - 1, '+', COL_DIM);
    putC(FX + FW, FY - 1, '+', COL_DIM);
    putC(FX - 1, FY + FH, '+', COL_DIM);
    putC(FX + FW, FY + FH, '+', COL_DIM);
    for (x = 0; x < FW; x++) {
        putC(FX + x, FY - 1, '-', COL_DIM);
        putC(FX + x, FY + FH, '-', COL_DIM);
    }
    for (y = 0; y < FH; y++) {
        putC(FX - 1, FY + y, '|', COL_DIM);
        putC(FX + FW, FY + y, '|', COL_DIM);
    }

    /* 子弹 */
    for (i = 0; i < MAX_BULLETS; i++) {
        if (!g.bullets[i].alive) continue;
        putC(FX + (int)(g.bullets[i].x + 0.5f), FY + (int)(g.bullets[i].y + 0.5f), '|', COL_GREEN);
    }

    /* 敌方子弹（红色 *） */
    for (i = 0; i < MAX_EBULLETS; i++) {
        if (!g.ebullets[i].alive) continue;
        putC(FX + (int)(g.ebullets[i].x + 0.5f), FY + (int)(g.ebullets[i].y + 0.5f), '*', COL_RED);
    }

    /* 敌机 */
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (!g.enemies[i].alive) continue;
        x = FX + (int)(g.enemies[i].x + 0.5f);
        y = FY + (int)(g.enemies[i].y + 0.5f);
        if (g.enemies[i].type == 0)
            putC(x, y, 'V', COL_RED);
        else if (g.enemies[i].type == 1)
            putC(x, y, 'v', COL_MAGENTA);
        else
            putC(x, y, g.enemies[i].hp == 2 ? 'W' : 'w', COL_YELLOW);
    }

    /* 关底 BOSS */
    if (g.boss.alive) {
        static const char *art[3] = {
            "  _______  ",
            " <[o   o]> ",
            "  \\_V_//  "
        };
        int bxx = FX + (int)(g.boss.x + 0.5f);
        int byy = FY + (int)(g.boss.y + 0.5f);
        int ci = g.boss.hitT > 0 ? COL_WHITE : COL_MAGENTA;
        for (i = 0; i < 3; i++) putS(bxx - 5, byy - 1 + i, art[i], ci);
    }

    /* BOSS 血条（横在战场最上面一行） */
    if (g.bossSpawned && (g.boss.alive || g.bossRewardT > 0)) {
        char bar[40];
        int bw = 30, fill, k;
        fill = bw * (g.boss.hp > 0 ? g.boss.hp : 0) /
               (g.boss.maxHp > 0 ? g.boss.maxHp : 1);
        for (k = 0; k < bw; k++) bar[k] = (k < fill) ? '#' : '-';
        bar[bw] = '\0';
        putS(FX + 1, FY, "BOSS", COL_RED);
        putS(FX + 6, FY, "[", COL_RED);
        putS(FX + 7, FY, bar, COL_RED);
        putS(FX + 7 + bw, FY, "]", COL_RED);
    }
    if (g.bossIntroT > 0 && ((int)(g.bossIntroT * 5)) % 2 == 0)
        putS(FX + 22, FY + 8, "!!! BOSS 来了 !!!", COL_RED);
    if (g.bossRewardT > 0) {
        sprintf(buf, "BOSS DOWN!  +%d COINS", g.bossRewardCoins);
        putS(FX + 20, FY + 8, buf, COL_YELLOW);
    }

    /* 玩家（无敌时间内闪烁） */
    if (!(t < g.invulnUntil && ((int)(t * 10) % 2 == 0)))
        putC(FX + (int)g.x, FY + (int)g.y, 'A', COL_CYAN);

    /* 底部说明 */
    putS(2, FY + FH + 2, "操作：A/D 或 ←/→ 移动  W/S 或 ↑/↓ 微调  空格射击  X 护盾  B 炸弹  P 暂停", COL_DIM);
    putS(2, FY + FH + 3, "敌机：V 普通 10分   v 快速 20分   W 装甲 30分(需两发)", COL_DIM);
    putS(2, FY + FH + 4, "规则：漏掉敌机不扣血；被撞或被红色 * 打中才掉血", COL_RED);
    putS(2, FY + FH + 5, "金币：击落敌机获得；每关打满目标会出现 BOSS，击落它金币更多", COL_CYAN);

    /* 技能状态栏 */
    {
        char s1[40], s2[40];
        int slv = g_skill[SK_SHIELD];
        if (slv <= 0) sprintf(s1, "护盾:未解锁");
        else if (t < g.shieldUntil) sprintf(s1, "护盾:生效 %.1fs", g.shieldUntil - t);
        else if (t < g.shieldReadyAt) sprintf(s1, "护盾:冷却 %d秒", (int)(g.shieldReadyAt - t) + 1);
        else sprintf(s1, "护盾:X 就绪");
        if (g_skill[SK_BOMB] <= 0) sprintf(s2, "炸弹:未解锁");
        else if (g.bombsLeft <= 0) sprintf(s2, "炸弹:本关用完");
        else sprintf(s2, "炸弹:B x%d", g.bombsLeft);
        sprintf(buf, "技能  %s   %s", s1, s2);
        putS(2, SH - 2, buf, COL_GREEN);
    }

    /* 通关面板：必须按空格确认才进入下一关 */
    if (g.cleared) {
        for (y = FY + 5; y <= FY + 13; y++)
            for (x = FX + 8; x <= FX + 54; x++)
                putC(x, y, ' ', COL_WHITE);
        sprintf(buf, "第 %d 关完成！", g.level);
        putS(FX + 22, FY + 6, buf, COL_GREEN);
        sprintf(buf, "本关击落 %d 架 · 累计得分 %d", g.levelKills, g.score);
        putS(FX + 17, FY + 8, buf, COL_YELLOW);
        if (g.bossRewardCoins > 0)
            sprintf(buf, "金币 +%d（共 %d）· BOSS 额外 +%d", g.coinsEarned, g_coins, g.bossRewardCoins);
        else
            sprintf(buf, "本局金币 +%d（共 %d）", g.coinsEarned, g_coins);
        putS(FX + 14, FY + 9, buf, COL_YELLOW);
        sprintf(buf, "按空格进入第 %d 关", g.level + 1);
        putS(FX + 21, FY + 11, buf, COL_CYAN);
    }

    /* 结束画面 */
    if (g.over) {
        for (y = FY + 5; y <= FY + 13; y++)
            for (x = FX + 8; x <= FX + 54; x++)
                putC(x, y, ' ', COL_WHITE);
        putS(FX + 21, FY + 6, "GAME OVER", COL_RED);
        sprintf(buf, "本局得分：%d   最高分：%d", g.score, bestScore);
        putS(FX + 18, FY + 8, buf, COL_YELLOW);
        sprintf(buf, "共击落 %d 架 · 到达第 %d 关", g.kills, g.level);
        putS(FX + 18, FY + 9, buf, COL_CYAN);
        sprintf(buf, "本局金币 +%d · 金币已保存", g.coinsEarned);
        putS(FX + 18, FY + 10, buf, COL_YELLOW);
        putS(FX + 18, FY + 12, "按 R 返回选关，按 Q 退出", COL_CYAN);
    }

    flushBuffer();
}

static void showPause(int paused)
{
    putS(2, SH - 1, paused ? "已暂停：按 P / Esc 继续，按 Q 退出" : "                                                  ", COL_YELLOW);
    gotoxy(2, SH - 1);
}

/* 技能升级界面：↑↓ 选择，空格花金币升级，Esc 返回 */
static void selectUpgrade(void)
{
    char buf[90];
    int sel = 0;
    while (1) {
        int i;
        clearBuffer();
        putS(2, 1, "天天打飞机 · 技能升级", COL_CYAN);
        sprintf(buf, "金币：%d", g_coins);
        putS(66, 1, buf, COL_YELLOW);
        putS(2, 2, "升级永久生效，金币会跨局保留", COL_DIM);
        for (i = 0; i < SK_COUNT; i++) {
            int y = 4 + i * 3;
            int lv = g_skill[i], mx = skillMaxLv(i), c, k;
            char dots[8];
            for (k = 0; k < 5; k++)
                dots[k] = (k < mx) ? ((k < lv) ? '#' : '-') : ' ';
            dots[5] = 0;
            sprintf(buf, "%s %s  Lv.%d/%d  [%s]", (i == sel ? ">" : " "), skillName(i), lv, mx, dots);
            putS(4, y, buf, i == sel ? COL_GREEN : COL_WHITE);
            putS(4, y + 1, skillDesc(i, lv), COL_DIM);
            if (lv >= mx) {
                putS(56, y, "已满级", COL_DIM);
            } else {
                c = skillCost(i, lv);
                sprintf(buf, "%d 金币  %s", c, g_coins >= c ? "可升级" : "金币不足");
                putS(52, y, buf, g_coins >= c ? COL_YELLOW : COL_DIM);
            }
        }
        putS(2, 24, "↑ ↓ 选择     空格 升级     Esc / U 返回", COL_DIM);
        flushBuffer();
        if (_kbhit()) {
            int ch = _getch();
            if (ch == 0 || ch == 224) {
                int d = _getch();
                if (d == 72 && sel > 0) sel--;
                else if (d == 80 && sel < SK_COUNT - 1) sel++;
            } else if (ch == ' ') {
                buyUpgrade(sel);
            } else if (ch == 27 || ch == 'u' || ch == 'U') {
                return;
            }
        }
        Sleep(30);
    }
}

/* 选关界面：左右切换已解锁关卡，空格开始 */
static int selectScreen(int *quit)
{
    char buf[80];
    while (!*quit) {
        clearBuffer();
        putS(2, 1, "天天打飞机 · 关卡选择", COL_CYAN);
        putS(2, 3, "← → 切换关卡     空格开始     U 技能升级     Q 退出", COL_DIM);
        sprintf(buf, "第 %d 关", g_selected);
        putS(40, 9, buf, COL_YELLOW);
        sprintf(buf, "本关目标：击落 %d 架敌机", targetForLevel(g_selected));
        putS(28, 11, buf, COL_DIM);
        sprintf(buf, "已解锁到第 %d 关", g_maxLevel);
        putS(34, 13, buf, COL_GREEN);
        sprintf(buf, "金币：%d    （击落敌机获得，可升级技能）", g_coins);
        putS(20, 15, buf, COL_YELLOW);
        putS(26, 18, "通关后自动解锁下一关", COL_DIM);
        putS(22, 20, "关卡越高，敌机越快、越多、越难打", COL_DIM);
        flushBuffer();
        if (_kbhit()) {
            int c = _getch();
            if (c == 0 || c == 224) {
                int d = _getch();
                if (d == 75 && g_selected > 1) g_selected--;              /* ← */
                else if (d == 77 && g_selected < g_maxLevel) g_selected++; /* → */
            } else if (c == ' ') {
                return 1;
            } else if (c == 'u' || c == 'U') {
                selectUpgrade();
            } else if (c == 'q' || c == 'Q') {
                *quit = 1;
                return 0;
            }
        }
        Sleep(30);
    }
    return 0;
}

int main(void)
{
    double last, t, dt;
    int quit = 0;

    hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleOutputCP(CP_UTF8);
    srand((unsigned int)time(NULL));
    system("mode con cols=90 lines=32 >nul 2>&1");
    system("cls");
    hideCursor();
    initShadow();
    loadBest();
    loadProgress();
    loadCoins();

    while (!quit) {
        if (!selectScreen(&quit)) break;
        initGameAt(g_selected);
        last = nowSec();

        while (!quit && !g.backToSelect) {
            t = nowSec();
            dt = t - last;
            if (dt < 0) dt = 0;
            last = t;

            if (_kbhit()) {
                int c = _getch();
                if (c == 0 || c == 224) {
                    int d = _getch();
                    if (d == 75 && g.x > 0) g.x--;                 /* ← */
                    else if (d == 77 && g.x < FW - 1) g.x++;       /* → */
                    else if (d == 72 && g.y > 0) g.y--;            /* ↑ */
                    else if (d == 80 && g.y < FH - 1) g.y++;       /* ↓ */
                } else if (c == 'q' || c == 'Q') {
                    quit = 1;
                    continue;
                } else if (c == 'r' || c == 'R') {
                    if (g.over) g.backToSelect = 1;   /* 回到选关界面 */
                } else if (c == 'p' || c == 'P' || c == 27) {
                    int paused = 1;
                    showPause(1);
                    while (paused) {
                        if (_kbhit()) {
                            int k = _getch();
                            if (k == 'p' || k == 'P' || k == 27) paused = 0;
                            else if (k == 'q' || k == 'Q') { quit = 1; paused = 0; }
                        }
                        Sleep(30);
                    }
                    showPause(0);
                    last = nowSec();
                    if (quit) break;
                } else if (c == 'x' || c == 'X') {
                    castShield();
                } else if (c == 'b' || c == 'B') {
                    castBomb();
                } else if (c == ' ') {
                    if (g.cleared) startLevel(g.level + 1);
                    else tryShoot();
                } else if (c == 'a' || c == 'A') {
                    if (g.x > 0) g.x--;
                } else if (c == 'd' || c == 'D') {
                    if (g.x < FW - 1) g.x++;
                } else if (c == 'w' || c == 'W') {
                    if (g.y > 0) g.y--;
                } else if (c == 's' || c == 'S') {
                    if (g.y < FH - 1) g.y++;
                }
            }

            update(dt);
            draw();
            Sleep(30);
        }
    }

    saveBest();
    saveCoins();
    gotoxy(0, SH - 1);
    setColor(COL_WHITE);
    printf("游戏结束，最高分：%d。\n", bestScore);
    return 0;
}

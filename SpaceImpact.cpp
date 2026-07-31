/*
 * =====================================================================================
 *  项目名称：Nokia 3310 Space Impact (空间冲击) C++ 全功能复刻版
 *  技术栈：C++17 / Windows Native API (Win32 GDI) / 双缓冲离屏渲染 / 多线程音效
 * 
 *  ===================================================================================
 *  【全代码超详细注释与架构解析】
 *  1. 架构模式 (Architecture & Patterns)：
 *     - 面向对象设计 (OOP) + 状态模式 (State Pattern)：通过枚举 `GameState` 解耦标题、
 *       游戏运行、中弹暂停、手动暂停、关卡结算、游戏结束与通关胜利等状态机逻辑。
 *     - ECS 实体组件思想：使用 STL 动态容器 `std::vector` 组合管理子弹 (Bullet)、
 *       敌机 (Enemy)、掉落道具 (PowerUp)、爆炸粒子 (Particle) 与背景星空 (Star)。
 * 
 *  2. 软件点阵渲染管线 (Software Rasterizer Pipeline)：
 *     - 84x48 单色/多色 LCD 点阵缓冲区：`m_pixelGrid` 模拟诺基亚 3310 原生点阵液晶屏。
 *     - 自研 5x3 微型字模引擎 (`DrawStringLCD`)：采用 Bitmask 位掩码解析 ASCII 字符。
 *     - 图形原语与算法：包含 Bresenham 直线画法算法、矩形填充与 ASCII 字符位图渲染器。
 *     - 多色分层绘制：通过不同像素标识，精准呈现【高亮电光蓝】玩家激光与【警告红色】敌弹。
 * 
 *  3. Win32 GDI 双缓冲技术 (GDI Double Buffering)：
 *     - 拦截 `WM_ERASEBKGND` 防止默认擦除引起的强闪烁。
 *     - 在内存中创建兼容 DC (`CreateCompatibleDC`) 与位图画布 (`CreateCompatibleBitmap`)。
 *     - 一次性通过 `BitBlt` 块快照传输到物理窗口 HDC，锁定 60 FPS 无闪烁渲染。
 * 
 *  4. 多线程与并发 (Multithreading & Concurrency)：
 *     - 异步线程脱离 (`std::thread().detach()`)：在独立后台线程中调用 Windows `Beep` API，
 *       实现零 UI/渲染线程阻塞的音效播放。
 * 
 *  5. 物理碰撞与游戏循环 (Physics, Collision & Game Loop)：
 *     - AABB 轴对齐包围盒碰撞检测 (Axis-Aligned Bounding Box)。
 *     - 正弦波轨迹方程 (`y += sin(t) * A * dt`) 模拟侦察机巡航。
 *     - 高精度时钟 (`std::chrono::high_resolution_clock`) 实现 Delta Time (dt) 帧率锁定。
 * =====================================================================================
 */

#ifndef UNICODE
#define UNICODE // 使用 Unicode 字符集，兼容 Windows 宽字符 API (如 TextOutW, CreateWindowExW)
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

// -------------------------------------------------------------------------------------
// C/C++ 标准库与 Windows 原生头文件包含
// -------------------------------------------------------------------------------------
#include <windows.h>    // Win32 原生 API 核心头文件 (提供窗口句柄 HWND、设备上下文 HDC、GDI 绘图函数与键盘响应)
#include <iostream>     // 标准输入输出流
#include <vector>       // STL 动态数组：用于高效管理游戏中的各种实体 (子弹、敌机、道具、粒子)
#include <string>       // 字符串处理库
#include <chrono>       // C++11 高精度时间库：用于计算微秒/毫秒级 Delta Time (dt) 锁定 60 FPS
#include <thread>       // C++11 多线程库：用于创建后台音效线程，防止主 UI 线程因音效阻断
#include <cmath>        // 数学函数库：用于极坐标转换 (cos/sin)、正弦波敌机运动轨迹计算
#include <algorithm>    // STL 算法库：提供 std::remove_if 配合 vector::erase 实现高效实体清扫
#include <cstdlib>      // 标准库：提供 rand() 随机数生成与 srand 时间种子初始化
#include <ctime>        // 时间库：提供 time(NULL) 作为随机数种子
#include <fstream>      // 文件流库：用于本地最高分 (space_impact_score.dat) 的读写持久化保存
#include <sstream>      // 字符串流库：用于 HUD 状态栏分数字符串的高效拼接

using namespace std;

// =====================================================================================
// 基础常量定义 (分辨率、放大倍率与窗口坐标参数)
// =====================================================================================

// 诺基亚 3310 原生 LCD 液晶屏物理分辨率 (逻辑点阵)
const int LCD_WIDTH = 84;   // 经典点阵宽度 84 像素
const int LCD_HEIGHT = 48;  // 经典点阵高度 48 像素

// 现代显示器上的 LCD 像素点放大倍率 (1 个 LCD 逻辑像素点映射为现代显示器上的 7x7 物理像素)
const int PIXEL_SCALE = 7; // 84 * 7 = 588px 宽度, 48 * 7 = 336px 高度

// 诺基亚 3310 机身外壳渲染边距与全窗口尺寸计算
const int PHONE_MARGIN_X = 60;       // 左右两侧机身塑料外壳边距 (像素)
const int PHONE_MARGIN_TOP = 90;     // 顶部听筒与 NOKIA 金属 Bar 边距 (像素)
const int PHONE_MARGIN_BOTTOM = 220;  // 底部物理按键与操作提示区域边距 (像素)
const int WINDOW_WIDTH = LCD_WIDTH * PIXEL_SCALE + PHONE_MARGIN_X * 2;   // 708px 窗口内部总宽度
const int WINDOW_HEIGHT = LCD_HEIGHT * PIXEL_SCALE + PHONE_MARGIN_TOP + PHONE_MARGIN_BOTTOM; // 646px 窗口内部总高度

// =====================================================================================
// 调色板定义 (RGB Color Palette - 拟真诺基亚 3310 绿屏、手机外壳与分色子弹)
// =====================================================================================
const COLORREF COLOR_LCD_BG        = RGB(155, 188, 15);  // #9bbc0f 经典诺基亚 3310 黄绿色 LCD 液晶背光色
const COLORREF COLOR_LCD_GRID      = RGB(139, 172, 15);  // #8bac0f 未点亮像素点的网格微暗色 (实现拟真点阵网格感)
const COLORREF COLOR_LCD_PIXEL     = RGB(15, 56, 15);    // #0f380f 点亮墨绿色 LCD 基础像素点 (用于玩家飞船、敌机、Boss、UI)
const COLORREF COLOR_BULLET_PLAYER = RGB(0, 150, 255);   // #0096ff 玩家子弹：高亮电光蓝色 (与敌弹形成鲜明对比)
const COLORREF COLOR_BULLET_ENEMY  = RGB(255, 30, 30);   // #ff1e1e 敌方子弹：鲜艳警告红色 (高视觉警示度)

const COLORREF COLOR_PHONE_BODY = RGB(35, 45, 65);    // 经典藏青色诺基亚 3310 塑料磨砂机身外壳
const COLORREF COLOR_PHONE_TRIM = RGB(210, 215, 220);  // 机身银灰色金属质感包边与按键包边
const COLORREF COLOR_BTN_DARK   = RGB(50, 60, 75);    // 按键深色阴影调
const COLORREF COLOR_BTN_TEXT   = RGB(230, 235, 240);  // 按键上的印字亮白色

// =====================================================================================
// 枚举数据结构 (状态模式与实体类型划分)
// =====================================================================================

// 游戏状态枚举 (State Pattern：解耦游戏不同阶段的更新与渲染管线)
enum GameState {
    STATE_TITLE,        // 主菜单 / 标题开始画面
    STATE_PLAYING,      // 游戏正常运行中
    STATE_PLAYER_HIT,   // 玩家被击中/中弹暂停状态 (显示 SHIP HIT! PRESS SPACE 提示)
    STATE_PAUSED,       // 玩家手动按 P 键触发的暂停状态
    STATE_LEVEL_CLEAR,  // 关卡完成结算
    STATE_GAME_OVER,    // 游戏结束 (玩家生命值耗尽阵亡)
    STATE_VICTORY       // 通关胜利 (击败最终关卡 Boss 母舰)
};

// 敌机类型枚举 (包含不同 AI 移动轨迹与开火行为)
enum EnemyType {
    ENEMY_SCOUT,       // 侦察机：做 S 形正弦波巡航轨迹，移动速度中等
    ENEMY_BOMBER,      // 重型轰炸机：直线缓慢推进，定期向玩家方向发射红色敌弹
    ENEMY_INTERCEPTOR, // 截击机：急速直线冲刺，血量较低但移动极快
    ENEMY_ASTEROID     // 障碍陨石 (预留扩展类型)
};

// 掉落强化道具类型枚举
enum PowerUpType {
    POWER_WEAPON,  // 武器升级 (单发激光 -> 平行双重激光 -> 三向扇形散弹)
    POWER_HEALTH,  // 生命恢复 (恢复 1 点生命值 HP)
    POWER_BOMB,    // 清屏炸弹补给 (增加 1 枚全屏震荡波炸弹)
    POWER_SHIELD   // 临时无敌护盾 (获得 5 秒无敌时间)
};

// =====================================================================================
// 实体结构体定义 (Entity Component Data Structs)
// =====================================================================================

// 爆炸粒子结构体 (用于极坐标物理散射粒子系统)
struct Particle {
    float x, y;     // 粒子在 LCD 点阵上的当前浮点坐标
    float vx, vy;   // X 轴与 Y 轴上的速度矢量 (像素/秒)
    int life;       // 当前剩余寿命 (帧数)
    int maxLife;    // 初始最大寿命 (用于计算透明度/衰减)
};

// 子弹结构体 (同时支持玩家子弹与敌方子弹)
struct Bullet {
    float x, y;     // 子弹当前点阵坐标
    float vx, vy;   // 飞行速度矢量 (vx > 0 向右飞行，vx < 0 向左飞行)
    bool isPlayer;  // true: 玩家发射的激光, false: 敌机/Boss 发射的红弹
    int damage;     // 该子弹造成的伤害数值
};

// 掉落道具结构体
struct PowerUp {
    float x, y;     // 道具坐标
    float vx;       // 道具向左缓慢漂移的速度
    PowerUpType type; // 道具种类枚举
    bool active;    // 是否处于激活可用状态 (未被拾取或未出屏)
};

// 敌机结构体
struct Enemy {
    float x, y;         // 敌机当前坐标
    float vx, vy;       // 移动速度矢量
    EnemyType type;     // 敌机种类 (Scout, Bomber, Interceptor)
    int hp;             // 当前剩余生命值
    int maxHp;          // 最大生命值
    float timer;        // 累计生存时间 (用于 sin() 正弦波轨迹相位计算)
    float shootTimer;   // 开火冷却计时器
    bool active;        // 是否存活
    int scoreValue;     // 击毁该敌机获得的分数奖励
};

// 关卡 Boss 结构体 (宇宙母舰)
struct Boss {
    float x, y;         // Boss 母舰坐标
    float vy;           // 上下乒乓反弹浮动速度
    int hp;             // 当前血量
    int maxHp;          // 最大血量上限 (用于绘制血条比例)
    int phase;          // 当前攻击阶段
    float timer;        // 阶段行为计时器
    float shootTimer;   // 复合弹幕发射计时器
    bool active;        // Boss 是否已入场激活
};

// =====================================================================================
// 本地最高分持久化 I/O 函数
// =====================================================================================

// 全局最高分变量
int g_highScore = 0;

// 从本地二进制/文本文件加载最高分记录
void LoadHighScore() {
    ifstream file("space_impact_score.dat");
    if (file.is_open()) {
        file >> g_highScore;
        file.close();
    }
}

// 刷新并保存最高分至本地文件
void SaveHighScore(int score) {
    if (score > g_highScore) {
        g_highScore = score;
        ofstream file("space_impact_score.dat");
        if (file.is_open()) {
            file << g_highScore;
            file.close();
        }
    }
}

// =====================================================================================
// 点阵位图数据 (ASCII Bitmap Patterns - 逻辑点阵字符矩阵)
// 说明：系统使用字符 '#' 或 '1' 代表点亮像素，'.' 代表未点亮透明像素
// =====================================================================================

// 玩家飞船位图 (9x7 像素)
const int SHIP_WIDTH = 9;
const int SHIP_HEIGHT = 7;
const char SHIP_BITMAP[] = 
    "..#......"
    ".###....."
    "#####...."
    "#########"
    "#####...."
    ".###....."
    "..#......";

// 侦察机位图 (7x5 像素)
const int SCOUT_WIDTH = 7;
const int SCOUT_HEIGHT = 5;
const char SCOUT_BITMAP[] =
    "..#...."
    ".###..."
    "#######"
    ".###..."
    "..#....";

// 重型轰炸机位图 (9x7 像素)
const int BOMBER_WIDTH = 9;
const int BOMBER_HEIGHT = 7;
const char BOMBER_BITMAP[] =
    "...###..."
    "..#####.."
    ".#######."
    "#########"
    ".#######."
    "..#####.."
    "...###...";

// 关卡 Boss 母舰位图 (18x16 像素)
const int BOSS_WIDTH = 18;
const int BOSS_HEIGHT = 16;
const char BOSS_BITMAP[] =
    "....##########...."
    "...############..."
    "..##############.."
    ".################."
    "##################"
    "#####...##...#####"
    "####..######..####"
    "###..########..###"
    "###..########..###"
    "####..######..####"
    "#####...##...#####"
    "##################"
    ".################."
    "..##############.."
    "...############..."
    "....##########....";

// =====================================================================================
// 核心游戏引擎类 (Space Impact Game Engine)
// 职责：管理引擎主循环、点阵渲染缓冲区、实体生命周期、物理碰撞检测、输入响应与 UI 渲染
// =====================================================================================
class SpaceImpactGame {
public:
    // 动态 LCD 点阵网格缓冲区 (0 = 未点亮网格, 1 = 基础LCD墨绿像素, 2 = 玩家电光蓝子弹, 3 = 敌方警告红子弹)
    uint8_t m_pixelGrid[LCD_HEIGHT][LCD_WIDTH];
    GameState m_state;  // 当前游戏状态机

    // 玩家状态数据 (标准诺基亚 3 滴血设定)
    float m_playerX, m_playerY; // 玩家当前位置 (使用 float 保证移动平滑无卡顿)
    int m_playerHp;             // 当前血量 (1~3)
    int m_playerMaxHp;          // 最大血量上限 (3)
    int m_playerLives;          // 剩余复活命数
    int m_score;                // 当前游戏累计得分
    int m_weaponLevel;          // 武器火力等级 (1~3 阶)
    int m_bombCount;            // 当前持有清屏炸弹数量
    float m_invincibleTimer;    // 受伤/复位后的无敌闪烁护盾计时器

    // 关卡进度与刷怪控制
    int m_currentStage;         // 当前关卡号 (1 或 2)
    float m_stageProgress;      // 当前关卡进度百分比计时
    float m_stageLength;        // 关卡总里程 (到达后暂停刷怪，触发 Boss 入场)
    float m_spawnTimer;         // 普通敌机波次刷怪定时器
    bool m_bossSpawned;         // 标志关卡 Boss 是否已生成入场

    // 游戏实体容器 (使用 STL vector 进行动态组合管理)
    vector<Bullet> m_bullets;     // 当前存活的所有子弹
    vector<Enemy> m_enemies;       // 当前存活的普通敌机
    vector<PowerUp> m_powerUps;   // 场景中掉落的强化道具
    vector<Particle> m_particles; // 爆炸与震荡波粒子特效
    Boss m_boss;                  // 关卡 Boss 实体对象

    // 视差滚动背景星空 (Parallax Starfield)
    struct Star { float x, y, speed; };
    vector<Star> m_stars;

    // 按键边缘触发锁 (Edge Triggers: 防止单次按键在连续帧内重复触发开火/炸弹)
    bool m_keyPrevSpace;
    bool m_keyPrevBomb;
    bool m_keyPrevPause;
    bool m_keyPrevEsc;

    // 引擎构造函数：初始化状态与默认参数 (标准 3 滴血)
    SpaceImpactGame() {
        m_state = STATE_TITLE;
        m_playerX = 5;
        m_playerY = 20;
        m_playerHp = 3;
        m_playerMaxHp = 3;
        m_playerLives = 3;
        m_score = 0;
        m_weaponLevel = 1;
        m_bombCount = 2;
        m_invincibleTimer = 0.0f;

        m_currentStage = 1;
        m_stageProgress = 0.0f;
        m_stageLength = 100.0f;
        m_spawnTimer = 0.0f;
        m_bossSpawned = false;

        m_keyPrevSpace = false;
        m_keyPrevBomb = false;
        m_keyPrevPause = false;
        m_keyPrevEsc = false;

        m_boss.active = false;
        ClearPixelGrid();
    }

    // 引擎系统初始化：设置时间种子、从本地读取高分并生成视差滚动星空
    void Init() {
        srand((unsigned int)time(NULL)); // 设置系统随机种子
        LoadHighScore();                 // 加载最高分文件

        // 随机生成 35 颗具有不同移动速度的背景视差星星
        m_stars.clear();
        for (int i = 0; i < 35; i++) {
            m_stars.push_back({
                (float)(rand() % LCD_WIDTH),
                (float)(rand() % (LCD_HEIGHT - 8) + 8),
                (float)(rand() % 3 + 1) * 0.2f // 速度差异形成层次感
            });
        }
    }

    // 重置游戏数据 (用于新游戏重新开局)
    void ResetGame() {
        m_playerX = 5;
        m_playerY = LCD_HEIGHT / 2 - SHIP_HEIGHT / 2;
        m_playerHp = 3;
        m_playerMaxHp = 3;
        m_playerLives = 3;
        m_score = 0;
        m_weaponLevel = 1;
        m_bombCount = 2;
        m_invincibleTimer = 0.0f;

        m_currentStage = 1;
        m_stageProgress = 0.0f;
        m_spawnTimer = 0.0f;
        m_bossSpawned = false;

        m_bullets.clear();
        m_enemies.clear();
        m_powerUps.clear();
        m_particles.clear();
        m_boss.active = false;
    }

    // ---------------------------------------------------------------------------------
    // 多线程异步音效播放模块 (Asynchronous Multi-threaded Audio)
    // 关键亮点：利用 std::thread 启动独立后台线程调用 Windows Beep API 播放单音，
    // 并在创建后调用 .detach() 剥离句柄，实现零 UI 阻塞、零渲染卡顿的音效系统。
    // ---------------------------------------------------------------------------------
    void PlaySoundEffect(int freq, int duration) {
        std::thread([freq, duration]() {
            Beep(freq, duration);
        }).detach();
    }

    // ---------------------------------------------------------------------------------
    // 软件点阵渲染器 (Software Rasterizer / Dot-Matrix LCD Renderer)
    // ---------------------------------------------------------------------------------

    // 清空 84x48 LCD 点阵缓冲区 (内存清零)
    void ClearPixelGrid() {
        memset(m_pixelGrid, 0, sizeof(m_pixelGrid));
    }

    // 绘制单个 LCD 逻辑像素点
    // 参数 colorType: 0=未点亮网格, 1=常规墨绿像素, 2=玩家电光蓝子弹, 3=敌弹警告红子弹
    void DrawPixel(int x, int y, int colorType = 1) {
        if (x >= 0 && x < LCD_WIDTH && y >= 0 && y < LCD_HEIGHT) {
            m_pixelGrid[y][x] = (uint8_t)colorType;
        }
    }

    // Bresenham 直线算法 (用于高效绘制 UI 分割线与几何边框)
    void DrawLine(int x0, int y0, int x1, int y1, int colorType = 1) {
        int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy, e2;
        for (;;) {
            DrawPixel(x0, y0, colorType);
            if (x0 == x1 && y0 == y1) break;
            e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }

    // 绘制矩形 (支持空心框与实心填充矩形)
    void DrawRect(int x, int y, int w, int h, bool filled = false, int colorType = 1) {
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) {
                if (filled || i == 0 || i == h - 1 || j == 0 || j == w - 1) {
                    DrawPixel(x + j, y + i, colorType);
                }
            }
        }
    }

    // 自研微型 5x3 点阵字符字模引擎 (Embedded 5x3 Pixel Font Generator)
    // 原理：为每个 ASCII 字符硬编码 5 行 3 列的 Bitmask 位掩码，按位提取像素点写入点阵内存
    void DrawStringLCD(int x, int y, const std::string& text, int colorType = 1) {
        for (size_t i = 0; i < text.size(); i++) {
            char c = text[i];
            int cx = x + (int)i * 4; // 每个字符宽度占 3px + 1px 字符间距 = 4px
            if (cx + 3 > LCD_WIDTH) break;

            uint8_t glyph[5] = { 0,0,0,0,0 };
            switch (toupper(c)) {
                case 'A': glyph[0]=0x2; glyph[1]=0x5; glyph[2]=0x7; glyph[3]=0x5; glyph[4]=0x5; break;
                case 'B': glyph[0]=0x6; glyph[1]=0x5; glyph[2]=0x6; glyph[3]=0x5; glyph[4]=0x6; break;
                case 'C': glyph[0]=0x3; glyph[1]=0x4; glyph[2]=0x4; glyph[3]=0x4; glyph[4]=0x3; break;
                case 'D': glyph[0]=0x6; glyph[1]=0x5; glyph[2]=0x5; glyph[3]=0x5; glyph[4]=0x6; break;
                case 'E': glyph[0]=0x7; glyph[1]=0x4; glyph[2]=0x6; glyph[3]=0x4; glyph[4]=0x7; break;
                case 'F': glyph[0]=0x7; glyph[1]=0x4; glyph[2]=0x6; glyph[3]=0x4; glyph[4]=0x4; break;
                case 'G': glyph[0]=0x3; glyph[1]=0x4; glyph[2]=0x5; glyph[3]=0x5; glyph[4]=0x3; break;
                case 'H': glyph[0]=0x5; glyph[1]=0x5; glyph[2]=0x7; glyph[3]=0x5; glyph[4]=0x5; break;
                case 'I': glyph[0]=0x7; glyph[1]=0x2; glyph[2]=0x2; glyph[3]=0x2; glyph[4]=0x7; break;
                case 'J': glyph[0]=0x1; glyph[1]=0x1; glyph[2]=0x1; glyph[3]=0x5; glyph[4]=0x2; break;
                case 'K': glyph[0]=0x5; glyph[1]=0x5; glyph[2]=0x6; glyph[3]=0x5; glyph[4]=0x5; break;
                case 'L': glyph[0]=0x4; glyph[1]=0x4; glyph[2]=0x4; glyph[3]=0x4; glyph[4]=0x7; break;
                case 'M': glyph[0]=0x5; glyph[1]=0x7; glyph[2]=0x5; glyph[3]=0x5; glyph[4]=0x5; break;
                case 'N': glyph[0]=0x5; glyph[1]=0x7; glyph[2]=0x7; glyph[3]=0x5; glyph[4]=0x5; break;
                case 'O': glyph[0]=0x2; glyph[1]=0x5; glyph[2]=0x5; glyph[3]=0x5; glyph[4]=0x2; break;
                case 'P': glyph[0]=0x6; glyph[1]=0x5; glyph[2]=0x6; glyph[3]=0x4; glyph[4]=0x4; break;
                case 'Q': glyph[0]=0x2; glyph[1]=0x5; glyph[2]=0x5; glyph[3]=0x3; glyph[4]=0x1; break;
                case 'R': glyph[0]=0x6; glyph[1]=0x5; glyph[2]=0x6; glyph[3]=0x5; glyph[4]=0x5; break;
                case 'S': glyph[0]=0x3; glyph[1]=0x4; glyph[2]=0x2; glyph[3]=0x1; glyph[4]=0x6; break;
                case 'T': glyph[0]=0x7; glyph[1]=0x2; glyph[2]=0x2; glyph[3]=0x2; glyph[4]=0x2; break;
                case 'U': glyph[0]=0x5; glyph[1]=0x5; glyph[2]=0x5; glyph[3]=0x5; glyph[4]=0x7; break;
                case 'V': glyph[0]=0x5; glyph[1]=0x5; glyph[2]=0x5; glyph[3]=0x5; glyph[4]=0x2; break;
                case 'W': glyph[0]=0x5; glyph[1]=0x5; glyph[2]=0x5; glyph[3]=0x7; glyph[4]=0x5; break;
                case 'X': glyph[0]=0x5; glyph[1]=0x5; glyph[2]=0x2; glyph[3]=0x5; glyph[4]=0x5; break;
                case 'Y': glyph[0]=0x5; glyph[1]=0x5; glyph[2]=0x2; glyph[3]=0x2; glyph[4]=0x2; break;
                case 'Z': glyph[0]=0x7; glyph[1]=0x1; glyph[2]=0x2; glyph[3]=0x4; glyph[4]=0x7; break;
                case '0': glyph[0]=0x7; glyph[1]=0x5; glyph[2]=0x5; glyph[3]=0x5; glyph[4]=0x7; break;
                case '1': glyph[0]=0x2; glyph[1]=0x6; glyph[2]=0x2; glyph[3]=0x2; glyph[4]=0x7; break;
                case '2': glyph[0]=0x7; glyph[1]=0x1; glyph[2]=0x7; glyph[3]=0x4; glyph[4]=0x7; break;
                case '3': glyph[0]=0x7; glyph[1]=0x1; glyph[2]=0x3; glyph[3]=0x1; glyph[4]=0x7; break;
                case '4': glyph[0]=0x5; glyph[1]=0x5; glyph[2]=0x7; glyph[3]=0x1; glyph[4]=0x1; break;
                case '5': glyph[0]=0x7; glyph[1]=0x4; glyph[2]=0x7; glyph[3]=0x1; glyph[4]=0x7; break;
                case '6': glyph[0]=0x7; glyph[1]=0x4; glyph[2]=0x7; glyph[3]=0x5; glyph[4]=0x7; break;
                case '7': glyph[0]=0x7; glyph[1]=0x1; glyph[2]=0x2; glyph[3]=0x2; glyph[4]=0x2; break;
                case '8': glyph[0]=0x7; glyph[1]=0x5; glyph[2]=0x7; glyph[3]=0x5; glyph[4]=0x7; break;
                case '9': glyph[0]=0x7; glyph[1]=0x5; glyph[2]=0x7; glyph[3]=0x1; glyph[4]=0x7; break;
                case ':': glyph[0]=0x0; glyph[1]=0x2; glyph[2]=0x0; glyph[3]=0x2; glyph[4]=0x0; break;
                case '-': glyph[0]=0x0; glyph[1]=0x0; glyph[2]=0x7; glyph[3]=0x0; glyph[4]=0x0; break;
                case '!': glyph[0]=0x2; glyph[1]=0x2; glyph[2]=0x2; glyph[3]=0x0; glyph[4]=0x2; break;
                case '.': glyph[0]=0x0; glyph[1]=0x0; glyph[2]=0x0; glyph[3]=0x0; glyph[4]=0x2; break;
                case '#': glyph[0]=0x5; glyph[1]=0x7; glyph[2]=0x7; glyph[3]=0x7; glyph[4]=0x2; break; // HP 心形像素
                case '*': glyph[0]=0x5; glyph[1]=0x2; glyph[2]=0x7; glyph[3]=0x2; glyph[4]=0x5; break; // Bomb 星形弹药
                case '/': glyph[0]=0x1; glyph[1]=0x1; glyph[2]=0x2; glyph[3]=0x4; glyph[4]=0x4; break;
                case '+': glyph[0]=0x0; glyph[1]=0x2; glyph[2]=0x7; glyph[3]=0x2; glyph[4]=0x0; break;
                default: break;
            }

            // 提取 Bitmask，将字符描绘至点阵内存
            for (int r = 0; r < 5; r++) {
                for (int c = 0; c < 3; c++) {
                    if (glyph[r] & (1 << (2 - c))) {
                        DrawPixel(cx + c, y + r, colorType);
                    }
                }
            }
        }
    }

    // 绘制 ASCII 模式阵列位图 (Bitmap Renderer)
    void DrawBitmap(int x, int y, int width, int height, const char* pattern, int colorType = 1) {
        for (int r = 0; r < height; r++) {
            for (int c = 0; c < width; c++) {
                char ch = pattern[r * width + c];
                if (ch == '#' || ch == '1') {
                    DrawPixel(x + c, y + r, colorType);
                } else if (ch == '.') {
                    DrawPixel(x + c, y + r, 0);
                }
            }
        }
    }

    // ---------------------------------------------------------------------------------
    // 物理、特效与武器机制
    // ---------------------------------------------------------------------------------

    // 极坐标粒子爆炸效果生成器 (基于极坐标转换：x = v*cos(θ), y = v*sin(θ))
    void SpawnExplosion(float x, float y, int count = 10) {
        PlaySoundEffect(150 + rand() % 200, 30); // 随机音高爆炸声
        for (int i = 0; i < count; i++) {
            float angle = (float)(rand() % 360) * 3.14159f / 180.0f; // 360度随机角度
            float speed = (float)(rand() % 100) / 50.0f + 0.5f;       // 随机速度矢量
            m_particles.push_back({
                x, y,
                cos(angle) * speed,
                sin(angle) * speed,
                rand() % 15 + 5, // 随机寿命
                20
            });
        }
    }

    // 玩家受击响应处理函数 (触发中弹暂停、扣血与状态转换)
    void OnPlayerHit() {
        m_playerHp--;
        SpawnExplosion(m_playerX, m_playerY, 15);
        PlaySoundEffect(200, 150);

        // 擦除玩家附近的敌方子弹，防止解除暂停后瞬间二次中弹
        m_bullets.erase(remove_if(m_bullets.begin(), m_bullets.end(), [](const Bullet& b) {
            return !b.isPlayer;
        }), m_bullets.end());

        if (m_playerHp <= 0) {
            m_playerLives--;
            if (m_playerLives <= 0) {
                m_state = STATE_GAME_OVER; // 生命值耗尽，游戏结束
                SaveHighScore(m_score);
            } else {
                m_playerHp = m_playerMaxHp;
                m_state = STATE_PLAYER_HIT; // 进入中弹暂停界面 (等待 Space 恢复)
            }
        } else {
            m_state = STATE_PLAYER_HIT; // 进入中弹暂停界面 (等待 Space 恢复)
        }
    }

    // 清屏炸弹必杀技 (Screen Bomb / Shockwave)
    void TriggerScreenBomb() {
        if (m_bombCount <= 0) return;
        m_bombCount--;
        PlaySoundEffect(600, 80);
        PlaySoundEffect(300, 120);

        // 1. 清空所有敌方子弹 (使用 Lambda 表达式配合 remove_if)
        m_bullets.erase(remove_if(m_bullets.begin(), m_bullets.end(), [](const Bullet& b) {
            return !b.isPlayer;
        }), m_bullets.end());

        // 2. 对全屏敌机施加 AOE 20 点伤害
        for (auto& enemy : m_enemies) {
            if (enemy.active) {
                enemy.hp -= 20;
                SpawnExplosion(enemy.x, enemy.y, 8);
            }
        }

        // 3. 对关卡 Boss 造成 30 点重创
        if (m_boss.active) {
            m_boss.hp -= 30;
            SpawnExplosion(m_boss.x + BOSS_WIDTH / 2, m_boss.y + BOSS_HEIGHT / 2, 20);
        }

        // 4. 生成全屏震荡波点阵粒子特效
        for (int x = 0; x < LCD_WIDTH; x += 4) {
            for (int y = 8; y < LCD_HEIGHT; y += 4) {
                m_particles.push_back({
                    (float)x, (float)y, 0, 0, 10, 10
                });
            }
        }
    }

    // 玩家武器开火机制 (多阶升级：单发 -> 双重平行 -> 三向散弹)
    void FirePlayerWeapon() {
        PlaySoundEffect(800, 18);
        float bx = m_playerX + SHIP_WIDTH;
        float by = m_playerY + SHIP_HEIGHT / 2;

        if (m_weaponLevel == 1) {
            // 1 阶：单发正东方向蓝色激光
            m_bullets.push_back({ bx, by, 3.5f, 0.0f, true, 1 });
        } else if (m_weaponLevel == 2) {
            // 2 阶：双重平行蓝色激光
            m_bullets.push_back({ bx, by - 2, 3.5f, 0.0f, true, 1 });
            m_bullets.push_back({ bx, by + 2, 3.5f, 0.0f, true, 1 });
        } else if (m_weaponLevel >= 3) {
            // 3 阶：三向扇形散射等离子炮 (中路高伤害 + 上下斜向)
            m_bullets.push_back({ bx, by, 4.0f, 0.0f, true, 2 });
            m_bullets.push_back({ bx, by - 1, 3.8f, -0.6f, true, 1 });
            m_bullets.push_back({ bx, by + 1, 3.8f, 0.6f, true, 1 });
        }
    }

    // ---------------------------------------------------------------------------------
    // 输入轮询与逻辑更新 (Input Polling & Game Logic Update Loop)
    // ---------------------------------------------------------------------------------
    void UpdateInput(float dt) {
        // 非阻塞按键状态检查 (检测最高位 0x8000 是否置位)
        bool keyUp = (GetAsyncKeyState(VK_UP) & 0x8000) || (GetAsyncKeyState('W') & 0x8000);
        bool keyDown = (GetAsyncKeyState(VK_DOWN) & 0x8000) || (GetAsyncKeyState('S') & 0x8000);
        bool keyLeft = (GetAsyncKeyState(VK_LEFT) & 0x8000) || (GetAsyncKeyState('A') & 0x8000);
        bool keyRight = (GetAsyncKeyState(VK_RIGHT) & 0x8000) || (GetAsyncKeyState('D') & 0x8000);
        bool keySpace = (GetAsyncKeyState(VK_SPACE) & 0x8000) || (GetAsyncKeyState('J') & 0x8000) || (GetAsyncKeyState('Z') & 0x8000);
        bool keyBomb = (GetAsyncKeyState('K') & 0x8000) || (GetAsyncKeyState('B') & 0x8000) || (GetAsyncKeyState('X') & 0x8000);
        bool keyPause = (GetAsyncKeyState('P') & 0x8000);
        bool keyEsc = (GetAsyncKeyState(VK_ESCAPE) & 0x8000);

        if (m_state == STATE_TITLE) {
            if (keySpace && !m_keyPrevSpace) {
                ResetGame();
                m_state = STATE_PLAYING;
                PlaySoundEffect(523, 80);
            }
        } else if (m_state == STATE_PLAYING) {
            // 玩家移动位图 (乘 Delta Time 保证解耦帧率)
            float moveSpeed = 35.0f * dt;
            if (keyUp) m_playerY -= moveSpeed;
            if (keyDown) m_playerY += moveSpeed;
            if (keyLeft) m_playerX -= moveSpeed;
            if (keyRight) m_playerX += moveSpeed;

            // 限制玩家移动范围在 LCD 边界内部
            if (m_playerX < 1) m_playerX = 1;
            if (m_playerX > LCD_WIDTH - SHIP_WIDTH - 2) m_playerX = (float)(LCD_WIDTH - SHIP_WIDTH - 2);
            if (m_playerY < 8) m_playerY = 8;
            if (m_playerY > LCD_HEIGHT - SHIP_HEIGHT - 1) m_playerY = (float)(LCD_HEIGHT - SHIP_HEIGHT - 1);

            // 射击 CD 冷却判定
            static float shootCooldown = 0.0f;
            shootCooldown -= dt;
            if (keySpace && shootCooldown <= 0.0f) {
                FirePlayerWeapon();
                shootCooldown = (m_weaponLevel >= 3) ? 0.12f : 0.18f;
            }

            // 清屏炸弹触发
            if (keyBomb && !m_keyPrevBomb) {
                TriggerScreenBomb();
            }

            // 手动暂停触发
            if (keyPause && !m_keyPrevPause) {
                m_state = STATE_PAUSED;
            }
        } else if (m_state == STATE_PLAYER_HIT) {
            // 中弹暂停状态：按下 Space 原位恢复游戏（保持被击落时的原位置，赋予 2 秒无敌闪烁护盾）
            if (keySpace && !m_keyPrevSpace) {
                m_invincibleTimer = 2.0f; // 2秒无敌保护
                m_state = STATE_PLAYING;
                PlaySoundEffect(523, 60);
            }
        } else if (m_state == STATE_PAUSED) {
            if (keyPause && !m_keyPrevPause) {
                m_state = STATE_PLAYING;
            } else if (keyEsc && !m_keyPrevEsc) {
                m_state = STATE_TITLE;
            }
        } else if (m_state == STATE_GAME_OVER || m_state == STATE_VICTORY || m_state == STATE_LEVEL_CLEAR) {
            if (keySpace && !m_keyPrevSpace) {
                if (m_state == STATE_LEVEL_CLEAR) {
                    m_currentStage++;
                    m_stageProgress = 0.0f;
                    m_bossSpawned = false;
                    m_playerX = 5;
                    m_playerY = LCD_HEIGHT / 2;
                    m_state = STATE_PLAYING;
                } else {
                    m_state = STATE_TITLE;
                }
            }
        }

        // 保存当前帧按键状态用于下帧边缘触发对比
        m_keyPrevSpace = keySpace;
        m_keyPrevBomb = keyBomb;
        m_keyPrevPause = keyPause;
        m_keyPrevEsc = keyEsc;
    }

    // 敌机波次刷怪与关卡 Boss 触发机制
    void SpawnEnemies(float dt) {
        if (m_bossSpawned || m_state != STATE_PLAYING) return;

        m_stageProgress += dt * 3.5f;

        // 关卡进度到达设定里程，触发关卡 Boss 母舰入场
        if (m_stageProgress >= m_stageLength && !m_bossSpawned) {
            m_bossSpawned = true;
            m_boss.x = LCD_WIDTH + 5;
            m_boss.y = (LCD_HEIGHT - 8 - BOSS_HEIGHT) / 2 + 8;
            m_boss.vy = 12.0f;
            m_boss.hp = (m_currentStage == 1) ? 120 : 250;
            m_boss.maxHp = m_boss.hp;
            m_boss.phase = 1;
            m_boss.timer = 0;
            m_boss.shootTimer = 0;
            m_boss.active = true;

            PlaySoundEffect(300, 150); // Boss 入场警报
            PlaySoundEffect(200, 200);
            return;
        }

        // 定时刷怪逻辑 (随机按不同权重生成 3 种敌机)
        m_spawnTimer += dt;
        if (m_spawnTimer >= 1.2f) {
            m_spawnTimer = 0.0f;
            int r = rand() % 100;
            float ey = (float)(rand() % (LCD_HEIGHT - 16) + 9);

            if (r < 50) {
                // 50% 概率生成侦察机 (正弦波巡航)
                m_enemies.push_back({
                    (float)LCD_WIDTH, ey,
                    -20.0f, 0.0f,
                    ENEMY_SCOUT, 2, 2, 0.0f, 0.0f, true, 100
                });
            } else if (r < 80) {
                // 30% 概率生成重型轰炸机 (会开火射击)
                m_enemies.push_back({
                    (float)LCD_WIDTH, ey,
                    -12.0f, 0.0f,
                    ENEMY_BOMBER, 5, 5, 0.0f, 0.0f, true, 250
                });
            } else {
                // 20% 概率生成截击机 (高速冲刺)
                m_enemies.push_back({
                    (float)LCD_WIDTH, ey,
                    -32.0f, 0.0f,
                    ENEMY_INTERCEPTOR, 1, 1, 0.0f, 0.0f, true, 150
                });
            }
        }
    }

    // 全局实体状态更新与 AABB 包围盒碰撞检测 (Physics & Collision)
    void UpdateEntities(float dt) {
        // 更新视差背景星空
        for (auto& s : m_stars) {
            s.x -= s.speed * 20.0f * dt;
            if (s.x < 0) s.x += LCD_WIDTH;
        }

        if (m_state != STATE_PLAYING && m_state != STATE_PLAYER_HIT) return;

        // 在中弹暂停状态下仅更新爆炸粒子散落
        if (m_state == STATE_PLAYER_HIT) {
            for (auto& p : m_particles) {
                p.x += p.vx * 30.0f * dt;
                p.y += p.vy * 30.0f * dt;
                p.life--;
            }
            m_particles.erase(remove_if(m_particles.begin(), m_particles.end(), [](const Particle& p) {
                return p.life <= 0;
            }), m_particles.end());
            return;
        }

        if (m_invincibleTimer > 0.0f) m_invincibleTimer -= dt;

        // 1. 更新子弹移动与边界回收
        for (auto& b : m_bullets) {
            b.x += b.vx * 60.0f * dt;
            b.y += b.vy * 60.0f * dt;
        }
        m_bullets.erase(remove_if(m_bullets.begin(), m_bullets.end(), [](const Bullet& b) {
            return b.x < 0 || b.x > LCD_WIDTH || b.y < 8 || b.y > LCD_HEIGHT;
        }), m_bullets.end());

        // 2. 更新爆炸粒子
        for (auto& p : m_particles) {
            p.x += p.vx * 30.0f * dt;
            p.y += p.vy * 30.0f * dt;
            p.life--;
        }
        m_particles.erase(remove_if(m_particles.begin(), m_particles.end(), [](const Particle& p) {
            return p.life <= 0;
        }), m_particles.end());

        // 3. 更新掉落道具与玩家拾取碰撞判定
        for (auto& p : m_powerUps) {
            p.x += p.vx * dt;

            // AABB 包围盒碰撞距离判定
            if (p.active && abs(p.x - m_playerX) < 4 && abs(p.y - m_playerY) < 4) {
                p.active = false;
                PlaySoundEffect(1000, 60);
                if (p.type == POWER_WEAPON && m_weaponLevel < 3) m_weaponLevel++;
                else if (p.type == POWER_HEALTH && m_playerHp < m_playerMaxHp) m_playerHp++;
                else if (p.type == POWER_BOMB) m_bombCount++;
                else if (p.type == POWER_SHIELD) m_invincibleTimer = 5.0f;
            }
        }

        // 4. 更新敌机轨迹、反击开火与玩家撞击判定
        for (auto& e : m_enemies) {
            if (!e.active) continue;
            e.timer += dt;
            e.x += e.vx * dt;

            // 正弦波轨迹: y += sin(timer * 4.0) * 15.0 * dt
            if (e.type == ENEMY_SCOUT) {
                e.y += sin(e.timer * 4.0f) * 15.0f * dt;
            } else if (e.type == ENEMY_BOMBER) {
                e.shootTimer += dt;
                if (e.shootTimer >= 1.8f) {
                    e.shootTimer = 0.0f;
                    // 向左发射敌方红色弹丸
                    m_bullets.push_back({ e.x, e.y + BOMBER_HEIGHT / 2, -1.8f, 0.0f, false, 1 });
                }
            }

            // 敌机与玩家 AABB 矩形包围盒精准碰撞判定
            int ew = (e.type == ENEMY_BOMBER) ? BOMBER_WIDTH : 
                     ((e.type == ENEMY_SCOUT) ? SCOUT_WIDTH : 4);
            int eh = (e.type == ENEMY_BOMBER) ? BOMBER_HEIGHT : 
                     ((e.type == ENEMY_SCOUT) ? SCOUT_HEIGHT : 4);

            bool overlapX = (m_playerX < e.x + ew) && (m_playerX + SHIP_WIDTH > e.x);
            bool overlapY = (m_playerY < e.y + eh) && (m_playerY + SHIP_HEIGHT > e.y);

            if (e.active && m_invincibleTimer <= 0.0f && overlapX && overlapY) {
                e.active = false;
                OnPlayerHit(); // 触发中弹暂停
                return;
            }
        }

        // 5. 更新 Boss 移动与多弹幕射击
        if (m_boss.active) {
            if (m_boss.x > LCD_WIDTH - BOSS_WIDTH - 2) {
                m_boss.x -= 15.0f * dt;
            } else {
                m_boss.y += m_boss.vy * dt;
                if (m_boss.y < 9 || m_boss.y > LCD_HEIGHT - BOSS_HEIGHT - 1) {
                    m_boss.vy = -m_boss.vy; // 上下反弹
                }
            }

            m_boss.shootTimer += dt;
            if (m_boss.shootTimer >= 0.8f) {
                m_boss.shootTimer = 0.0f;
                // 上下双路直线红弹
                m_bullets.push_back({ m_boss.x, m_boss.y + 2, -2.5f, 0.0f, false, 1 });
                m_bullets.push_back({ m_boss.x, m_boss.y + BOSS_HEIGHT - 2, -2.5f, 0.0f, false, 1 });
                // 斜向散射红弹
                if (rand() % 2 == 0) {
                    m_bullets.push_back({ m_boss.x, m_boss.y + BOSS_HEIGHT / 2, -2.2f, -0.5f, false, 1 });
                    m_bullets.push_back({ m_boss.x, m_boss.y + BOSS_HEIGHT / 2, -2.2f, 0.5f, false, 1 });
                }
            }
        }

        // 6. 子弹碰撞检测 (子弹 vs 玩家、子弹 vs 敌机/Boss)
        for (auto& b : m_bullets) {
            // (1) 敌方红弹击中玩家 (AABB 精确判定)
            if (!b.isPlayer) {
                if (m_invincibleTimer <= 0.0f &&
                    b.x >= m_playerX - 1.0f && b.x <= m_playerX + SHIP_WIDTH &&
                    b.y >= m_playerY - 1.0f && b.y <= m_playerY + SHIP_HEIGHT) {
                    b.x = -99.0f;
                    OnPlayerHit(); // 触发中弹暂停
                    return;
                }
                continue;
            }

            // (2) 玩家蓝光击中敌机 (AABB 矩形包围盒精确判定，解决轰炸机大尺寸穿透问题)
            for (auto& e : m_enemies) {
                if (!e.active) continue;

                int ew = (e.type == ENEMY_BOMBER) ? BOMBER_WIDTH : 
                         ((e.type == ENEMY_SCOUT) ? SCOUT_WIDTH : 4);
                int eh = (e.type == ENEMY_BOMBER) ? BOMBER_HEIGHT : 
                         ((e.type == ENEMY_SCOUT) ? SCOUT_HEIGHT : 4);

                // 精确判定子弹点 (b.x, b.y) 是否位于敌机矩形 [e.x-1, e.x+ew] x [e.y-1, e.y+eh] 内
                if (b.x >= e.x - 1.0f && b.x <= e.x + ew &&
                    b.y >= e.y - 1.0f && b.y <= e.y + eh) {
                    float hitX = b.x, hitY = b.y;
                    b.x = 999.0f; // 回收该子弹
                    e.hp -= b.damage;
                    SpawnExplosion(hitX, hitY, 3); // 产生受击火花

                    if (e.hp <= 0) {
                        e.active = false;
                        m_score += e.scoreValue;
                        SaveHighScore(m_score);
                        SpawnExplosion(e.x + ew / 2, e.y + eh / 2, 12);

                        // 25% 概率掉落道具
                        if (rand() % 4 == 0) {
                            PowerUpType ptype = (rand() % 3 == 0) ? POWER_WEAPON :
                                                (rand() % 2 == 0 ? POWER_HEALTH : POWER_BOMB);
                            m_powerUps.push_back({ e.x + ew / 2, e.y + eh / 2, -12.0f, ptype, true });
                        }
                    }
                    break;
                }
            }

            // (3) 玩家蓝光击中 Boss
            if (m_boss.active && b.x >= m_boss.x && b.x <= m_boss.x + BOSS_WIDTH &&
                b.y >= m_boss.y && b.y <= m_boss.y + BOSS_HEIGHT) {
                b.x = 999;
                m_boss.hp -= b.damage;
                SpawnExplosion(b.x, b.y, 3);
                if (m_boss.hp <= 0) {
                    m_boss.active = false;
                    m_score += 2000 * m_currentStage;
                    SaveHighScore(m_score);
                    SpawnExplosion(m_boss.x + BOSS_WIDTH / 2, m_boss.y + BOSS_HEIGHT / 2, 30);
                    m_state = (m_currentStage >= 2) ? STATE_VICTORY : STATE_LEVEL_CLEAR;
                }
            }
        }
    }

    // ---------------------------------------------------------------------------------
    // LCD 界面渲染管线 (LCD Display Layer Assembly & HUD)
    // ---------------------------------------------------------------------------------

    // 绘制顶部 HUD 状态栏 (分数 SC, 生命值 HP:3, 炸弹数 B)
    void RenderHUD() {
        DrawLine(0, 7, LCD_WIDTH - 1, 7, 1); // HUD 分割线

        std::stringstream ss;
        ss << "SC:" << m_score;
        DrawStringLCD(1, 1, ss.str(), 1);

        std::string hpStr = "HP:";
        for (int i = 0; i < m_playerMaxHp; i++) {
            hpStr += (i < m_playerHp) ? "#" : ".";
        }
        DrawStringLCD(32, 1, hpStr, 1);

        std::string bombStr = "B:";
        for (int i = 0; i < m_bombCount; i++) bombStr += "*";
        DrawStringLCD(62, 1, bombStr, 1);
    }

    // 根据当前 State 填充点阵内存 `m_pixelGrid`
    void RenderLCD() {
        ClearPixelGrid();

        // 绘制背景星空
        for (const auto& s : m_stars) {
            DrawPixel((int)s.x, (int)s.y, 1);
        }

        if (m_state == STATE_TITLE) {
            DrawStringLCD(14, 8, "SPACE IMPACT", 1);
            DrawStringLCD(18, 16, "NOKIA 3310", 1);
            DrawBitmap(LCD_WIDTH / 2 - SHIP_WIDTH / 2, 24, SHIP_WIDTH, SHIP_HEIGHT, SHIP_BITMAP, 1);
            DrawStringLCD(10, 34, "PRESS SPACE", 1);
            DrawStringLCD(16, 41, "TO START", 1);
        } else if (m_state == STATE_PLAYING || m_state == STATE_PAUSED || m_state == STATE_PLAYER_HIT) {
            RenderHUD();

            // 绘制敌机
            for (const auto& e : m_enemies) {
                if (!e.active) continue;
                if (e.type == ENEMY_SCOUT) {
                    DrawBitmap((int)e.x, (int)e.y, SCOUT_WIDTH, SCOUT_HEIGHT, SCOUT_BITMAP, 1);
                } else if (e.type == ENEMY_BOMBER) {
                    DrawBitmap((int)e.x, (int)e.y, BOMBER_WIDTH, BOMBER_HEIGHT, BOMBER_BITMAP, 1);
                } else {
                    DrawRect((int)e.x, (int)e.y, 4, 4, true, 1);
                }
            }

            // 绘制 Boss 母舰与血条
            if (m_boss.active) {
                DrawBitmap((int)m_boss.x, (int)m_boss.y, BOSS_WIDTH, BOSS_HEIGHT, BOSS_BITMAP, 1);
                int barW = (m_boss.hp * (LCD_WIDTH - 20)) / m_boss.maxHp;
                DrawRect(10, LCD_HEIGHT - 3, barW, 2, true, 1);
            }

            // -------------------------------------------------------------------------
            // 子弹区分绘制逻辑 (2: 玩家蓝光束 "--", 3: 敌方红弹丸 2x2)
            // -------------------------------------------------------------------------
            for (const auto& b : m_bullets) {
                int bx = (int)b.x;
                int by = (int)b.y;
                if (b.isPlayer) {
                    // 玩家蓝光束
                    DrawPixel(bx, by, 2);
                    DrawPixel(bx - 1, by, 2);
                } else {
                    // 敌方红弹丸 (2x2)
                    DrawPixel(bx, by, 3);
                    DrawPixel(bx + 1, by, 3);
                    DrawPixel(bx, by + 1, 3);
                    DrawPixel(bx + 1, by + 1, 3);
                }
            }

            // 绘制道具
            for (const auto& p : m_powerUps) {
                if (!p.active) continue;
                const char* icon = (p.type == POWER_WEAPON) ? "P" : (p.type == POWER_HEALTH ? "H" : "B");
                DrawStringLCD((int)p.x, (int)p.y, icon, 1);
            }

            // 绘制爆炸粒子
            for (const auto& pt : m_particles) {
                DrawPixel((int)pt.x, (int)pt.y, 1);
            }

            // 绘制玩家飞船
            if (m_state != STATE_PLAYER_HIT) {
                if (m_invincibleTimer <= 0.0f || ((int)(m_invincibleTimer * 10) % 2 == 0)) {
                    DrawBitmap((int)m_playerX, (int)m_playerY, SHIP_WIDTH, SHIP_HEIGHT, SHIP_BITMAP, 1);
                }
            }

            // 绘制中弹暂停对话框 (SHIP HIT! PRESS SPACE)
            if (m_state == STATE_PLAYER_HIT) {
                DrawRect(10, 14, 64, 22, true, 0);  // 黑色底框遮罩
                DrawRect(10, 14, 64, 22, false, 1); // 白色边框
                DrawStringLCD(18, 17, "SHIP HIT!", 1);
                DrawStringLCD(12, 27, "PRESS SPACE", 1);
            } else if (m_state == STATE_PAUSED) {
                DrawRect(22, 18, 40, 14, true, 0);
                DrawRect(22, 18, 40, 14, false, 1);
                DrawStringLCD(28, 22, "PAUSED", 1);
            }
        } else if (m_state == STATE_LEVEL_CLEAR) {
            DrawStringLCD(16, 12, "STAGE CLEAR!", 1);
            std::stringstream ss;
            ss << "SCORE:" << m_score;
            DrawStringLCD(14, 24, ss.str(), 1);
            DrawStringLCD(10, 36, "PRESS SPACE", 1);
        } else if (m_state == STATE_GAME_OVER) {
            DrawStringLCD(20, 10, "GAME OVER", 1);
            std::stringstream ss;
            ss << "SCORE:" << m_score;
            DrawStringLCD(14, 22, ss.str(), 1);

            std::stringstream ssH;
            ssH << "HIGH:" << g_highScore;
            DrawStringLCD(16, 30, ssH.str(), 1);

            DrawStringLCD(10, 40, "PRESS SPACE", 1);
        } else if (m_state == STATE_VICTORY) {
            DrawStringLCD(16, 10, "VICTORY!", 1);
            DrawStringLCD(6, 20, "EARTH SAVED!", 1);
            std::stringstream ss;
            ss << "FINAL:" << m_score;
            DrawStringLCD(14, 30, ss.str(), 1);
            DrawStringLCD(10, 40, "PRESS SPACE", 1);
        }
    }

    // 单帧驱动入口
    void Update(float dt) {
        UpdateInput(dt);
        SpawnEnemies(dt);
        UpdateEntities(dt);
        RenderLCD();
    }
};

// 实例实例化全局游戏引擎对象
SpaceImpactGame g_game;

// =====================================================================================
// Win32 窗口消息回调函数 (Window Procedure - WndProc)
// 说明：处理离屏 GDI 双缓冲绘制与 3310 复古机身/按键说明的渲染
// =====================================================================================
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            // 拦截背景擦除消息，直接返回 1 阻止 Win32 默认刷屏，根除闪烁 (Flicker Free)
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            // -------------------------------------------------------------------------
            // GDI 离屏双缓冲管线 (GDI Double Buffering)
            // -------------------------------------------------------------------------
            HDC memDC = CreateCompatibleDC(hdc); // 创建兼容内存 DC
            HBITMAP memBitmap = CreateCompatibleBitmap(hdc, WINDOW_WIDTH, WINDOW_HEIGHT); // 创建兼容内存位图
            HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap); // 挂载位图

            // 1. 绘制最外层桌面背景 (深灰蓝色)
            HBRUSH winBgBrush = CreateSolidBrush(RGB(20, 25, 35));
            RECT winRect = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
            FillRect(memDC, &winRect, winBgBrush);
            DeleteObject(winBgBrush);

            // 2. 绘制诺基亚 3310 圆角藏青色机身
            HBRUSH phoneBodyBrush = CreateSolidBrush(COLOR_PHONE_BODY);
            HPEN phoneBorderPen = CreatePen(PS_SOLID, 3, COLOR_PHONE_TRIM);
            SelectObject(memDC, phoneBodyBrush);
            SelectObject(memDC, phoneBorderPen);
            RoundRect(memDC, 20, 15, WINDOW_WIDTH - 20, WINDOW_HEIGHT - 25, 60, 60);
            DeleteObject(phoneBodyBrush);
            DeleteObject(phoneBorderPen);

            // 绘制顶部 NOKIA 金属 Logo
            SetBkMode(memDC, TRANSPARENT);
            SetTextColor(memDC, RGB(220, 225, 230));
            HFONT logoFont = CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Arial");
            HFONT oldFont = (HFONT)SelectObject(memDC, logoFont);
            TextOutW(memDC, WINDOW_WIDTH / 2 - 42, 42, L"NOKIA", 5);
            SelectObject(memDC, oldFont);
            DeleteObject(logoFont);

            // 绘制听筒网格
            HBRUSH speakerBrush = CreateSolidBrush(RGB(15, 20, 30));
            SelectObject(memDC, speakerBrush);
            RoundRect(memDC, WINDOW_WIDTH / 2 - 35, 30, WINDOW_WIDTH / 2 + 35, 36, 6, 6);
            DeleteObject(speakerBrush);

            // 3. 渲染 84x48 LCD 点阵屏幕区域
            int lcdLeft = PHONE_MARGIN_X;
            int lcdTop = PHONE_MARGIN_TOP;
            int lcdRight = lcdLeft + LCD_WIDTH * PIXEL_SCALE;
            int lcdBottom = lcdTop + LCD_HEIGHT * PIXEL_SCALE;

            // LCD 外遮罩边框
            HBRUSH lcdFrameBrush = CreateSolidBrush(RGB(10, 15, 20));
            RECT lcdFrameRect = { lcdLeft - 10, lcdTop - 10, lcdRight + 10, lcdBottom + 10 };
            FillRect(memDC, &lcdFrameRect, lcdFrameBrush);
            DeleteObject(lcdFrameBrush);

            // LCD 黄绿背光底色
            HBRUSH lcdBgBrush = CreateSolidBrush(COLOR_LCD_BG);
            RECT lcdRect = { lcdLeft, lcdTop, lcdRight, lcdBottom };
            FillRect(memDC, &lcdRect, lcdBgBrush);
            DeleteObject(lcdBgBrush);

            // 遍历 84x48 点阵内存并按类型调色渲染
            HBRUSH pixelOnBrush      = CreateSolidBrush(COLOR_LCD_PIXEL);
            HBRUSH pixelOffBrush     = CreateSolidBrush(COLOR_LCD_GRID);
            HBRUSH bulletPlayerBrush = CreateSolidBrush(COLOR_BULLET_PLAYER); // 蓝弹
            HBRUSH bulletEnemyBrush  = CreateSolidBrush(COLOR_BULLET_ENEMY);  // 红弹

            for (int r = 0; r < LCD_HEIGHT; r++) {
                for (int c = 0; c < LCD_WIDTH; c++) {
                    int px = lcdLeft + c * PIXEL_SCALE;
                    int py = lcdTop + r * PIXEL_SCALE;
                    RECT pr = { px, py, px + PIXEL_SCALE - 1, py + PIXEL_SCALE - 1 };

                    uint8_t cell = g_game.m_pixelGrid[r][c];
                    if (cell == 1) {
                        FillRect(memDC, &pr, pixelOnBrush);      // 暗绿标准像素
                    } else if (cell == 2) {
                        FillRect(memDC, &pr, bulletPlayerBrush); // 玩家蓝光
                    } else if (cell == 3) {
                        FillRect(memDC, &pr, bulletEnemyBrush);  // 敌方红弹
                    } else {
                        FillRect(memDC, &pr, pixelOffBrush);     // 背景未点亮网格
                    }
                }
            }
            DeleteObject(pixelOnBrush);
            DeleteObject(pixelOffBrush);
            DeleteObject(bulletPlayerBrush);
            DeleteObject(bulletEnemyBrush);

            // 4. 绘制机身下方操作按键提示说明
            SetTextColor(memDC, RGB(220, 230, 240));
            HFONT uiFont = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Consolas");
            SelectObject(memDC, uiFont);

            const wchar_t* hintLine1 = L"W / A / S / D / Arrow Keys : Move Ship";
            const wchar_t* hintLine2 = L"SPACE / J : Fire     K / B : Bomb";
            const wchar_t* hintLine3 = L"P : Pause            ESC : Menu / Quit";

            TextOutW(memDC, WINDOW_WIDTH / 2 - 170, lcdBottom + 30, hintLine1, (int)wcslen(hintLine1));
            TextOutW(memDC, WINDOW_WIDTH / 2 - 170, lcdBottom + 60, hintLine2, (int)wcslen(hintLine2));
            TextOutW(memDC, WINDOW_WIDTH / 2 - 170, lcdBottom + 90, hintLine3, (int)wcslen(hintLine3));

            SelectObject(memDC, oldFont);
            DeleteObject(uiFont);

            // 【关键快照传输】BitBlt 将内存 DC 一次性考拷贝到物理窗口设备 HDC
            BitBlt(hdc, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, memDC, 0, 0, SRCCOPY);

            // 释放 GDI 句柄
            SelectObject(memDC, oldBitmap);
            DeleteObject(memBitmap);
            DeleteDC(memDC);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            PostQuitMessage(0); // 退出 Win32 消息循环
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// =====================================================================================
// WinMain 入口点与固定 60 FPS 游戏主循环 (Fixed Timestep Game Loop)
// 说明：结合 chrono 高精度时间戳计算 Delta Time (dt) 并通过 sleep 锁帧
// =====================================================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 1. 初始化引擎
    g_game.Init();

    const wchar_t CLASS_NAME[] = L"Nokia3310_SpaceImpactWindow";

    // 2. 注册 Windows 窗口类
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClassW(&wc);

    // 计算精准 Client 区域尺寸
    RECT wr = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
    AdjustWindowRect(&wr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);

    // 3. 创建原生 Win32 桌面窗口
    HWND hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"Nokia 3310 - Space Impact",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top,
        NULL, NULL, hInstance, NULL
    );

    if (!hwnd) return 0;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // 4. 固定 60 FPS 游戏主循环 (Fixed Step Game Loop)
    MSG msg = {};
    auto lastTime = chrono::high_resolution_clock::now();
    const float targetFrameTime = 1.0f / 60.0f; // 目标每帧耗时 16.67 毫秒

    while (msg.message != WM_QUIT) {
        // (1) 处理系统 Windows 消息
        if (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        } else {
            // (2) 计算微秒级帧间隔 Delta Time
            auto currentTime = chrono::high_resolution_clock::now();
            float dt = chrono::duration<float>(currentTime - lastTime).count();
            lastTime = currentTime;

            // 限制最大 dt (防止切屏或卡顿导致穿墙)
            if (dt > 0.1f) dt = 0.1f;

            // (3) 驱动引擎更新与重绘
            g_game.Update(dt);
            InvalidateRect(hwnd, NULL, FALSE); // 触发 WM_PAINT

            // (4) 锁帧 Sleeper：若计算耗时小于 16.67ms 则让出 CPU
            auto elapsed = chrono::duration<float>(chrono::high_resolution_clock::now() - currentTime).count();
            if (elapsed < targetFrameTime) {
                this_thread::sleep_for(chrono::milliseconds((int)((targetFrameTime - elapsed) * 1000)));
            }
        }
    }

    return (int)msg.wParam;
}

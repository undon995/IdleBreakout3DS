// Idle Breakout 3DS — native Nintendo 3DS homebrew adaptation.
// Original Idle Breakout artwork and audio remain the property of their owners.
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include "assets.h"
#include "audio_data.h"

#define TOP_W 400
#define BOT_W 320
#define HEIGHT 240
#define COLS 15
#define ROWS 20
#define BTYPES 8
#define MAX_BALLS 120
#define FIRST_BRICK_X 12
#define FIRST_BRICK_Y 49
#define STEP_X 25
#define STEP_Y 8
#define BW 24
#define BH 7
#define GAME_SAVE_OLD "sdmc:/3ds/IdleBreakout3DS/save.bin"
#define GAME_SAVE_V3 "sdmc:/3ds/IdleBreakout3DS/save_v3.bin"
#define GAME_SAVE "sdmc:/3ds/IdleBreakout3DS/save_v5.bin"
#define GAME_SAVE_BAK "sdmc:/3ds/IdleBreakout3DS/save_v5.bak"
#define GAME_SAVE_V4 "sdmc:/3ds/IdleBreakout3DS/save_v4.bin"
#define EXPORT_SAVE "sdmc:/3ds/IdleBreakout3DS/export_v5.bin"
#define IMPORT_SAVE "sdmc:/3ds/IdleBreakout3DS/import_v5.bin"
#define SAVES_DIR "sdmc:/3ds/IdleBreakout3DS/saves"
#define SAVE_LIST_CAP 64
#define SAVE_PAGE_ROWS 6
#define MAGIC 0x49444232UL
#define SAVE_VERSION 5
#define SHOP_PAGES 10
#define MILESTONE 600

#define PI 3.14159265358979323846f

typedef struct { float x,y,vx,vy, boost; signed char type; unsigned char active,sub,ttl,hitCooldown; } Ball;
typedef struct { double hp,maxhp; unsigned char poison,black,palette,gold; } Brick;
// v2 layout is preserved to migrate the player's existing progress.
typedef struct {
 uint32_t magic,version,level,prestige;
 double money, lifetime;
 uint32_t owned[BTYPES], power[BTYPES], speed[BTYPES];
 uint32_t soundEnabled, checksum;
} LegacySave;
// Native V3 save layout: read-only migration source. The old file remains untouched.
typedef struct {
 uint32_t magic,version,level,prestige;
 double money, lifetime;
 uint32_t owned[BTYPES], power[BTYPES], speed[BTYPES];
 uint32_t soundEnabled,prestigePoints,prestigeUp[4];
 uint32_t bestLevel,claimedMilestone,pendingReward,activePowerup,powerupFrames,perksClaimed;
 uint32_t checksum;
} SaveV3;
typedef struct {
 uint32_t magic,version,level,prestige;
 double money, lifetime;
 uint32_t owned[BTYPES], power[BTYPES], speed[BTYPES];
 uint32_t soundEnabled;
 uint32_t prestigePoints;
 uint32_t prestigeUp[4];
 uint32_t bestLevel, claimedMilestone;
 uint32_t pendingReward;      // Unspent original Power Up credits (earned at 600-brick milestones)
 uint32_t activePowerup;      // 0 none; 1 Cash, 2 Click, 3 Demon, 4 Snow, 5 Gold
 uint32_t powerupFrames;
 uint32_t perksClaimed;       // Number of one-time power-ups activated this run
 uint32_t bricksBroken;       // Bricks destroyed this run
 uint32_t powerupsUsedMask;  // 5 once-per-run original Power Ups
 uint32_t checksum;
} SaveV4;
typedef struct {
 uint32_t magic,version,level,prestige;
 double money, lifetime;
 uint32_t owned[BTYPES], power[BTYPES], speed[BTYPES];
 uint32_t soundEnabled,prestigePoints,prestigeUp[4];
 uint32_t bestLevel,claimedMilestone,pendingReward,activePowerup,powerupFrames;
 uint32_t perksClaimed,bricksBroken,powerupsUsedMask;
 // New permanent prestige + battle progression, and UI preferences.
 uint32_t capUpgrade,laserPower,blackBricks,bossWins,skillPoints,skillMask;
 uint32_t pendingGold,bossActive,bgColor,effectsEnabled;
 int64_t lastSaveEpoch;
 uint32_t checksum;
} Save;
typedef struct { unsigned char *fb; int w; } Screen;

static Brick bricks[ROWS][COLS];
static Ball balls[MAX_BALLS];
static Save saveData;
static int frame=0,page=0,chosenX=5,chosenY=3,refreshBottom=1,unsaved=0;
static int saveState=0,saveStateFrame=0,confirmRebirth=0;
typedef struct { float pos,dir; int pulse; } Vaus;
static Vaus vaus[4]={{85,1,0},{170,1,0},{135,-1,0},{260,-1,0}};
static int laserX=-1,laserY=0,laserFrames=0;
static unsigned bossLevelIndex=1;
static double offlineEarnings=0;
static int activeBalls=0;
// Factory reset requires a dedicated warning screen and a second intentional tap.
// It does NOT erase exported backups in /saves/.
static int settingsStatus=0,factoryResetConfirm=0,rewardNoticeUntil=0,rewardNoticeType=0;
// Save browser: .bin files are listed from SD:/3ds/IdleBreakout3DS/saves/.
// These variables deliberately are NOT part of the on-disk save layout.
static char saveList[SAVE_LIST_CAP][64];
static int saveListCount=0,saveListPage=0,saveSelected=-1,saveBrowserOpen=0,importConfirm=0;
static char lastExportName[64]="";

static int bossLevel=0,perfMode=1; // 1 = low-effects mode on Old 3DS
// Developer-only test switches do not enter the persistent save format.
// Infinite credits/points and early rebirths apply only while this is enabled.
static int testMode=1;
static unsigned lastSoundFrame[12];
static const unsigned char ballSound[BTYPES]={0,1,2,10,3,4,5,6};
// Sprite mapping follows the original game artwork:
// 0 Basic (yellow), 1 Plasma (magenta), 2 Sniper (cross),
// 3 Scatter (orange bars), 4 Cannonball (skull), 5 Poison (red),
// 6 Snow (white), 7 Money (green $), 8 orange ball projectile.
// The last two are powerup/reference artwork, not regular shop purchases.
static const int ballVisual[BTYPES]={SPR_BALL0,SPR_BALL1,SPR_BALL2,SPR_BALL3,SPR_BALL4,SPR_BALL5,SPR_BALL6,SPR_BALL7};

static unsigned scatterShots=0;
static char topPrices[6][32];
static unsigned char topBgCache[TOP_W*HEIGHT*3];
static unsigned char bottomCache[BOT_W*HEIGHT*3];
static int bottomValid=0;
// Level layout masks follow the original game patterns.
static const char *levelColumns[5]={
 "001111101111100","000001111100000","011111000111110","100000111000001","001111101111100"};
static const char *levelRows[5]={
 "01100110011001100110","11111111111111111111","00000111111111100000",
 "11111111111111111111","00001111100111110000"};
static const char *specialRows[20]={
 "000000000000000","000000000000000","000001000100000","000011101110000",
 "000011111110000","000111111111000","000111111111000","000111111111000",
 "000111111111000","000111111111000","000111111111000","000011111110000",
 "000011111110000","000001111100000","000001111100000","000000111000000",
 "000000111000000","000000010000000","000000000000000","000000000000000"};
static uint32_t rng=0x873fa924;
static int audioReady=0;
static void *audioMem[12];
static ndspWaveBuf wav[5];
static uint16_t audioCursor=0;
static const char *ballNames[BTYPES]={"BASIC BALL","PLASMA BALL","SNIPER BALL","SCATTER BALL","CANNONBALL","POISON BALL","SNOW BALL","MONEY BALL"};
static const char *ballHelp[BTYPES]={"STANDARD BALL","25% SPLASH","HOMING","EXTRA SHOTS","BREAK THROUGH","2X BRICK DMG","SPEED GROWS","10X MONEY"};
static const double ballBase[BTYPES]={25,200,1500,10000,75000,75000,25000,25000};
static const double powerBase[BTYPES]={250,1250,8000,100000,150000,130000,150000,90000};
static const double speedBase[BTYPES]={100,800,7500,100000,90000,110000,35000,35000};
static const float baseVel[BTYPES]={2.0f,2.0f,2.5f,2.0f,2.1f,2.1f,2.0f,2.5f};
static const float baseDamage[BTYPES]={1,3,5,10,50,5,1,1};
// Upgrade names and descriptions follow the original game.
static const char *perkNames[5]={"CASHED UP","CLICK FRENZY","DEMON CORE","SNOW BALL","2X GOLD"};
static const char *perkHelp[5]={"DOUBLE MONEY","10X CLICK + SHOTS","WAVES OF DAMAGE","BOUNCE BOOST","NEXT GOLD BRICK"};
static const char *prestigeNames[5]={"CASH BONUS", "BALL SPEED", "BALL POWER", "VAUS LASERS", "BALL LIMIT"};
static const unsigned prestigeCostBase[5]={2,5,5,10,4};
// V1 boss roster and HP values from original-game reference; see BossSpec for adapted combat rules.
static const char *bossNames[50]={
 "BLOCK HEAD",
 "BLOCKY BALBOA",
 "BLOCKO",
 "D BLOCK",
 "BLOCKBEARD",
 "PLYMOUTH BLOCK",
 "ICE BLOCK",
 "BLOCKWORK ORANGE",
 "BLOCKA DOODLE DOO",
 "COUNT BLOCKULA",
 "BLOCKODILE",
 "BLOCKED NOSE",
 "BLOCKTOPUS",
 "BLOCK HOLLIDAY",
 "ROBLOCK",
 "Z BLOCK",
 "CODE BLOCK",
 "CALL THE BLOCKTOR",
 "WRITERS BLOCK",
 "J BLOCK",
 "BLOCKED PIPES",
 "SHERBLOCK",
 "BLOCKED ARTERY",
 "MENTAL BLOCK",
 "S BLOCK",
 "BLOCKED TRAFFIC",
 "BLOCK FOOTAGE",
 "CELL BLOCK",
 "BLOCK LOAF",
 "ROAD BLOCK",
 "BLOCK OF CHEESE",
 "BLOCKTOMAN",
 "BLOCKARINA OF TIME",
 "BLOCKENSPIEL",
 "BLOCK NOODLES",
 "T BLOCK",
 "KNIFE BLOCK",
 "HAM BLOCK",
 "L BLOCK",
 "SHAMBLOCK",
 "COMBO BLOCK",
 "HELIBLOCKTER",
 "BLOCKCHAIN",
 "O BLOCK",
 "BLOCK AND KEY",
 "BLOCK OF LAND",
 "BLOCKED SIGNAL",
 "POWER BLOCK",
 "BLOCKED VISION",
 "I BLOCK"
};
static const double bossHp[50]={5000000.0f,10000000.0f,20000000.0f,35000000.0f,50000000.0f,75000000.0f,100000000.0f,150000000.0f,200000000.0f,250000000.0f,300000000.0f,350000000.0f,400000000.0f,450000000.0f,500000000.0f,800000000.0f,850000000.0f,900000000.0f,950000000.0f,1000000000.0f,1050000000.0f,1100000000.0f,1150000000.0f,1200000000.0f,1250000000.0f,1300000000.0f,1350000000.0f,1400000000.0f,1450000000.0f,1500000000.0f,1550000000.0f,1600000000.0f,1650000000.0f,1700000000.0f,1750000000.0f,1800000000.0f,1850000000.0f,1900000000.0f,1950000000.0f,2000000000.0f,2050000000.0f,2100000000.0f,2150000000.0f,2200000000.0f,2250000000.0f,2300000000.0f,2350000000.0f,2400000000.0f,2450000000.0f,2500000000.0f};
static int bossIndex(void){return (saveData.bossWins<50u)?(int)saveData.bossWins:49;}

// V1 boss abilities from the community reference, adapted for the 3DS engine.
// Exact freeze/attack patterns and boss-rush lives are still under development.
typedef struct {
 uint8_t portrait,laserImmune,doubleMask,halfMask,regenPct,freezeStyle;
 uint16_t timeLimit;
 uint8_t shield,attack;
} BossSpec;
static const BossSpec bossSpec[50]={
 {3,1,0,0,0,0,0,0,0},
 {0,0,1,4,0,0,0,0,0},
 {1,1,0,2,1,0,0,0,0},
 {2,0,3,12,1,0,0,0,0},
 {3,1,32,0,1,2,0,0,1},
 {0,1,0,48,5,0,225,0,0},
 {4,1,4,1,1,1,0,0,0},
 {5,1,4,8,10,0,300,0,0},
 {6,0,8,1,1,1,0,0,0},
 {7,0,0,24,10,1,0,0,1},
 {8,1,1,34,1,0,200,1,0},
 {9,0,36,10,1,2,0,0,0},
 {10,0,2,4,10,0,0,0,0},
 {11,0,24,0,0,0,0,0,1},
 {12,1,18,4,5,1,0,1,0},
 {13,0,40,0,10,0,180,0,0},
 {14,1,16,10,0,0,0,0,0},
 {15,1,34,8,1,1,0,0,0},
 {16,0,5,40,5,0,0,0,0},
 {17,1,10,16,10,0,0,0,1},
 {18,1,4,34,0,0,150,0,0},
 {19,0,40,1,1,0,0,0,0},
 {20,1,17,10,5,0,0,0,0},
 {21,1,34,4,10,1,0,0,0},
 {22,0,1,40,0,0,0,0,0},
 {23,1,10,16,1,1,125,0,1},
 {24,1,20,34,5,0,0,0,0},
 {25,0,40,1,10,0,0,0,0},
 {26,1,16,40,0,0,0,0,1},
 {27,1,34,4,1,1,0,0,0},
 {28,0,5,40,5,0,100,0,0},
 {29,1,10,16,10,0,0,0,1},
 {30,1,4,34,0,0,0,0,0},
 {31,0,34,1,1,0,0,0,0},
 {32,1,17,10,5,0,0,0,0},
 {33,1,34,4,10,2,75,0,0},
 {34,0,1,40,0,0,0,0,0},
 {35,1,10,16,1,2,0,0,1},
 {36,1,20,34,5,0,0,0,0},
 {37,0,40,1,10,0,0,0,0},
 {38,1,16,10,0,0,50,0,1},
 {39,1,34,4,1,1,0,0,0},
 {40,0,5,40,5,0,0,0,0},
 {41,1,10,16,10,2,0,0,1},
 {42,1,4,34,0,0,0,0,0},
 {43,0,40,1,1,0,25,0,0},
 {44,1,17,10,5,0,0,0,0},
 {45,1,34,4,10,1,0,0,0},
 {46,0,1,40,0,0,0,0,0},
 {47,1,10,16,1,2,0,0,0}
};
static int bossTimer=0, bossFreezeTimer=0, bossFrozenType=-1;
static unsigned bossAttackStep=0;
static const BossSpec* currentBoss(void){return &bossSpec[bossIndex()];}
static int bossPortrait(void){return SPR_MOB0+(int)currentBoss()->portrait;}



static unsigned rand32(void){rng ^=rng<<13;rng ^=rng>>17;rng ^=rng<<5;return rng;}
static float randf(void){return (rand32()&0xffff)/65535.0f;}
static float minf(float a,float b){return a<b?a:b;}
static float maxf(float a,float b){return a>b?a:b;}
static double mind(double a,double b){return a<b?a:b;}

static inline void dot(Screen *s,int x,int y,int r,int g,int b){
 if((unsigned)x>=(unsigned)s->w||(unsigned)y>=HEIGHT)return;
 unsigned char *p=s->fb+(x*HEIGHT+(HEIGHT-1-y))*3;
 p[0]=(unsigned char)b;p[1]=(unsigned char)g;p[2]=(unsigned char)r;
}
static void box(Screen *s,int x,int y,int w,int h,int r,int g,int b){
 int x0=x<0?0:x,y0=y<0?0:y,x1=x+w>s->w?s->w:x+w,y1=y+h>HEIGHT?HEIGHT:y+h;
 if(x1<=x0||y1<=y0)return;
 for(int xx=x0;xx<x1;xx++){
  unsigned char *p=s->fb+(xx*HEIGHT+(HEIGHT-y1))*3;
  for(int yy=y1-1;yy>=y0;yy--,p+=3){p[0]=b;p[1]=g;p[2]=r;}
 }
}
static void outline(Screen *s,int x,int y,int w,int h,int r,int g,int b){
 box(s,x,y,w,1,r,g,b);box(s,x,y+h-1,w,1,r,g,b);
 box(s,x,y,1,h,r,g,b);box(s,x+w-1,y,1,h,r,g,b);
}
static void blit(Screen *s,int idx,int dx,int dy,int dw,int dh){
 if(idx<0||idx>=SPR_COUNT||dw<=0||dh<=0)return;
 const Sprite *sp=&sprites[idx];
 int x0=dx<0?0:dx,y0=dy<0?0:dy;
 int x1=dx+dw>s->w?s->w:dx+dw, y1=dy+dh>HEIGHT?HEIGHT:dy+dh;
 if(x1<=x0||y1<=y0)return;
 // Write directly to the 3DS' column-major BGR8 framebuffer (no per-pixel dot calls).
 for(int x=x0;x<x1;x++){
  int sx=(x-dx)*sp->w/dw;
  unsigned char *p=s->fb+(x*HEIGHT+(HEIGHT-y1))*3;
  for(int y=y1-1;y>=y0;y--,p+=3){
   int sy=(y-dy)*sp->h/dh;
   uint16_t color=sp->p[sy*sp->w+sx];
   if(!(color&0x8000))continue;
   p[2]=(unsigned char)(((color>>10)&31)*255/31);
   p[1]=(unsigned char)(((color>>5)&31)*255/31);
   p[0]=(unsigned char)((color&31)*255/31);
  }
 }
}
static void writing(Screen *s,int x,int y,const char *text,int r,int g,int b,int scale){
 for(;*text;text++,x+=(8*scale)){
  int ch=(unsigned char)*text;if(ch<32||ch>126)ch='?';
  for(int j=0;j<10;j++){
   unsigned line=font_7x10[(ch-32)*10+j];
   for(int i=0;i<7;i++)if(line&(1<<i)){
    if(scale==1)dot(s,x+i,y+j,r,g,b);
    else box(s,x+i*scale,y+j*scale,scale,scale,r,g,b);
   }
  }
 }
}
static void smallWriting(Screen *s,int x,int y,const char *text,int r,int g,int b){
 // Fits four characters in a 24-pixel brick, even at high levels.
 for(;*text;text++,x+=5){
  int ch=(unsigned char)*text;if(ch<32||ch>126)ch='?';
  for(int j=0;j<7;j++){
   unsigned line=font_7x10[(ch-32)*10+(j*10)/7];
   for(int i=0;i<4;i++)if(line&(1<<((i*7)/4)))dot(s,x+i,y+j,r,g,b);
  }
 }
}
static void numeric(double val,char *buf,size_t cap){
 double ab=fabs(val);const char *suffix="";
 if(ab>=1.e15){val/=1.e15;suffix="Qa";}
 else if(ab>=1.e12){val/=1.e12;suffix="T";}
 else if(ab>=1.e9){val/=1.e9;suffix="B";}
 else if(ab>=1.e6){val/=1.e6;suffix="M";}
 else if(ab>=1.e3){val/=1.e3;suffix="K";}
 if(fabs(val)>=100||suffix[0]==0)snprintf(buf,cap,"%.0f%s",val,suffix);
 else snprintf(buf,cap,"%.1f%s",val,suffix);
}
static unsigned checksumBytes(const void *data,size_t size){
 const unsigned char *p=(const unsigned char*)data;unsigned hash=2166136261u;
 for(size_t i=0;i<size;i++){hash^=p[i];hash*=16777619u;}return hash;
}
static unsigned checksum(const Save *s){return checksumBytes(s,offsetof(Save,checksum));}
static unsigned checksumV3(const SaveV3 *s){return checksumBytes(s,offsetof(SaveV3,checksum));}
static unsigned checksumV4(const SaveV4 *s){return checksumBytes(s,offsetof(SaveV4,checksum));}
static int readLegacyV4(const char *path,SaveV4 *out){
 FILE *f=fopen(path,"rb");if(!f)return 0;
 int ok=fread(out,sizeof(*out),1,f)==1;fclose(f);
 return ok&&out->magic==MAGIC&&out->version==4&&out->checksum==checksumV4(out)
        &&out->level>=1&&out->level<100000000;
}
static int readLegacyV3(const char *path,SaveV3 *out){
 FILE *f=fopen(path,"rb");if(!f)return 0;
 int ok=fread(out,sizeof(*out),1,f)==1;fclose(f);
 return ok&&out->magic==MAGIC&&out->version==3&&out->checksum==checksumV3(out)
        &&out->level>=1&&out->level<100000000;
}
static unsigned legacyChecksum(const LegacySave *s){return checksumBytes(s,offsetof(LegacySave,checksum));}
static int readSave(const char *path,Save *out){
 FILE *f=fopen(path,"rb");if(!f)return 0;
 int ok=fread(out,sizeof(*out),1,f)==1;fclose(f);
 return ok&&out->magic==MAGIC&&out->version==SAVE_VERSION&&out->checksum==checksum(out)
        &&out->level>=1&&out->level<100000000;
}
static void saveGame(void){
 saveData.lastSaveEpoch=(int64_t)time(NULL);
 saveData.checksum=checksum(&saveData);
 // Homebrew Launcher initializes and mounts SDMC through libctru appInit.
 // Keep the v2 save.bin untouched as a migration backup.
 mkdir("sdmc:/3ds",0777);
 mkdir("sdmc:/3ds/IdleBreakout3DS",0777);
 FILE *f=fopen(GAME_SAVE ".tmp","wb");
 if(!f){saveState=-1;saveStateFrame=frame;return;}
 int ok=fwrite(&saveData,sizeof(saveData),1,f)==1;
 if(fflush(f)!=0)ok=0;
 if(fclose(f)!=0)ok=0;
 if(!ok){remove(GAME_SAVE ".tmp");saveState=-1;saveStateFrame=frame;return;}
 Save verify;
 if(!readSave(GAME_SAVE ".tmp",&verify)){remove(GAME_SAVE ".tmp");saveState=-1;saveStateFrame=frame;return;}
 // Back up a valid last save; if replacement fails, retain the backup.
 Save previous;
 if(readSave(GAME_SAVE,&previous)){
  remove(GAME_SAVE_BAK);
  if(rename(GAME_SAVE,GAME_SAVE_BAK)!=0){
   remove(GAME_SAVE ".tmp");saveState=-1;saveStateFrame=frame;return;
  }
 }else remove(GAME_SAVE);
 if(rename(GAME_SAVE ".tmp",GAME_SAVE)!=0){
  rename(GAME_SAVE_BAK,GAME_SAVE);
  saveState=-1;saveStateFrame=frame;return;
 }
 unsaved=0;saveState=1;saveStateFrame=frame;refreshBottom=1;
}
static void loadGame(void){
 memset(&saveData,0,sizeof(saveData));
 if(!readSave(GAME_SAVE,&saveData)){
  if(!readSave(GAME_SAVE_BAK,&saveData)){
   SaveV4 prior;
   if(readLegacyV4(GAME_SAVE_V4,&prior)){
    memcpy(&saveData,&prior,offsetof(SaveV4,checksum));
    saveData.version=SAVE_VERSION;
    saveData.effectsEnabled=1;
    // All prior permanent prestige upgrades are preserved.
    unsaved=1;saveState=2;
   }else{
   SaveV3 prev;
   if(readLegacyV3(GAME_SAVE_V3,&prev)){
    // V3 -> V4: carry over money, balls, prestige, levels and sound settings.
    // The old random-spinner state is not a real original Power Up inventory.
    memcpy(&saveData,&prev,offsetof(SaveV3,checksum));
    saveData.version=SAVE_VERSION;
    saveData.pendingReward=0;saveData.activePowerup=0;
    saveData.powerupFrames=0;saveData.perksClaimed=0;
    saveData.claimedMilestone=0;saveData.bricksBroken=0;
    saveData.powerupsUsedMask=0;
    unsaved=1;saveState=2;
   }else{
   LegacySave old;
   FILE *f=fopen(GAME_SAVE_OLD,"rb");
   int ok=0;
   if(f){ok=fread(&old,sizeof(old),1,f)==1;fclose(f);}
   if(ok&&old.magic==MAGIC&&old.version==2&&old.checksum==legacyChecksum(&old)){
    saveData.magic=MAGIC;saveData.version=SAVE_VERSION;
    saveData.level=old.level;saveData.prestige=old.prestige;
    saveData.money=old.money;saveData.lifetime=old.lifetime;
    memcpy(saveData.owned,old.owned,sizeof(old.owned));
    memcpy(saveData.power,old.power,sizeof(old.power));
    memcpy(saveData.speed,old.speed,sizeof(old.speed));
    saveData.soundEnabled=old.soundEnabled;
    saveData.bestLevel=old.level;
    saveData.claimedMilestone=0;
    unsaved=1;saveState=2;
   }
   }
   }
  }
 }
 if(saveData.magic!=MAGIC){
  memset(&saveData,0,sizeof(saveData));
  saveData.magic=MAGIC;saveData.version=SAVE_VERSION;
  saveData.level=1;saveData.bestLevel=1;saveData.soundEnabled=1;saveData.effectsEnabled=1;
  unsaved=1;saveState=2;
 }else if(saveState!=2)saveState=3;
 for(int i=0;i<BTYPES;i++){
  if(saveData.power[i]>500)saveData.power[i]=500;
  if(saveData.speed[i]>100)saveData.speed[i]=100;
  if(saveData.owned[i]>MAX_BALLS)saveData.owned[i]=MAX_BALLS;
 }
 for(int i=0;i<4;i++)if(saveData.prestigeUp[i]>100)saveData.prestigeUp[i]=100;
 if(saveData.prestigeUp[3]>4)saveData.prestigeUp[3]=4;
 if(saveData.money<0||!isfinite(saveData.money))saveData.money=0;
 if(saveData.lifetime<0||!isfinite(saveData.lifetime))saveData.lifetime=0;
 if(saveData.level<1)saveData.level=1;
 if(saveData.bestLevel<saveData.level)saveData.bestLevel=saveData.level;
 if(saveData.activePowerup>5)saveData.activePowerup=0;
 if(saveData.powerupFrames>1800)saveData.powerupFrames=1800;
 if(saveData.powerupsUsedMask>31)saveData.powerupsUsedMask&=31;
 if(saveData.pendingReward>5)saveData.pendingReward=5;
 if(saveData.bricksBroken>100000000)saveData.bricksBroken=100000000;
 if(saveData.prestigePoints>100000000)saveData.prestigePoints=100000000;
 if(saveData.capUpgrade>7)saveData.capUpgrade=7;
 if(saveData.laserPower>100)saveData.laserPower=100;
 if(saveData.bgColor>4)saveData.bgColor=0;
 saveData.effectsEnabled=!!saveData.effectsEnabled;
 if(saveData.bossActive>1)saveData.bossActive=0;
 if(saveData.bossWins>50)saveData.bossWins=50;
 if(saveData.skillPoints>100000)saveData.skillPoints=100000;
 if(saveData.pendingGold>100000000)saveData.pendingGold=100000000;
 int64_t now=(int64_t)time(NULL);
 if(saveData.lastSaveEpoch>0&&now>saveData.lastSaveEpoch){
  int64_t seconds=now-saveData.lastSaveEpoch;
  if(seconds>43200)seconds=43200; // cap catch-up at 12 hours
  double power=0;
  const double offlineBase[6]={1,3,5,10,50,5};
  for(int t=0;t<6;t++)power+=(double)saveData.owned[t]*offlineBase[t]*(1.+saveData.power[t]*0.72);
  offlineEarnings=power*(double)seconds*0.025;
  if(offlineEarnings>1e14)offlineEarnings=1e14;
  if(offlineEarnings>0){saveData.money+=offlineEarnings;saveData.lifetime+=offlineEarnings;unsaved=1;}
 }

}
static void restoreBalls(void);
static void resetBricks(void);
static int saveNameAllowed(const char *name){
 size_t n=strlen(name);
 if(n<5||n>60||strcmp(name+n-4,".bin")!=0)return 0;
 for(size_t i=0;i<n;i++){
  char ch=name[i];
  if(!((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||
       (ch>='0'&&ch<='9')||ch=='_'||ch=='-'||ch=='.'))return 0;
 }
 return 1;
}
static int filenameCmp(const void *aa,const void *bb){
 return -strcmp((const char*)aa,(const char*)bb); // newest timestamped export first
}
static void scanSaveFolder(void){
 saveListCount=0;saveSelected=-1;importConfirm=0;
 mkdir("sdmc:/3ds",0777);
 mkdir("sdmc:/3ds/IdleBreakout3DS",0777);
 mkdir(SAVES_DIR,0777);
 DIR *d=opendir(SAVES_DIR);
 if(!d){settingsStatus=-1;return;}
 struct dirent *ent;
 while((ent=readdir(d))!=NULL&&saveListCount<SAVE_LIST_CAP){
  if(!saveNameAllowed(ent->d_name))continue;
  char path[160];snprintf(path,sizeof(path),"%s/%s",SAVES_DIR,ent->d_name);
  struct stat st;
  if(stat(path,&st)!=0||!S_ISREG(st.st_mode)||st.st_size!=(off_t)sizeof(Save))continue;
  size_t nameLen = strlen(ent->d_name);
  if (nameLen >= sizeof(saveList[saveListCount])) continue;
  memcpy(saveList[saveListCount], ent->d_name, nameLen + 1);
  saveListCount++;
 }
 closedir(d);
 qsort(saveList,saveListCount,sizeof(saveList[0]),filenameCmp);
 if(saveListPage*SAVE_PAGE_ROWS>=saveListCount)saveListPage=0;
 refreshBottom=1;
}
static void exportGame(void){
 // Save the normal live slot first; export is always a verified portable copy.
 if(unsaved)saveGame();
 if(saveState==-1){settingsStatus=-1;refreshBottom=1;return;}
 mkdir("sdmc:/3ds/IdleBreakout3DS",0777);
 mkdir(SAVES_DIR,0777);
 time_t now=time(NULL);
 struct tm *local=localtime(&now);
 char stamp[24];
 if(local&&now>1000000000)
  strftime(stamp,sizeof(stamp),"%Y%m%d_%H%M%S",local);
 else snprintf(stamp,sizeof(stamp),"RTC_%08lu",(unsigned long)frame);
 char path[160];int found=0;
 for(unsigned suffix=0;suffix<1000;suffix++){
  if(suffix==0)snprintf(lastExportName,sizeof(lastExportName),"IdleBreakout_%s.bin",stamp);
  else snprintf(lastExportName,sizeof(lastExportName),"IdleBreakout_%s_%03u.bin",stamp,suffix);
  snprintf(path,sizeof(path),"%s/%s",SAVES_DIR,lastExportName);
  struct stat st;if(stat(path,&st)!=0){found=1;break;}
 }
 if(!found){settingsStatus=-1;return;}
 saveData.checksum=checksum(&saveData);
 FILE *f=fopen(path,"wb");
 if(!f){settingsStatus=-1;return;}
 int ok=fwrite(&saveData,sizeof(saveData),1,f)==1;
 if(fflush(f)!=0)ok=0;
 if(fclose(f)!=0)ok=0;
 Save verification;
 if(!ok||!readSave(path,&verification)){
  remove(path);settingsStatus=-1;
 }else settingsStatus=1;
 if(saveBrowserOpen)scanSaveFolder();
 refreshBottom=1;
}
// Factory reset the LIVE game state only, preserving user-created exported backups.
// Reset writes a verified fresh v5 save before pruning old auto-migration save slots.
// The second write makes the automatic .bak a *reset* state, not old progress.
static void factoryResetGame(void){
 Save previous=saveData;
 int priorPerfMode=perfMode;
 int priorUnsaved=unsaved;
 int priorBossLevel=bossLevel;
 unsigned priorBossIndex=bossLevelIndex;
 memset(&saveData,0,sizeof(saveData));
 saveData.magic=MAGIC;
 saveData.version=SAVE_VERSION;
 saveData.level=1;
 saveData.bestLevel=1;
 saveData.soundEnabled=1;
 saveData.effectsEnabled=1;
 saveData.bgColor=0;
 saveData.lastSaveEpoch=0;
 perfMode=1;
 // Keep the developer's infinite-money test switch at its default ON state.
 // It does not change the fresh save file or grant persistent progress.
 testMode=1;
 bossLevel=0;
 bossLevelIndex=1;
 offlineEarnings=0;
 chosenX=5;chosenY=3;
 confirmRebirth=0;
 saveBrowserOpen=0;importConfirm=0;
 saveListCount=0;saveSelected=-1;saveListPage=0;
 unsaved=1;
 saveGame();
 if(saveState!=1){
  // Do not claim a reset if we cannot persist it on the SD card.
  saveData=previous;perfMode=priorPerfMode;unsaved=priorUnsaved;
  bossLevel=priorBossLevel;bossLevelIndex=priorBossIndex;
  restoreBalls();resetBricks();
  settingsStatus=-3;factoryResetConfirm=0;refreshBottom=1;
  return;
 }
 // Save again so a later recovery from save_v5.bak cannot resurrect old progress.
 saveGame();
 Save checked;
 if(!readSave(GAME_SAVE,&checked)){
  saveData=previous;perfMode=priorPerfMode;unsaved=1;
  bossLevel=priorBossLevel;bossLevelIndex=priorBossIndex;
  restoreBalls();resetBricks();
  settingsStatus=-3;factoryResetConfirm=0;refreshBottom=1;
  return;
 }
 // All historical *live* save slots must be removed, otherwise legacy migration
 // could re-import old progress if the v5 file is ever missing/corrupted.
 // Exported .bin files inside SAVES_DIR are intentionally untouched.
 remove(GAME_SAVE_OLD);
 remove(GAME_SAVE_V3);
 remove(GAME_SAVE_V4);
 remove(GAME_SAVE ".tmp");
 // Now update runtime physics and the presentation for the fresh save.
 saveData=checked;
 restoreBalls();resetBricks();
 for(int i=0;i<4;i++){
  vaus[i].pos=(i==0?85.0f:i==1?170.0f:i==2?135.0f:260.0f);
  vaus[i].dir=(i<2?1.0f:-1.0f);
  vaus[i].pulse=0;
 }
 laserFrames=0;scatterShots=0;
 rewardNoticeUntil=0;rewardNoticeType=0;
 factoryResetConfirm=0;
 settingsStatus=3;saveState=1;saveStateFrame=frame;
 refreshBottom=1;bottomValid=0;unsaved=0;
}

static void importGameFile(const char *filename){
 if(!saveNameAllowed(filename)){settingsStatus=-2;return;}
 char path[160];snprintf(path,sizeof(path),"%s/%s",SAVES_DIR,filename);
 Save imported;
 // Verify signature, version, checksum and file structure before touching live progress.
 if(!readSave(path,&imported)){settingsStatus=-2;refreshBottom=1;return;}
 // Restore an existing live save to .bak through saveGame's atomic replacement.
 // No exported files are ever modified during import.
 saveData=imported;
 for(int i=0;i<BTYPES;i++){
  if(saveData.power[i]>500)saveData.power[i]=500;
  if(saveData.speed[i]>100)saveData.speed[i]=100;
  if(saveData.owned[i]>MAX_BALLS)saveData.owned[i]=MAX_BALLS;
 }
 for(int i=0;i<4;i++)if(saveData.prestigeUp[i]>100)saveData.prestigeUp[i]=100;
 if(saveData.prestigeUp[3]>4)saveData.prestigeUp[3]=4;
 if(saveData.capUpgrade>7)saveData.capUpgrade=7;
 if(saveData.laserPower>100)saveData.laserPower=100;
 if(saveData.bgColor>4)saveData.bgColor=0;
 if(saveData.bossActive>1)saveData.bossActive=0;
 if(saveData.bossWins>50)saveData.bossWins=50;
 if(!isfinite(saveData.money)||saveData.money<0)saveData.money=0;
 if(!isfinite(saveData.lifetime)||saveData.lifetime<0)saveData.lifetime=0;
 if(saveData.level<1)saveData.level=1;
 if(saveData.bestLevel<saveData.level)saveData.bestLevel=saveData.level;
 if(saveData.pendingReward>5)saveData.pendingReward=5;
 saveData.powerupsUsedMask&=31;
 if(saveData.activePowerup>5)saveData.activePowerup=0;
 if(saveData.powerupFrames>1800)saveData.powerupFrames=1800;
 offlineEarnings=0;
 restoreBalls();resetBricks();
 unsaved=1;saveGame();
 settingsStatus=saveState==1?2:-1;
 saveBrowserOpen=0;importConfirm=0;
 refreshBottom=1;
}
static void audioInit(void){
 if(R_FAILED(ndspInit())){audioReady=0;return;}
 audioReady=1;ndspSetOutputMode(NDSP_OUTPUT_STEREO);
 for(int i=0;i<12;i++){
  unsigned size=samples[i].samples*sizeof(int16_t);
  audioMem[i]=linearAlloc((size+0x7f)&~0x7f);
  if(!audioMem[i])continue;
  memcpy(audioMem[i],samples[i].p,size);
  DSP_FlushDataCache(audioMem[i],size);
 }
 for(int ch=0;ch<5;ch++){
  ndspChnReset(ch);ndspChnSetInterp(ch,NDSP_INTERP_LINEAR);
  ndspChnSetRate(ch,16000.0f);ndspChnSetFormat(ch,NDSP_FORMAT_MONO_PCM16);
  float mix[12]={0};mix[0]=0.55f;mix[1]=0.55f;
  ndspChnSetMix(ch,mix);
 }
}
static void audioExit(void){
 if(!audioReady)return;
 ndspExit();
 for(int i=0;i<12;i++)if(audioMem[i])linearFree(audioMem[i]);
}
static void sound(int id){
 if(!audioReady||!saveData.soundEnabled||id<0||id>=12||!audioMem[id])return;
 // Throttle repeated hits from dozens of balls but keep each ball type audible.
 // The scatter ball has a dedicated original effect (sound_scatter.ogg).
 unsigned cooldown=(id==10||id==11)?9:((id==7||id==9)?2:5);
 if(lastSoundFrame[id]&&frame-lastSoundFrame[id]<cooldown)return;
 lastSoundFrame[id]=(unsigned)frame+1;
 unsigned ch=audioCursor++%5;
 ndspChnWaveBufClear(ch);
 memset(&wav[ch],0,sizeof(ndspWaveBuf));
 wav[ch].data_vaddr=audioMem[id];
 wav[ch].nsamples=samples[id].samples;
 wav[ch].looping=false;
 ndspChnWaveBufAdd(ch,&wav[ch]);
}
static double price(int type,int kind){
 double base=(kind==0?ballBase[type]:(kind==1?powerBase[type]:speedBase[type]));
 double level=(kind==0?saveData.owned[type]:(kind==1?saveData.power[type]:saveData.speed[type]));
 double factor=(kind==0?1.34:(kind==1?1.65:1.52));
 double price=base*pow(factor,mind(level,400.0));
 if(!isfinite(price)||price>1.e30)return 1.e30;
 return price;
}
static int ballCapacity(void){return 50+10*(int)saveData.capUpgrade;}
static int countBalls(void){int n=0;for(int t=0;t<BTYPES;t++)n+=saveData.owned[t];return n;}
static void spawn(int type,int sub){
 for(int j=0;j<MAX_BALLS;j++)if(!balls[j].active){
  float angle=0.8f+randf()*4.7f;
  float speed=baseVel[type];
  balls[j]=(Ball){200.0f,195.0f,cosf(angle)*speed,sinf(angle)*speed,1.0f,type,1,sub,sub?190:0,0};
  activeBalls++;return;
 }
}
// Scatter-shot sub-balls: visible, independent moving projectiles from the parent.
static void scatterBurst(float x,float y,float vx,float vy){
 int extra=3+(int)(saveData.speed[3]>5?5:saveData.speed[3]);
 if(activeBalls>=MAX_BALLS-extra)return;
 float angle=atan2f(vy,vx);
 for(int n=0;n<extra;n++){
  int slot=-1;
  for(int j=0;j<MAX_BALLS;j++)if(!balls[j].active){slot=j;break;}
  if(slot<0)break;
  float a=angle+((float)n-(extra-1)*0.5f)*0.36f+randf()*0.10f;
  float speed=2.6f;
  balls[slot]=(Ball){x,y,cosf(a)*speed,sinf(a)*speed,1.0f,3,1,1,85,5};
  activeBalls++;scatterShots++;
 }
}
static void restoreBalls(void){
 memset(balls,0,sizeof(balls));activeBalls=0;
 for(int t=0;t<BTYPES;t++)for(unsigned n=0;n<saveData.owned[t]&&activeBalls<MAX_BALLS;n++)spawn(t,0);
}
static void resetBricks(void){
 bossLevel=(int)saveData.bossActive;
 if(bossLevel)bossLevelIndex=(unsigned)bossIndex()+1u;
 int blackBudget=(saveData.level>100&&!bossLevel)?(int)fminf(100.0f,ceilf((saveData.level%1000)/10.0f)):0;
 int pattern=(int)(saveData.level%5);
 for(int row=0;row<ROWS;row++)for(int col=0;col<COLS;col++){
  int present=0;
  if(bossLevel){present=(row==10&&col==7);}
  else if(saveData.level%9==8)present=(specialRows[row][col]=='1');
  else present=(levelRows[pattern][row]=='1'&&levelColumns[pattern][col]=='1');
  double hp=(double)saveData.level;
  if(saveData.level>1000)hp*=ceil(saveData.level/100.0)*floor(saveData.level/1000.0);
  if(bossLevel)hp=bossHp[bossIndex()];
  int black=present&&!bossLevel&&blackBudget>0;
  if(black)blackBudget--;
  int gold=present&&!bossLevel&&saveData.level%20==0&&row==9&&col==7;
  if(gold)black=0;
  bricks[row][col]=(Brick){present?hp:0,present?hp:0,0,black,(unsigned char)((saveData.level-1)%9),gold};
 }
 if(!bossLevel&&saveData.level%20==0){
  bricks[9][7]=(Brick){(float)saveData.level*50.0f,(float)saveData.level*50.0f,0,0,0,1};
 }
 // Boss levels contain one larger boss tile with the original boss artwork.
 if(chosenX>=COLS)chosenX=COLS-1;
 if(chosenY>=ROWS)chosenY=ROWS-1;
}
static void stepLevel(void){
 saveData.level++;
 double bonus=10.0*saveData.level*(1+saveData.prestige);
 if(saveData.prestigeUp[0]>0)bonus*=0.5*(1u<<saveData.prestigeUp[0]);
 if(saveData.activePowerup==1)bonus*=2;
 saveData.money+=bonus;
 if(saveData.level>saveData.bestLevel)saveData.bestLevel=saveData.level;
 resetBricks();refreshBottom=1;unsaved=1;sound(9);
}
static void giveMoney(float actual,int t){
 if(actual<=0)return;
 double amt=actual*(1+saveData.prestige)*((t==7)?10:1);
 // Level-complete cash prestige only affects level bonuses, not brick hits.
 if(saveData.activePowerup==1)amt*=2;
 saveData.money+=amt;saveData.lifetime+=amt;unsaved=1;
}
static void hurt(int row,int col,float damage,int type,int splash){
 if(row<0||row>=ROWS||col<0||col>=COLS||bricks[row][col].hp<=0)return;
 Brick *b=&bricks[row][col];
 if(bossLevel){
  const BossSpec *spec=currentBoss();
  if(type>=0&&type<6){
   if(spec->doubleMask&(1u<<type))damage*=2.0f;
   if(spec->halfMask&(1u<<type))damage*=0.5f;
  }
  if(spec->shield&&((frame/360)%2)==0)damage*=0.5f;
 }else{
  if(b->poison)damage*=2;
  if(type==5)b->poison=1;
 }
 if(b->black && type==0 && !(saveData.skillMask&1u))return;
 float actual=(float)mind(b->hp,(double)damage);b->hp-=actual;
 if(b->hp<=0&&actual>0){
  if(b->black&&saveData.blackBricks<100000000u)saveData.blackBricks++;
  if(b->gold&&saveData.pendingGold<100000000u-2u)saveData.pendingGold+=2;
  if(saveData.bossActive){
   saveData.bossActive=0;
   // Leave bossLevel flagged until checkCleared() restores the normal map.
   // Clearing it here would incorrectly award a normal level advancement.
   bossTimer=0;bossFreezeTimer=0;bossFrozenType=-1;
   if(saveData.bossWins<50)saveData.bossWins++;
   saveData.skillPoints+=1;
   saveData.prestigePoints+=2;
   rewardNoticeUntil=frame+240;rewardNoticeType=3;
   refreshBottom=1;unsaved=1;
  }
  // Source game awards one Power Up for each 600 bricks, at most 5 (3,000 bricks).
  if(!bossLevel&&saveData.bricksBroken<100000000)saveData.bricksBroken++;
  if(!bossLevel&&saveData.bricksBroken>0&&saveData.bricksBroken<=3000&&saveData.bricksBroken%600==0&&saveData.pendingReward<5){
   saveData.pendingReward++;rewardNoticeUntil=frame+240;rewardNoticeType=1;
   refreshBottom=1;
  }
 }
 giveMoney(actual,type);
 if(!splash&&type==1){
  float area=maxf(1,damage*0.25f);
  for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++)if(dx||dy)hurt(row+dy,col+dx,area,type,1);
 }
 if(type==4&&!splash&&b->hp<=0)sound(8);
 // Scatter impact audio is triggered once after its projectiles launch.
}
static void checkCleared(void){
 for(int r=0;r<ROWS;r++)for(int c=0;c<COLS;c++)if(bricks[r][c].hp>0)return;
 if(bossLevel){resetBricks();refreshBottom=1;return;}
 stepLevel();
}
static void clickBrick(void){
 if(bricks[chosenY][chosenX].hp<=0)return;
 float clickPower=(float)(1+saveData.prestige);
 if(saveData.activePowerup==2)clickPower*=10;
 hurt(chosenY,chosenX,clickPower,0,1);
 if(saveData.activePowerup==2)scatterBurst(FIRST_BRICK_X+chosenX*STEP_X+12,
    FIRST_BRICK_Y+chosenY*STEP_Y+4,2.0f,-2.0f);
 sound(7);checkCleared();
}
static void buy(int type,int kind){
 if(type<0||type>=BTYPES)return;
 double cost=price(type,kind);
 if(!testMode&&saveData.money+1e-7<cost)return;
 if(kind==0&&(countBalls()>=ballCapacity()||countBalls()>=MAX_BALLS))return;
 if(kind==0&&type>=6)return; // Snow is a power-up; Money Ball was removed in the original.
 if(kind==0&&type>0&&saveData.owned[type-1]==0)return; // sequential unlock
 if(kind==1&&saveData.power[type]>=500)return;
 if(kind==2&&saveData.speed[type]>=100)return;
 if(!testMode)saveData.money-=cost;
 if(kind==0){saveData.owned[type]++;spawn(type,0);}
 else if(kind==1)saveData.power[type]++;
 else saveData.speed[type]++;
 refreshBottom=1;unsaved=1;sound(7);
}
static unsigned prestigePrice(int idx){
 if(idx<0||idx>=5)return 0;
 unsigned lvl=(idx==4?saveData.capUpgrade:saveData.prestigeUp[idx]);
 if(idx==3){static const unsigned goldCost[4]={10,15,23,34};return lvl<4?goldCost[lvl]:0;}
 if(idx==4){if(lvl>=7)return 0;double cost=4.0*pow(1.45,(double)lvl);return (unsigned)ceil(cost);}
 if(idx==1&&lvl>=9)return 0;
 if(idx==0&&lvl>=10)return 0;
 if(idx==2&&lvl>=18)return 0;
 if(lvl>=20)return 0;
 unsigned base=prestigeCostBase[idx];
 return base<<lvl;
}
static void purchasePrestige(int idx){
 if(idx<0||idx>=5)return;
 unsigned cost=prestigePrice(idx);if(!cost)return;
 if(!testMode&&saveData.prestigePoints<cost)return;
 if(!testMode)saveData.prestigePoints-=cost;
 if(idx==4)saveData.capUpgrade++;
 else saveData.prestigeUp[idx]++;
 refreshBottom=1;unsaved=1;sound(7);
}
static unsigned rebirthReward(void){
 if(testMode)return 2;
 return saveData.pendingGold;
}
static void rebirth(void){
 unsigned gold=rebirthReward();if(!gold)return;
 if(!testMode){
  if(saveData.prestigePoints<100000000u-gold)saveData.prestigePoints+=gold;
  else saveData.prestigePoints=100000000u;
 }
 if(saveData.prestige<100000000u)saveData.prestige++;
 // Correct reset: money, level, owned balls, temporary upgrades, active powerups.
 // Persistent gold, lasers, ball capacity, skills, boss progress and preferences survive.
 saveData.level=1;saveData.money=(double)saveData.prestige*50.0;
 saveData.pendingGold=0;saveData.bossActive=0;
 saveData.claimedMilestone=0;saveData.pendingReward=0;
 saveData.activePowerup=0;saveData.powerupFrames=0;
 saveData.bricksBroken=0;saveData.powerupsUsedMask=0;saveData.perksClaimed=0;
 memset(saveData.owned,0,sizeof(saveData.owned));
 memset(saveData.speed,0,sizeof(saveData.speed));
 memset(saveData.power,0,sizeof(saveData.power));
 confirmRebirth=0;
 restoreBalls();resetBricks();refreshBottom=1;unsaved=1;sound(9);saveGame();
}
static void usePowerup(int index){
 // Five source-game Power Ups: one of each per run, earned by breaking bricks.
 if(index<0||index>=5||saveData.pendingReward==0)return;
 if((saveData.powerupsUsedMask>>index)&1u)return;
 saveData.powerupsUsedMask|=1u<<index;
 saveData.pendingReward--;
 saveData.activePowerup=(unsigned)index+1u;
 saveData.powerupFrames=1800; // 30 seconds, as in original
 saveData.perksClaimed++;
 rewardNoticeUntil=frame+160;rewardNoticeType=2;
 refreshBottom=1;unsaved=1;sound(7);
 if(index==3){ // Snow Ball spawns for the duration, without adding to owned quantity.
  int slot=-1;
  for(int b=0;b<MAX_BALLS;b++)if(!balls[b].active){slot=b;break;}
  if(slot>=0){spawn(6,0);balls[slot].ttl=255;} // temporary, cleared after effect
 }
}
static void demonCorePulse(void){
 if(saveData.activePowerup!=3||frame%32!=0)return;
 // Lightweight damage waves. Exact GameMaker particle shapes not yet copied.
 int shots=0;
 for(int attempt=0;attempt<35&&shots<6;attempt++){
  int r=(int)(rand32()%ROWS),c=(int)(rand32()%COLS);
  if(bricks[r][c].hp<=0)continue;
  hurt(r,c,(float)(saveData.level*2+saveData.prestige+1),0,1);
  shots++;
 }
 if(shots)sound(8);
}
static void fireLasers(void){
 int num=(int)saveData.prestigeUp[3];
 if(num>4)num=4;
 if(num<=0)return;
 int pulse=0;
 for(int k=0;k<num;k++){
  Vaus *v=&vaus[k];
  float speed=(k&1)?0.52f:0.38f;
  v->pos+=v->dir*speed;
  float low=(k&1)?20.0f:50.0f;
  float high=(k&1)?380.0f:211.0f;
  if(v->pos<low){v->pos=low;v->dir=1;}
  if(v->pos>high){v->pos=high;v->dir=-1;}
  if(frame%46!=k*8)continue;
  v->pulse=12;pulse=1;
  float dmg=100.0f*(1.0f+saveData.laserPower*0.25f);
  dmg*=powf(2.0f,(float)saveData.prestigeUp[2]);
  if(k%2==0){
   for(int r=0;r<ROWS;r++)for(int c=0;c<COLS;c++){
    if(bricks[r][c].hp<=0)continue;
    int y=FIRST_BRICK_Y+r*STEP_Y+BH/2;
    if(fabsf(y-v->pos)<(float)(BH+4)){
     if(bossLevel&&currentBoss()->laserImmune&&!(saveData.skillMask&2u))continue;
     hurt(r,c,dmg,0,1);
    }
   }
  }else{
   for(int r=0;r<ROWS;r++)for(int c=0;c<COLS;c++){
    if(bricks[r][c].hp<=0)continue;
    int x=FIRST_BRICK_X+c*STEP_X+BW/2;
    if(fabsf(x-v->pos)<(float)(BW/2+4)){
     if(bossLevel&&currentBoss()->laserImmune&&!(saveData.skillMask&2u))continue;
     hurt(r,c,dmg,0,1);
    }
   }
  }
 }
 if(pulse)sound(11);
 for(int k=0;k<num;k++)if(vaus[k].pulse>0)vaus[k].pulse--;
}
static void drawLasers(Screen *s){
 int num=(int)saveData.prestigeUp[3];if(num>4)num=4;
 for(int k=0;k<num;k++){
  Vaus *v=&vaus[k];
  int blue=v->pulse>0?255:207;
  if((k&1)==0){
   int y=(int)v->pos;
   box(s,13,y-2,374,5,8,45,blue);
   box(s,13,y,374,1,145,232,255);
   // left and right orange original-style VAUS mounts
   box(s,3,y-6,14,13,246,101,27);outline(s,3,y-6,14,13,35,28,23);
   box(s,383,y-6,14,13,246,101,27);outline(s,383,y-6,14,13,35,28,23);
  }else{
   int x=(int)v->pos;
   box(s,x-2,47,5,168,8,45,blue);
   box(s,x,47,1,168,145,232,255);
   box(s,x-9,43,19,10,246,101,27);outline(s,x-9,43,19,10,35,28,23);
   box(s,x-9,210,19,10,246,101,27);outline(s,x-9,210,19,10,35,28,23);
  }
 }
}
static void bounce(Ball *b){
 if(b->type==6)b->boost=minf(3.5f,b->boost+0.04f);
 if(b->type==2){
  float nearest=1e30f,tx=200,ty=80;
  for(int r=0;r<ROWS;r++)for(int c=0;c<COLS;c++)if(bricks[r][c].hp>0){
   int x=FIRST_BRICK_X+c*STEP_X+BW/2,y=FIRST_BRICK_Y+r*STEP_Y+BH/2;
   float d=(x-b->x)*(x-b->x)+(y-b->y)*(y-b->y);
   if(d<nearest){nearest=d;tx=x;ty=y;}
  }
  if(nearest<1e29f){float dx=tx-b->x,dy=ty-b->y,dist=sqrtf(dx*dx+dy*dy);
   if(dist>1){float speed=sqrtf(b->vx*b->vx+b->vy*b->vy);b->vx=dx/dist*speed;b->vy=dy/dist*speed;}
  }
 }
}
static void updateBalls(void){
 const float prestigePower=ldexpf(1.0f,(int)saveData.prestigeUp[2]);
 for(int i=0;i<MAX_BALLS;i++){
  Ball *b=&balls[i];if(!b->active)continue;
  if(bossLevel&&bossFreezeTimer>0&&b->type==bossFrozenType)continue;
  if(b->hitCooldown>0)b->hitCooldown--;
  if(b->sub){if(--b->ttl==0){b->active=0;activeBalls--;continue;}}
  int t=b->type;
  float speedMultiplier=(1+minf(saveData.speed[t],90)*0.085f)*b->boost;
  speedMultiplier*=minf(4.0f,1.0f+saveData.prestigeUp[1]*0.4f);
  float vx=b->vx*speedMultiplier,vy=b->vy*speedMultiplier;
  float vmax=maxf(fabsf(vx),fabsf(vy)),steps=ceilf(vmax/5.0f);if(steps<1)steps=1;
  if(steps>4)steps=4;
  for(int substep=0;substep<(int)steps;substep++){
   float oldX=b->x,oldY=b->y;
   b->x+=vx/steps;b->y+=vy/steps;
   int reflected=0;
   // Balls remain inside the art-correct arena, never inside the upper HUD.
   if(b->x<10){b->x=10;b->vx=fabsf(b->vx);reflected=1;}
   if(b->x>TOP_W-11){b->x=TOP_W-11;b->vx=-fabsf(b->vx);reflected=1;}
   if(b->y<52){b->y=52;b->vy=fabsf(b->vy);reflected=1;}
   if(b->y>HEIGHT-27){b->y=HEIGHT-27;b->vy=-fabsf(b->vy);reflected=1;}
   if(reflected)bounce(b);
   if(b->hitCooldown)continue;
   int c=((int)b->x-FIRST_BRICK_X)/STEP_X;
   int r=((int)b->y-FIRST_BRICK_Y)/STEP_Y;
   if(bossLevel){
    int bc=7,br=10;
    int bossX=FIRST_BRICK_X+bc*STEP_X-34,bossY=FIRST_BRICK_Y+br*STEP_Y-18;
    if(b->x>=bossX-4&&b->x<=bossX+93&&b->y>=bossY-3&&b->y<=bossY+46){c=bc;r=br;}
    else continue;
   }else if(b->x<FIRST_BRICK_X||b->y<FIRST_BRICK_Y||c<0||c>=COLS||r<0||r>=ROWS)continue;
   int bx=FIRST_BRICK_X+c*STEP_X,by=FIRST_BRICK_Y+r*STEP_Y;
   if(!bossLevel&&(b->x<bx-4||b->x>bx+BW+4||b->y<by-4||b->y>by+BH+4))continue;
   if(bricks[r][c].hp<=0)continue;
   float damage=baseDamage[t]*(1.0f+saveData.power[t]*0.72f)*b->boost;
   damage*=prestigePower;
   if(saveData.activePowerup==4&&t==6)damage*=2.0f;
   if(b->sub)damage*=0.5f;
   hurt(r,c,damage,t,0);
   // Match ball categories to the original sound samples, not their purchase index.
   if(!b->sub || (frame%15)==0)sound(ballSound[t]);
   if(t==3&&!b->sub)scatterBurst(b->x,b->y,b->vx,b->vy);
   if(b->sub){b->active=0;activeBalls--;break;}
   if(t!=4){
    if(bossLevel||oldY<by-2||oldY>by+BH+2)b->vy=-b->vy;
    else b->vx=-b->vx;
    b->x=oldX;b->y=oldY;
    bounce(b);
   }
   b->hitCooldown=5;
  }
 }
 if((frame%7)==0)checkCleared();
}
// Original-art arena walls: distinct tiled white segments and square corners.
// Draw after bricks and before lasers, so the VAUS emitters are not hidden.
// The HUD ends at y=45; side walls terminate before the bottom help strip.
static void drawArenaFrame(Screen *s){
 // Top and bottom rails with black separators between white wall tiles.
 for(int x=8;x<392;x+=32){
  int span=(x+32>392)?392-x:32;
  blit(s,SPR_WALL_HORIZONTAL,x,43,span,7);
  blit(s,SPR_WALL_HORIZONTAL,x,215,span,7);
 }
 // Edge tiles from the original spr_wall sprite.
 for(int y=50;y<215;y+=32){
  int span=(y+32>215)?215-y:32;
  blit(s,SPR_WALL_VERTICAL,1,y,7,span);
  blit(s,SPR_WALL_VERTICAL,392,y,7,span);
 }
 // Four square corner caps, scaled from the original wall art.
 blit(s,SPR_WALL_CORNER,0,42,9,9);
 blit(s,SPR_WALL_CORNER,391,42,9,9);
 blit(s,SPR_WALL_CORNER,0,214,9,9);
 blit(s,SPR_WALL_CORNER,391,214,9,9);
 // Reinforce the continuous inward border with the game's dark outlines.
 box(s,8,50,1,165,30,30,30);box(s,391,50,1,165,30,30,30);
 box(s,9,50,382,1,30,30,30);box(s,9,214,382,1,30,30,30);
}
static void drawTop(Screen *s){
 if(saveData.bgColor==0)memcpy(s->fb,topBgCache,sizeof(topBgCache));
 else{
  static const unsigned char colors[5][3]={{248,240,213},{222,246,247},{248,247,242},{255,234,218},{185,198,207}};
  const unsigned char *co=colors[saveData.bgColor%5];box(s,0,0,TOP_W,HEIGHT,co[0],co[1],co[2]);
 }
 char tmp[40],msg[80],fullHp[32];
 // Display-only original-style ball purchase bar. Purchases happen on bottom touchscreen.
 for(int i=0;i<6;i++){
  int x=5+i*65;
  box(s,x,1,63,26,247,242,225);outline(s,x,1,63,26,22,22,22);
  blit(s,ballVisual[i],x+4,4,12,12);
  if(frame%18==0||!topPrices[i][0])numeric(price(i,0),topPrices[i],sizeof(topPrices[i]));
  snprintf(tmp,sizeof(tmp),"%.31s",topPrices[i]);
  writing(s,x+18,6,tmp,31,31,31,1);
  box(s,x+1,19,61,7,113,211,52);
 }
 box(s,4,28,392,18,248,240,213);outline(s,4,28,392,18,38,38,38);
 snprintf(msg,sizeof(msg),"LV %lu",(unsigned long)saveData.level);writing(s,10,32,msg,34,30,25,1);
 numeric(saveData.money,tmp,sizeof(tmp));snprintf(msg,sizeof(msg),testMode?"$INF TEST":"$%s",tmp);
 writing(s,107,32,msg,22,95,22,1);
 snprintf(msg,sizeof(msg),"%d/%d BALLS",countBalls(),ballCapacity());writing(s,218,32,msg,34,30,25,1);
 snprintf(msg,sizeof(msg),"BB:%lu",(unsigned long)saveData.blackBricks);writing(s,325,32,msg,34,30,25,1);
 float baseHp=(float)saveData.level;
 if(saveData.level>1000)baseHp*=ceilf(saveData.level/100.0f)*floorf(saveData.level/1000.0f);
 if(bossLevel)baseHp=(float)bossHp[bossIndex()];
 numeric((double)baseHp,fullHp,sizeof(fullHp));
 for(int r=0;r<ROWS;r++)for(int c=0;c<COLS;c++){
  Brick *b=&bricks[r][c];if(b->hp<=0)continue;
  int x=FIRST_BRICK_X+c*STEP_X,y=FIRST_BRICK_Y+r*STEP_Y;
  if(bossLevel){
   blit(s,SPR_BRICK_BOSS0,x-34,y-18,90,42);
   blit(s,bossPortrait(),x-29,y-15,35,35);
   numeric(ceil(b->hp),tmp,sizeof(tmp));writing(s,x-17,y-2,tmp,18,18,18,1);
  }else{
   int pal=(int)fmodf(ceilf(b->hp),9.0f);if(pal<0)pal+=9;
   if(b->gold){blit(s,SPR_BRICK0+4,x,y,BW,BH);blit(s,SPR_GOLD0,x+7,y-5,10,12);}
   else blit(s,b->black?SPR_BRICK_BLACK0:SPR_BRICK0+pal,x,y,BW,BH);
   if(b->poison)box(s,x+1,y+1,2,5,48,100,45);
   if(b->black){
    // Black bricks used to lose their HP label whenever it exceeded three characters.
    // Use a compact readable counter in white, including K/M/B abbreviations.
    numeric(ceil(b->hp),tmp,sizeof(tmp));
    size_t len=strlen(tmp);
    if(len>4){
     double hp=ceil(b->hp);
     if(hp>=1e12)snprintf(tmp,sizeof(tmp),"%.0fT",hp/1e12);
     else if(hp>=1e9)snprintf(tmp,sizeof(tmp),"%.0fB",hp/1e9);
     else if(hp>=1e6)snprintf(tmp,sizeof(tmp),"%.0fM",hp/1e6);
     else snprintf(tmp,sizeof(tmp),"%.0fK",hp/1e3);
     len=strlen(tmp);
    }
    if(len<=4)smallWriting(s,x+(BW-(int)len*5)/2,y+1,tmp,245,245,245);
   }else if(b->hp>1.4){
    const char *label=fullHp;
    if(b->hp!=b->maxhp){numeric(ceil(b->hp),tmp,sizeof(tmp));label=tmp;}
    int len=(int)strlen(label);
    if(len<=3)writing(s,x+BW/2-len*4,y-1,label,25,25,25,1);
   }
  }
 }
 drawArenaFrame(s);
 drawLasers(s);
 // Non-blocking original-style power-up notice, not a randomized spinner.
 if(offlineEarnings>0&&frame<360){
  char idleText[45];numeric(offlineEarnings,idleText,sizeof(idleText));
  box(s,64,178,275,20,195,231,169);outline(s,64,178,275,20,59,69,49);
  snprintf(msg,sizeof(msg),"WHILE AWAY +$%s",idleText);writing(s,78,182,msg,39,75,44,1);
 }
 if(frame<rewardNoticeUntil){
  box(s,62,130,276,43,243,229,182);outline(s,62,130,276,43,58,48,30);
  if(rewardNoticeType==3){
   writing(s,93,137,"BOSS BRICK DEFEATED",65,55,35,1);
   writing(s,94,152,"+1 SKILL POINT +2 GOLD",33,87,44,1);
  }else if(rewardNoticeType==2&&saveData.activePowerup>0){
   writing(s,91,137,"POWER UP ACTIVATED",65,55,35,1);
   writing(s,94,152,perkNames[saveData.activePowerup-1],33,87,44,1);
  }else{
   writing(s,92,137,"POWER UP EARNED!",65,55,35,1);
   writing(s,79,152,"L/R: POWER UPS   TAP TO USE",33,87,44,1);
  }
 }
 for(int i=0;i<MAX_BALLS;i++)if(balls[i].active){
  // Keep every ball visible; only optional effects are reduced in FAST mode.
  Ball *b=&balls[i];
  if(b->sub)blit(s,SPR_BALL8,(int)b->x-2,(int)b->y-2,5,5);
  else{
   // Small movement cues, with expensive effects disabled in FAST mode.
   // Plasma glow uses frames 0..6 of the ORIGINAL plasma atlas. Frame 7 is a
   // separate gray rectangle and must not be part of the animation cycle.
   // Always render a small animated Plasma halo, even with 40+ balls in FAST mode.
   // Limit its area in FAST mode to keep the Old 3DS framebuffer cost manageable.
   if(b->type==1 && saveData.effectsEnabled){
    int glowFrame=(frame/4+i*2)%7;
    int glowSize=perfMode?(14+(glowFrame%3)*2):(17+glowFrame*2);
    blit(s,SPR_PLASMA0+glowFrame,(int)b->x-glowSize/2,
         (int)b->y-glowSize/2,glowSize,glowSize);
   }
   // Scatter/poison/cannonball extra effects can still be suppressed when
   // many balls are active; their actual damage mechanics are unchanged.
   if(saveData.effectsEnabled&&(!perfMode||activeBalls<40)){
    if(b->type==4&&frame%3==0){
     box(s,(int)(b->x-b->vx*3)-1,(int)(b->y-b->vy*3)-1,3,3,116,116,116);
    }else if(b->type==5&&frame%5==0){
     outline(s,(int)b->x-7,(int)b->y-7,15,15,159,207,91);
    }
   }
   blit(s,ballVisual[(unsigned)b->type<BTYPES?(unsigned)b->type:0],(int)b->x-5,(int)b->y-5,11,11);
  }
 }
 // Selected brick cursor: 3DS has no top touchscreen; D-Pad moves it and A taps.
 outline(s,FIRST_BRICK_X+chosenX*STEP_X-1,FIRST_BRICK_Y+chosenY*STEP_Y-1,BW+2,BH+2,28,24,25);
 box(s,5,224,391,13,238,228,196);
 writing(s,12,225,"D-PAD TARGET  A:CLICK  L/R:SHOP+MAP",52,44,29,1);
}
static void buyButton(Screen *s,int x,int y,int width,int height,int affordable){
 blit(s,affordable?SPR_BUY0:SPR_BUY2,x,y,width,height);
 outline(s,x,y,width,height,35,35,35);
}
static void drawShop(Screen *s){
 char line[80],priceText[40];
 for(int i=0;i<3;i++){
  int type=page*3+i;int y=35+i*61;
  int unlocked=(type==0||saveData.owned[type-1]>0);
  box(s,5,y,310,59,248,239,206);outline(s,5,y,310,59,48,45,39);
  blit(s,ballVisual[type],12,y+5,17,17);
  writing(s,33,y+3,ballNames[type],31,32,31,1);
  snprintf(line,sizeof(line),"OWN:%lu",(unsigned long)saveData.owned[type]);
  writing(s,162,y+3,line,71,64,47,1);
  numeric(price(type,0),priceText,sizeof(priceText));
  buyButton(s,219,y+2,87,19,unlocked&&(testMode||saveData.money>=price(type,0))&&countBalls()<ballCapacity());
  snprintf(line,sizeof(line),unlocked?"BUY %s":"LOCKED",priceText);
  writing(s,225,y+6,line,21,25,20,1);
  writing(s,12,y+24,ballHelp[type],87,79,65,1);
  double pp=price(type,1),sp=price(type,2);
  numeric(pp,priceText,sizeof(priceText));snprintf(line,sizeof(line),"PWR %s",priceText);
  buyButton(s,11,y+40,90,17,testMode||saveData.money>=pp);
  writing(s,17,y+44,line,25,26,22,1);
  numeric(sp,priceText,sizeof(priceText));snprintf(line,sizeof(line),type==1?"RNG %s":(type==3?"EXTRA %s":"SPD %s"),priceText);
  buyButton(s,106,y+40,106,17,testMode||saveData.money>=sp);
  writing(s,112,y+44,line,25,26,22,1);
  box(s,217,y+40,88,17,236,209,196);outline(s,217,y+40,88,17,50,45,38);
  writing(s,229,y+44,"DELETE 1",64,38,34,1);
 }
}
static void deleteBall(int t){
 if(t<0||t>=6||saveData.owned[t]==0)return;
 saveData.owned[t]--;
 for(int j=0;j<MAX_BALLS;j++)if(balls[j].active&&!balls[j].sub&&balls[j].type==t){
  balls[j].active=0;activeBalls--;break;
 }
 refreshBottom=1;unsaved=1;sound(7);
}
static void drawRewards(Screen *s){
 char line[100];
 // Follows the original POWER UPS interface: one-time power-up choices,
 // earned at 600, 1200, 1800, 2400, 3000 broken bricks. No slot spinner.
 box(s,5,35,310,42,243,229,187);outline(s,5,35,310,42,55,49,32);
 snprintf(line,sizeof(line),"BRICKS %lu  POWER-UPS %lu",
   (unsigned long)saveData.bricksBroken,(unsigned long)saveData.pendingReward);
 writing(s,13,42,line,44,39,31,1);
 if(saveData.bricksBroken<3000){
  snprintf(line,sizeof(line),"NEXT POWER UP AT %lu BRICKS",
   (unsigned long)(((saveData.bricksBroken/600)+1)*600));
  writing(s,13,59,line,66,59,42,1);
 }else writing(s,13,59,"ALL FIVE CREDITS EARNED",66,59,42,1);
 writing(s,13,69,"ONE USE PER POWER UP",86,77,60,1);
 for(int i=0;i<5;i++){
  int y=81+i*27;
  int used=(saveData.powerupsUsedMask>>i)&1;
  int canUse=saveData.pendingReward>0&&!used;
  box(s,8,y,304,25,used?216:(canUse?250:236),used?208:(canUse?236:229),used?189:(canUse?195:213));
  outline(s,8,y,304,25,67,57,39);
  blit(s,SPR_POWER0+i,12,y+2,21,21);
  writing(s,39,y+2,perkNames[i],used?123:43,used?117:39,used?104:32,1);
  writing(s,39,y+14,perkHelp[i],used?145:79,used?135:69,used?121:51,1);
  writing(s,258,y+9,used?"USED":(canUse?"USE":"LOCK"),
    used?114:(canUse?27:116),used?108:(canUse?117:107),used?99:(canUse?40:94),1);
 }
}
static void drawPrestige(Screen *s){
 char line[88],n[28];
 box(s,5,35,310,31,244,225,179);outline(s,5,35,310,31,65,53,30);
 if(testMode)snprintf(line,sizeof(line),"REBIRTHS:%lu GOLD:INF",(unsigned long)saveData.prestige);
 else snprintf(line,sizeof(line),"REBIRTHS:%lu GOLD:%lu",(unsigned long)saveData.prestige,(unsigned long)saveData.prestigePoints);
 writing(s,12,39,line,46,44,34,1);
 snprintf(line,sizeof(line),"NEXT RESET: +%lu GOLD",(unsigned long)rebirthReward());
 writing(s,12,53,line,67,59,39,1);
 for(int i=0;i<5;i++){
  int y=64+i*23;
  unsigned lvl=(i==4?saveData.capUpgrade:saveData.prestigeUp[i]);
  unsigned cost=prestigePrice(i);
  box(s,7,y,306,21,253,242,216);outline(s,7,y,306,21,86,76,53);
  snprintf(line,sizeof(line),"%s LV.%u",prestigeNames[i],lvl);
  writing(s,12,y+5,line,51,47,37,1);
  if(!cost)snprintf(n,sizeof(n),"MAX");else snprintf(n,sizeof(n),"%u G",cost);
  box(s,244,y+1,64,19,(testMode||saveData.prestigePoints>=cost)&&cost?170:218,(testMode||saveData.prestigePoints>=cost)&&cost?229:200,150);
  outline(s,244,y+1,64,19,76,68,44);
  writing(s,249,y+5,n,40,38,29,1);
 }
 box(s,7,181,306,18,247,224,189);outline(s,7,181,306,18,86,76,53);
 snprintf(line,sizeof(line),"LASER POWER LV.%lu",(unsigned long)saveData.laserPower);
 writing(s,12,185,line,51,47,37,1);
 unsigned cost=10u+(unsigned)saveData.laserPower*5u;
 snprintf(n,sizeof(n),"%u G",cost);writing(s,245,185,n,44,35,28,1);
 box(s,5,202,310,19,189,227,160);outline(s,5,202,310,19,65,59,43);
 if(confirmRebirth)writing(s,11,206,"TAP AGAIN: REBIRTH + RESET",110,43,27,1);
 else writing(s,11,206,rebirthReward()?"CLAIM GOLD / REBIRTH":"GOLD BRICK AT LV.20",33,84,28,1);
}
static void beginBoss(void){
 if(saveData.bossActive||saveData.bossWins>=50u)return;
 unsigned entry=(saveData.bossWins+1u)*1000u;
 if(!testMode&&saveData.blackBricks<entry)return;
 if(!testMode)saveData.blackBricks-=entry;
 saveData.bossActive=1;
 bossLevelIndex=(unsigned)bossIndex()+1u;
 bossTimer=(int)currentBoss()->timeLimit*60;
 bossFreezeTimer=0;bossFrozenType=-1;bossAttackStep=0;
 resetBricks();refreshBottom=1;unsaved=1;
}
static void exitBoss(void){
 if(!saveData.bossActive)return;
 saveData.bossActive=0;
 bossTimer=0;bossFreezeTimer=0;bossFrozenType=-1;
 resetBricks();refreshBottom=1;unsaved=1;
}
static void buySkill(unsigned idx){
 unsigned mask=1u<<idx;
 unsigned cost=(idx==0?1u:15u);
 if(idx>1||(saveData.skillMask&mask))return;
 if(!testMode&&saveData.skillPoints<cost)return;
 if(!testMode)saveData.skillPoints-=cost;
 saveData.skillMask|=mask;refreshBottom=1;unsaved=1;sound(7);
}
static void drawBoss(Screen *s){
 char line[96],n[40];
 writing(s,9,36,"SKILLS / BOSS BATTLES",43,39,32,1);
 box(s,8,52,305,42,245,239,217);outline(s,8,52,305,42,31,27,22);
 if(saveData.bossActive){
  snprintf(line,sizeof(line),"BOSS %lu: %s",(unsigned long)bossLevelIndex,bossNames[bossIndex()]);
  writing(s,15,57,line,35,36,32,1);
  if(currentBoss()->timeLimit){
   snprintf(line,sizeof(line),"TIME %02d:%02d   EXIT OR TEST CLEAR",bossTimer/3600,(bossTimer/60)%60);
   writing(s,15,75,line,117,48,39,1);
  }else writing(s,15,75,"BATTLE ACTIVE - EXIT BELOW",78,64,48,1);
 }else if(saveData.bossWins>=50){
  writing(s,15,57,"ALL 50 V1 BOSSES CLEARED",33,113,39,1);
  writing(s,15,75,"END OF CURRENT BOSS ROSTER",78,64,48,1);
 }else{
  snprintf(line,sizeof(line),"BOSS %lu: %s",(unsigned long)saveData.bossWins+1u,bossNames[bossIndex()]);
  writing(s,15,57,line,35,36,32,1);
  snprintf(line,sizeof(line),testMode?"COST %lu BB (TEST: FREE)":"COST: %lu BLACK BRICKS",((unsigned long)saveData.bossWins+1u)*1000ul);
  writing(s,15,75,line,78,64,48,1);
 }
 blit(s,bossPortrait(),266,55,32,32);
 snprintf(line,sizeof(line),"BB:%lu   SKILLS:%lu",(unsigned long)saveData.blackBricks,(unsigned long)saveData.skillPoints);
 writing(s,11,99,line,55,48,35,1);
 writing(s,11,111,"SKILL TREE: 6 COLUMNS",70,63,49,1);
 for(int r=0;r<2;r++)for(int c=0;c<6;c++){
  int x=11+c*50,y=125+r*25;
  int available=r==0&&c<2;
  int purchased=(c<2&&r==0&&((saveData.skillMask>>c)&1));
  box(s,x,y,45,21,purchased?107:(available?242:226),purchased?202:(available?242:222),purchased?68:(available?221:209));
  outline(s,x,y,45,21,44,40,34);
  snprintf(n,sizeof(n),"%d,%d",c,r);writing(s,x+6,y+5,n,65,54,44,1);
 }
 if(saveData.skillMask&1u)writing(s,11,176,"BASIC BALLS DAMAGE BLACK BRICKS",54,108,44,1);
 else writing(s,11,176,"0,0: BASIC DAMAGES BLACK BRICKS",60,51,44,1);
 writing(s,11,187,"1,0: LASERS DAMAGE BOSSES (15 SP)",70,59,48,1);
 box(s,8,201,303,20,saveData.bossActive?250:193,saveData.bossActive?197:227,saveData.bossActive?176:157);
 outline(s,8,201,303,20,67,53,42);
 if(saveData.bossActive&&testMode){
  box(s,167,201,144,20,173,233,168);outline(s,167,201,144,20,67,53,42);
  writing(s,19,205,"EXIT BOSS",48,44,33,1);
  writing(s,181,205,"TEST CLEAR",38,68,34,1);
 }else writing(s,34,205,saveData.bossActive?"EXIT BOSS":(saveData.bossWins>=50?"ALL BOSSES CLEARED":"FIGHT NEXT BOSS"),48,44,33,1);
}
static void drawMap(Screen *s){
 box(s,5,34,310,188,249,240,215);outline(s,5,34,310,188,72,62,43);
 writing(s,14,37,"TAP A BRICK TO DAMAGE IT",44,38,32,1);
 const int x0=21,y0=53,stepX=18,stepY=8;
 // The bottom map also uses the original segmented wall-style rail motif.
 for(int x=x0-2;x<x0+COLS*stepX+2;x+=30){
  int span=x+30>x0+COLS*stepX+2?(x0+COLS*stepX+2)-x:30;
  if(span>0){box(s,x,48,span,4,234,235,233);outline(s,x,48,span,4,46,44,42);}
 }
 for(int r=0;r<ROWS;r++)for(int c=0;c<COLS;c++){
  Brick *b=&bricks[r][c];if(b->hp<=0)continue;
  int x=x0+c*stepX,y=y0+r*stepY;
  if(bossLevel){box(s,x-20,y-12,54,28,250,204,20);outline(s,x-20,y-12,54,28,25,21,16);}
  else if(b->black)box(s,x,y,16,7,16,16,16);
  else {int pal=(int)fmodf(ceilf(b->hp),9.0f);if(pal<0)pal+=9;static const unsigned char palette[9][3]={
    {153,255,85},{85,213,255},{236,85,255},{255,129,85},{253,255,85},
    {85,255,172},{85,152,255},{255,85,180},{255,85,85}};
   box(s,x,y,16,7,palette[pal][0],palette[pal][1],palette[pal][2]);}
 }
 outline(s,x0+chosenX*stepX-1,y0+chosenY*stepY-1,18,9,220,51,45);
 char shotLine[60];snprintf(shotLine,sizeof(shotLine),"SCATTER SHOTS: %u",scatterShots);
 writing(s,15,199,shotLine,59,53,41,1);
 writing(s,15,211,"TAP=CLICK   L/R=CHANGE PAGE",58,50,36,1);
}
static void drawAchievements(Screen *s){
 static const char *ach[8]={"REACH LEVEL 20","REACH LEVEL 100","REACH LEVEL 250",
 "PRESTIGE ONCE","PRESTIGE 5 TIMES","BUY 10 OF EACH BALL","UNLOCK ALL 4 LASERS","EARN 1M TOTAL CASH"};
 int done[8]={saveData.bestLevel>=20,saveData.bestLevel>=100,saveData.bestLevel>=250,
 saveData.prestige>=1,saveData.prestige>=5,1,saveData.prestigeUp[3]>=4,saveData.lifetime>=1000000};
 for(int t=0;t<BTYPES;t++)if(saveData.owned[t]<10)done[5]=0;
 writing(s,14,38,"ACHIEVEMENTS",49,45,32,1);
 for(int i=0;i<8;i++){
  int y=56+i*19;
  box(s,8,y,304,18,done[i]?199:245,done[i]?237:230,done[i]?183:211);
  writing(s,13,y+4,done[i]?"OK":"--",done[i]?25:114,done[i]?99:89,done[i]?36:60,1);
  writing(s,42,y+4,ach[i],56,52,41,1);
 }
 writing(s,14,213,"ACHIEVEMENTS UPDATE AUTOMATICALLY",61,53,44,1);
}
static void drawSettings(Screen *s){
 char line[96];
 const char *heading="SETTINGS / SAVE TRANSFER";
 if(settingsStatus==1)heading="EXPORT COMPLETE";
 else if(settingsStatus==2)heading="IMPORT COMPLETE";
 else if(settingsStatus==3)heading="FACTORY RESET COMPLETE";
 else if(settingsStatus==-3)heading="RESET FAILED - SD WRITE ERROR";
 else if(settingsStatus==-2)heading="INVALID / CORRUPTED SAVE";
 else if(settingsStatus==-1)heading="SD FILE ERROR";
 writing(s,12,35,heading,settingsStatus<0?154:46,41,29,1);
 static const char *bgNames[5]={"CLASSIC BEIGE","LIGHT BLUE","WHITE","PEACH","SLATE"};
 const int ys[5]={50,75,100,125,150};
 for(int i=0;i<5;i++){
  int y=ys[i];box(s,9,y,302,23,250,239,211);outline(s,9,y,302,23,79,68,51);
  if(i==0)snprintf(line,sizeof(line),"SOUND: %s",saveData.soundEnabled?"ON":"OFF");
  else if(i==1)snprintf(line,sizeof(line),"FPS: %s",perfMode?"FAST":"QUALITY");
  else if(i==2)snprintf(line,sizeof(line),"TEST: %s (UNLIMITED CASH)",testMode?"ON":"OFF");
  else if(i==3)snprintf(line,sizeof(line),"BACKGROUND: %s",bgNames[saveData.bgColor%5]);
  else snprintf(line,sizeof(line),"BALL EFFECTS: %s",saveData.effectsEnabled?"ON":"OFF");
  writing(s,16,y+6,line,34,43,31,1);
 }
 box(s,9,178,145,23,180,226,160);outline(s,9,178,145,23,56,90,43);
 writing(s,19,185,"EXPORT SAVE",31,62,34,1);
 box(s,165,178,145,23,225,201,154);outline(s,165,178,145,23,99,61,41);
 writing(s,170,185,"BROWSE SD",60,43,31,1);
 // Deliberately separate from Export/Import to avoid an accidental reset.
 box(s,9,202,301,19,248,207,200);outline(s,9,202,301,19,154,56,51);
 writing(s,92,206,"FACTORY RESET",137,34,29,1);
}
static void drawFactoryResetWarning(Screen *s){
 // Modal is drawn on TOP of the bottom-screen Settings page.
 box(s,6,42,308,175,251,239,220);outline(s,6,42,308,175,125,39,39);
 box(s,12,49,296,24,210,68,55);
 writing(s,35,55,"WARNING: FACTORY RESET",255,255,243,1);
 writing(s,16,82,"ERASE ALL GAME PROGRESS?",117,40,33,1);
 writing(s,16,98,"MONEY, BALLS, LEVELS",65,48,41,1);
 writing(s,16,111,"PRESTIGE, GOLD, SKILLS",65,48,41,1);
 writing(s,16,124,"AND SETTINGS WILL RESET.",65,48,41,1);
 writing(s,16,143,"EXPORTED SAVES ARE KEPT",39,85,41,1);
 writing(s,16,155,"IN THE SD /saves FOLDER.",39,85,41,1);
 box(s,12,181,139,30,193,229,188);outline(s,12,181,139,30,64,93,60);
 writing(s,55,190,"CANCEL",43,78,39,1);
 box(s,164,181,144,30,211,92,82);outline(s,164,181,144,30,123,39,37);
 writing(s,187,190,"ERASE ALL",255,250,242,1);
}
static void drawSaveBrowser(Screen *s){
 char line[80];
 writing(s,10,36,"SD SAVE FILES - TAP TO SELECT",49,42,35,1);
 writing(s,9,48,"/3ds/IdleBreakout3DS/saves/",67,57,41,1);
 if(saveListCount==0)writing(s,22,100,"NO EXPORTED .BIN FILES FOUND",145,70,60,1);
 for(int i=0;i<SAVE_PAGE_ROWS;i++){
  int index=saveListPage*SAVE_PAGE_ROWS+i;
  if(index>=saveListCount)break;
  int y=61+i*21;
  int selected=index==saveSelected;
  box(s,8,y,304,19,selected?166:245,selected?218:234,selected?164:213);
  outline(s,8,y,304,19,71,65,44);
  writing(s,13,y+4,saveList[index],51,43,33,1);
 }
 box(s,8,195,55,24,237,226,207);outline(s,8,195,55,24,64,58,45);
 writing(s,14,202,"PREV",54,44,33,1);
 box(s,67,195,55,24,237,226,207);outline(s,67,195,55,24,64,58,45);
 writing(s,72,202,"NEXT",54,44,33,1);
 box(s,126,195,108,24,166,229,145);outline(s,126,195,108,24,64,75,47);
 writing(s,132,202,importConfirm?"CONFIRM!":"IMPORT",45,55,29,1);
 box(s,239,195,73,24,244,207,183);outline(s,239,195,73,24,65,58,45);
 writing(s,245,202,"CANCEL",69,42,34,1);
 snprintf(line,sizeof(line),"%d FILES   %d/%d",saveListCount,saveListPage+1,
  saveListCount?((saveListCount+SAVE_PAGE_ROWS-1)/SAVE_PAGE_ROWS):1);
 writing(s,11,183,line,60,56,43,1);
}
static void drawSocials(Screen *s){
 writing(s,15,38,"SOCIALS - IDLE BREAKOUT 3DS",48,43,33,1);
 box(s,8,58,304,88,238,230,212);outline(s,8,58,304,88,71,63,48);
 blit(s,SPR_DISCORD_LOGO,17,71,60,60);
 writing(s,89,74,"JOIN OUR DISCORD",50,42,32,1);
 writing(s,89,96,"discord.gg/",62,60,48,1);
 writing(s,89,111,"U7rY6x9zkw",48,53,150,1);
 writing(s,12,157,"COMMUNITY",53,47,39,1);
 writing(s,12,173,"JOIN THE SERVER FOR UPDATES",64,51,39,1);
 writing(s,12,187,"AND BUG REPORTS",64,51,39,1);
 writing(s,12,209,"OPEN INVITE ON PHONE OR PC",63,54,41,1);
}
static void drawAbout(Screen *s){
 writing(s,15,40,"ABOUT IDLE BREAKOUT 3DS",51,43,34,1);
 box(s,9,62,302,139,249,237,214);outline(s,9,62,302,139,60,51,41);
 writing(s,17,73,"ORIGINAL IDLE BREAKOUT",47,45,32,1);
 writing(s,17,88,"CREATED BY KODIQI",64,56,40,1);
 writing(s,17,109,"UNOFFICIAL FAN HOMEBREW",50,44,35,1);
 writing(s,17,123,"ADAPTED FOR NINTENDO 3DS",60,54,42,1);
 writing(s,17,145,"VERSION 3.3.3 TEST",50,50,45,1);
 writing(s,17,163,"ORIGINAL ART AND AUDIO",60,53,40,1);
 writing(s,17,177,"BELONG TO THEIR OWNER",60,53,40,1);
 writing(s,13,209,"FREE COMMUNITY PROJECT",53,94,46,1);
}
static void drawBottom(Screen *s){
 blit(s,SPR_UPGRADE0,0,0,BOT_W,HEIGHT);
 char line[72],money[32];
 numeric(saveData.money,money,sizeof(money));
 box(s,4,19,312,14,250,240,211);outline(s,4,19,312,14,50,45,34);
 if(page==3){
  snprintf(line,sizeof(line),"PRESTIGE SHOP    SAVE:%s",saveState==-1?"FAILED":(saveState==1?"OK":"READY"));
 }else{
  if(testMode)snprintf(line,sizeof(line),"TEST MODE: $INF  POINTS INF");
  else snprintf(line,sizeof(line),"$%s   PRESTIGE %lu",money,(unsigned long)saveData.prestige);
 }
 writing(s,12,21,line,24,79,24,1);
 if(page==6&&saveBrowserOpen)drawSaveBrowser(s);
 else if(page<2)drawShop(s);
 else if(page==2)drawRewards(s);
 else if(page==3)drawPrestige(s);
 else if(page==4)drawMap(s);
 else if(page==5)drawAchievements(s);
 else if(page==6)drawSettings(s);
 else if(page==7)drawBoss(s);
 else if(page==8)drawSocials(s);
 else drawAbout(s);
 if(page==6&&factoryResetConfirm&&!saveBrowserOpen)drawFactoryResetWarning(s);
 box(s,5,222,310,17,240,231,203);outline(s,5,222,310,17,50,45,34);
 writing(s,9,225,"<",50,43,32,1);
 snprintf(line,sizeof(line),"%d/10  L/R:PAGE  X:PRESTIGE",page+1);
 writing(s,32,225,line,42,38,31,1);
 writing(s,298,225,">",50,43,32,1);
 if(saveState==-1){
  box(s,145,0,170,18,255,181,173);
  writing(s,150,3,"SAVE FAILED!",130,18,18,1);
 }else if(saveState==1&&frame-saveStateFrame<120){
  box(s,210,0,106,18,199,241,195);
  writing(s,216,3,"SAVE OK",30,105,46,1);
 }
}
static void touchBuy(const touchPosition *t){
 if(page==6&&factoryResetConfirm){
  if(t->py>=181&&t->py<213){
   if(t->px>=164&&t->px<310)factoryResetGame();
   else if(t->px>=12&&t->px<153)factoryResetConfirm=0;
  }
  refreshBottom=1;
  return;
 }
 if(page==6&&saveBrowserOpen){
  if(t->py>=61&&t->py<61+SAVE_PAGE_ROWS*21){
   int n=saveListPage*SAVE_PAGE_ROWS+(t->py-61)/21;
   if(n<saveListCount){saveSelected=n;importConfirm=0;refreshBottom=1;}
  }else if(t->py>=195&&t->py<220){
   if(t->px<64){if(saveListPage>0)saveListPage--;saveSelected=-1;importConfirm=0;}
   else if(t->px<123){if((saveListPage+1)*SAVE_PAGE_ROWS<saveListCount)saveListPage++;saveSelected=-1;importConfirm=0;}
   else if(t->px<237){
    if(saveSelected>=0&&saveSelected<saveListCount){
     if(importConfirm)importGameFile(saveList[saveSelected]);else importConfirm=1;
    }
   }else{saveBrowserOpen=0;importConfirm=0;}
   refreshBottom=1;
  }
  return;
 }
 if(t->py>=222){
  if(t->px<=28)page=(page+SHOP_PAGES-1)%SHOP_PAGES;
  else if(t->px>=281)page=(page+1)%SHOP_PAGES;
  confirmRebirth=0;refreshBottom=1;return;
 }
 if(page<2){
  if(t->py<35||t->py>=218)return;
  int row=(t->py-35)/61;
  int part=(t->py-35)%61;
  if(row<0||row>2||part>=59)return;
  int tball=page*3+row;
  if(part<=22){
   if(t->px<32)sound(ballSound[tball]);
   else if(t->px>=218)buy(tball,0);
  }else if(part>=40&&part<58){
   if(t->px<103)buy(tball,1);
   else if(t->px<215)buy(tball,2);
   else deleteBall(tball);
  }
 }else if(page==2){
  if(t->py>=81&&t->py<216){int idx=(t->py-81)/27;
   if(((t->py-81)%27)<25)usePowerup(idx);}
 }else if(page==3){
  if(t->py>=64&&t->py<179){
   int row=(t->py-64)/23;
   if(row>=0&&row<5&&t->px>=218)purchasePrestige(row);
  }else if(t->py>=181&&t->py<200&&t->px>214){
   unsigned cost=10u+(unsigned)saveData.laserPower*5u;
   if(saveData.laserPower<100&&(testMode||saveData.prestigePoints>=cost)){
    if(!testMode)saveData.prestigePoints-=cost;
    saveData.laserPower++;refreshBottom=1;unsaved=1;sound(7);
   }
  }else if(t->py>=202&&t->py<222&&rebirthReward()>0){
   if(confirmRebirth)rebirth();else confirmRebirth=1;
   refreshBottom=1;
  }
 }else if(page==4){
  const int x0=21,y0=53;
  if(t->px>=x0&&t->px<x0+COLS*18&&t->py>=y0&&t->py<y0+ROWS*8){
   chosenX=(t->px-x0)/18;chosenY=(t->py-y0)/8;
   if(bossLevel){chosenX=7;chosenY=10;}
   clickBrick();refreshBottom=1;
  }
 }else if(page==6){
  if(t->py>=50&&t->py<73){saveData.soundEnabled=!saveData.soundEnabled;unsaved=1;}
  else if(t->py>=75&&t->py<98)perfMode=!perfMode;
  else if(t->py>=100&&t->py<123)testMode=!testMode;
  else if(t->py>=125&&t->py<148){saveData.bgColor=(saveData.bgColor+1)%5;unsaved=1;}
  else if(t->py>=150&&t->py<173){saveData.effectsEnabled=!saveData.effectsEnabled;unsaved=1;}
  else if(t->py>=178&&t->py<201){if(t->px<157)exportGame();else {saveBrowserOpen=1;saveListPage=0;scanSaveFolder();}}
  else if(t->py>=202&&t->py<222){factoryResetConfirm=1;}
  refreshBottom=1;
 }else if(page==7){
  if(t->py>=125&&t->py<146){if(t->px<58)buySkill(0);else if(t->px<107)buySkill(1);}
  else if(t->py>=201&&t->py<222){
   if(saveData.bossActive){
    if(testMode&&t->px>=167){
     // Test-mode shortcut permits verifying all 50 boss portraits and transitions.
     bricks[10][7].hp=1.0f;
     hurt(10,7,10.0f,0,1);checkCleared();
    }else exitBoss();
   }else beginBoss();
  }
  refreshBottom=1;
 }
}
// Lightweight boss modifiers. Frame clock is 60Hz on 3DS (hardware target).
// Boss rush, precise freeze targeting, and the full original skill tree remain incomplete.
static void updateBoss(void){
 if(!saveData.bossActive||bricks[10][7].hp<=0)return;
 const BossSpec *spec=currentBoss();
 if(spec->timeLimit&&bossTimer>0){
  bossTimer--;
  if(bossTimer==0){
   // Time ran out. Preserve completed bosses but end this encounter.
   saveData.bossActive=0;bossLevel=0;bossFrozenType=-1;bossFreezeTimer=0;
   resetBricks();refreshBottom=1;unsaved=1;
   return;
  }
 }
 if(bossFreezeTimer>0){
  bossFreezeTimer--;
  if(bossFreezeTimer==0)bossFrozenType=-1;
 }
 if(frame>0&&frame%180==0&&spec->regenPct){
  Brick *b=&bricks[10][7];
  b->hp=mind(b->maxhp,b->hp+b->maxhp*(double)spec->regenPct*0.01);
 }
 if(spec->freezeStyle && frame>0 && frame%540==0){
  unsigned count[6]={0};
  for(int b=0;b<MAX_BALLS;b++)if(balls[b].active&&!balls[b].sub&&balls[b].type>=0&&balls[b].type<6)
   count[(unsigned)balls[b].type]++;
  bossFrozenType=-1;
  for(int t=0;t<6;t++)if(count[t] && (bossFrozenType<0||count[t]>count[bossFrozenType]))bossFrozenType=t;
  bossFreezeTimer=spec->freezeStyle==2?600:300;
 }
 // Original boss attacks have additional animations/target patterns not yet ported.
}
int main(void){
 gfxInitDefault();
 gfxSetScreenFormat(GFX_TOP,GSP_BGR8_OES);
 gfxSetScreenFormat(GFX_BOTTOM,GSP_BGR8_OES);
 // First launch for both HOME Menu CIA and Homebrew Launcher: prepare SD save folders.
 // If SD mounting/permissions fail, the existing save-status indicator reports errors.
 mkdir("sdmc:/3ds",0777);
 mkdir("sdmc:/3ds/IdleBreakout3DS",0777);
 mkdir(SAVES_DIR,0777);
 loadGame();resetBricks();restoreBalls();audioInit();
 if(saveData.bossActive)bossTimer=(int)currentBoss()->timeLimit*60;
 Screen bg={topBgCache,TOP_W};
 // The old atlas crop contained a ghosted menu rectangle; a clean base fixes it.
 box(&bg,0,0,TOP_W,HEIGHT,248,240,213);
 memset(bottomCache,0,sizeof(bottomCache));
 if(unsaved)saveGame();
 while(aptMainLoop()){
  hidScanInput();uint32_t down=hidKeysDown(),held=hidKeysHeld();
  if(down&KEY_START){if(unsaved)saveGame();break;}
  if(down&KEY_SELECT)saveGame();
  if(down&KEY_L){saveBrowserOpen=0;factoryResetConfirm=0;page=(page+SHOP_PAGES-1)%SHOP_PAGES;confirmRebirth=0;refreshBottom=1;}
  if(down&KEY_R){saveBrowserOpen=0;factoryResetConfirm=0;page=(page+1)%SHOP_PAGES;confirmRebirth=0;refreshBottom=1;}
  if(down&KEY_X){saveBrowserOpen=0;factoryResetConfirm=0;page=3;confirmRebirth=0;refreshBottom=1;}
  if(down&KEY_Y){saveData.soundEnabled=!saveData.soundEnabled;refreshBottom=1;unsaved=1;}
  if((down&KEY_DUP)||((held&KEY_DUP)&&frame%7==0))chosenY=(chosenY+ROWS-1)%ROWS;
  if((down&KEY_DDOWN)||((held&KEY_DDOWN)&&frame%7==0))chosenY=(chosenY+1)%ROWS;
  if((down&KEY_DLEFT)||((held&KEY_DLEFT)&&frame%7==0))chosenX=(chosenX+COLS-1)%COLS;
  if((down&KEY_DRIGHT)||((held&KEY_DRIGHT)&&frame%7==0))chosenX=(chosenX+1)%COLS;
  if((down&KEY_A)||((held&KEY_A)&&frame%8==0))clickBrick();
  if(down&KEY_TOUCH){touchPosition t;hidTouchRead(&t);touchBuy(&t);}
  // Never pause the physics or lasers while the award animation plays.
  updateBoss();
  updateBalls();
  demonCorePulse();
  if(saveData.prestigeUp[3]>0)fireLasers();
  if(saveData.powerupFrames>0){
   saveData.powerupFrames--;if(saveData.powerupFrames==0){
    if(saveData.activePowerup==4)for(int b=0;b<MAX_BALLS;b++){
     if(balls[b].active&&balls[b].type==6&&balls[b].ttl==255){balls[b].active=0;activeBalls--;}
    }
    saveData.activePowerup=0;refreshBottom=1;unsaved=1;
   }
  }
  if(laserFrames>0)laserFrames--;
  Screen top={gfxGetFramebuffer(GFX_TOP,GFX_LEFT,NULL,NULL),TOP_W};
  drawTop(&top);
  if(frame%24==0||page==4||(page==2&&frame%60==0))refreshBottom=1;
  if(!bottomValid||refreshBottom){
   Screen bottom={bottomCache,BOT_W};drawBottom(&bottom);
   bottomValid=1;refreshBottom=0;
  }
  memcpy(gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,NULL,NULL),bottomCache,sizeof(bottomCache));
  // In V2 a slow SD write occurred after EVERY ball upgrade; now save periodically.
  if(frame>0&&frame%1200==0&&unsaved)saveGame();
  gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();frame++;
 }
 audioExit();if(unsaved)saveGame();gfxExit();return 0;
}

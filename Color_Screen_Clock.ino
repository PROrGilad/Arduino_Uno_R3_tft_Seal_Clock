/*
   ===== MCUFRIEND 2.8" UNO — Tech HUD Clock (Seal BG, Flicker-Reduced, Pulsing Colons) =====
   - Procedural ocean + seal background (no SD)
   - Auto-fit seven-seg HH:MM:SS (24h)
   - Two-line date:
       1) DD-MM-YYYY
       2) Friday , October 10
   - Flicker fixes:
       * Seconds bar frame drawn once; only fill updates
       * Per-segment delta drawing (only changed segments update)
   - Colons pulse once per second (smooth brightness)
   - Time set from PC (__DATE__/__TIME__)
*/

#include <MCUFRIEND_kbv.h>
#include <Adafruit_GFX.h>
#include <string.h>
#include <math.h>   // for cosf

MCUFRIEND_kbv tft;

// === Config ===
#define ENABLE_SCANLINE 0  // set to 1 to re-enable the subtle moving scanline

// ---------- Colors ----------
const uint16_t COL_BG      = 0x0000; // black
const uint16_t COL_TEXT    = 0xFFFF; // white
const uint16_t COL_BAR     = 0x05BF; // cyan-ish
const uint16_t COL_GLOW    = 0x07FF; // neon cyan (colon max)
const uint16_t COL_GLOW2   = 0x07E0; // neon green
const uint16_t COL_GLOW_DIM= 0x025F; // dim cyan (colon min)

// Ocean palette
const uint16_t OCEAN_TOP   = 0x0255; // dark teal
const uint16_t OCEAN_MID   = 0x039B; // medium teal
const uint16_t OCEAN_BOT   = 0x043F; // lighter teal / cyan

// Seal palette (grays)
const uint16_t SEAL_DARK   = 0x632C; // dark gray
const uint16_t SEAL_MID    = 0x8410; // mid gray
const uint16_t SEAL_LIGHT  = 0xAD55; // light gray
const uint16_t SEAL_EYE    = 0x0000; // black
const uint16_t SEAL_WHISK  = 0xFFFF; // white whiskers

// ---------- Screen ----------
uint16_t W, H;

// ---------- Time ----------
struct ClockTime { int year, month, day, hour, minute, second; } nowTime;
unsigned long lastTickMs = 0;   // aligned to the start of the current second
unsigned long subAnimMs  = 0;

// ---------- Seven-seg (VARIABLE for auto-fit) ----------
int SEG_THICK = 5;
int SEG_LEN   = 28;
int SEG_HGAP  = 7;
int SEG_VLEN  = 28;

// ---------- Layout ----------
int DIGIT_Y;
int DIG_X[6];
int COLON_X[2];
int COLON_Y;
int COLON_W = 8;
int E_SP    = 8;

// ---------- Segment indices ----------
enum { SEG_A=0, SEG_B=1, SEG_C=2, SEG_D=3, SEG_E=4, SEG_F=5, SEG_G=6 };

// ---------- Segment masks ----------
const uint8_t DIGIT_MASK[10] = {
  /*0*/ 0b0111111, /*1*/ 0b0000110, /*2*/ 0b1011011, /*3*/ 0b1001111, /*4*/ 0b1100110,
  /*5*/ 0b1101101, /*6*/ 0b1111101, /*7*/ 0b0000111, /*8*/ 0b1111111, /*9*/ 0b1101111
};

// caches
char prevTimeStr[9]  = "??:??:??";
char prevDateStr[16] = "";
uint8_t prevDigitMask[6] = {0,0,0,0,0,0};  // for delta segment drawing

// ---------- Date helpers ----------
bool isLeap(int y){return((y%4==0&&y%100!=0)||y%400==0);}
int daysInMonth(int y,int m){static const uint8_t d[12]={31,28,31,30,31,30,31,31,30,31,30,31};return(m==2)?d[1]+(isLeap(y)?1:0):d[m-1];}
int monthFromAbbrev(const char* mmm){
  if(!strncmp(mmm,"Jan",3))return 1;if(!strncmp(mmm,"Feb",3))return 2;if(!strncmp(mmm,"Mar",3))return 3;
  if(!strncmp(mmm,"Apr",3))return 4;if(!strncmp(mmm,"May",3))return 5;if(!strncmp(mmm,"Jun",3))return 6;
  if(!strncmp(mmm,"Jul",3))return 7;if(!strncmp(mmm,"Aug",3))return 8;if(!strncmp(mmm,"Sep",3))return 9;
  if(!strncmp(mmm,"Oct",3))return 10;if(!strncmp(mmm,"Nov",3))return 11;if(!strncmp(mmm,"Dec",3))return 12;
  return 1;
}
void parseCompileDateTime(ClockTime &ct){
  const char* D=__DATE__;const char* T=__TIME__;
  ct.month=monthFromAbbrev(D);
  ct.day=(D[4]==' ')?(D[5]-'0'):((D[4]-'0')*10+(D[5]-'0'));
  ct.year=(D[7]-'0')*1000+(D[8]-'0')*100+(D[9]-'0')*10+(D[10]-'0');
  ct.hour=(T[0]-'0')*10+(T[1]-'0');ct.minute=(T[3]-'0')*10+(T[4]-'0');ct.second=(T[6]-'0')*10+(T[7]-'0');
}
void tickSeconds(ClockTime &ct,int add){
  ct.second+=add;
  while(ct.second>=60){ct.second-=60;ct.minute++;}
  while(ct.minute>=60){ct.minute-=60;ct.hour++;}
  while(ct.hour>=24){ct.hour-=24;ct.day++;}
  while(ct.day>daysInMonth(ct.year,ct.month)){ct.day-=daysInMonth(ct.year,ct.month);ct.month++;if(ct.month>12){ct.month=1;ct.year++;}}
}
int weekdayIndex(int y,int m,int d){static int t[]={0,3,2,5,0,3,5,1,4,6,2,4};if(m<3)y-=1;return(y+y/4-y/100+y/400+t[m-1]+d)%7;}
const char* WEEKDAY_NAME[7]={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
const char* MONTH_NAME[12]={"January","February","March","April","May","June","July","August","September","October","November","December"};

// ---------- Auto-size ----------
int totalRowWidth(){int dboxW=SEG_LEN+2*SEG_HGAP+4;return 6*dboxW+2*COLON_W+7*E_SP;}
void autoSizeToFit(){
  const int MARGIN=8,MIN_SP=3,MIN_COLON=4,MIN_THICK=4,MIN_LEN=16,MIN_VLEN=16;
  for(int g=0;g<200;++g){int need=totalRowWidth();if(need<=W-2*MARGIN)break;
    if(E_SP>MIN_SP){E_SP--;continue;}if(COLON_W>MIN_COLON){COLON_W--;continue;}
    if(SEG_LEN>MIN_LEN){SEG_LEN--;continue;}if(SEG_VLEN>MIN_VLEN){SEG_VLEN--;continue;}
    if(SEG_THICK>MIN_THICK){SEG_THICK--;continue;}break;}
  Serial.print("Auto-size -> LEN=");Serial.print(SEG_LEN);Serial.print(" VLEN=");Serial.print(SEG_VLEN);
  Serial.print(" TH=");Serial.print(SEG_THICK);Serial.print(" SP=");Serial.print(E_SP);
  Serial.print(" COLON_W=");Serial.println(COLON_W);
}

// ---------- Procedural Background (Ocean + Seal) ----------
uint16_t lerp565(uint16_t c1, uint16_t c2, uint8_t t){
  uint16_t r1=(c1>>11)&0x1F, g1=(c1>>5)&0x3F, b1=c1&0x1F;
  uint16_t r2=(c2>>11)&0x1F, g2=(c2>>5)&0x3F, b2=c2&0x1F;
  uint16_t r = ( (r1*(255-t) + r2*t) / 255 );
  uint16_t g = ( (g1*(255-t) + g2*t) / 255 );
  uint16_t b = ( (b1*(255-t) + b2*t) / 255 );
  return (r<<11) | (g<<5) | b;
}
void drawOceanGradient() {
  int midY = H * 2 / 5;
  for (int y = 0; y < midY; ++y) {
    uint8_t t = (uint32_t)y * 255 / (midY ? midY : 1);
    uint16_t col = lerp565(OCEAN_TOP, OCEAN_MID, t);
    tft.drawFastHLine(0, y, W, col);
  }
  for (int y = midY; y < (int)H; ++y) {
    uint8_t t = (uint32_t)(y - midY) * 255 / ((H - midY) ? (H - midY) : 1);
    uint16_t col = lerp565(OCEAN_MID, OCEAN_BOT, t);
    tft.drawFastHLine(0, y, W, col);
  }
}
void ftri(int x0,int y0,int x1,int y1,int x2,int y2,uint16_t c){ tft.fillTriangle(x0,y0,x1,y1,x2,y2,c); }
void drawSeal(int cx, int cy, int scale) {
  int br = 28*scale/10;
  tft.fillCircle(cx, cy, br+6, SEAL_DARK);
  tft.fillCircle(cx+8*scale/10, cy+2*scale/10, br+4, SEAL_DARK);
  tft.fillCircle(cx-12*scale/10, cy+2*scale/10, br, SEAL_DARK);
  tft.fillCircle(cx-2*scale/10, cy+4*scale/10, br-8, SEAL_LIGHT);
  int hr = 14*scale/10; int hx = cx + 24*scale/10, hy = cy - 10*scale/10;
  tft.fillCircle(hx, hy, hr, SEAL_MID);
  tft.fillCircle(hx + hr/3, hy + hr/3, 2*scale/10 + 1, SEAL_EYE);
  tft.fillCircle(hx - hr/3, hy - hr/4, 2, SEAL_EYE);
  tft.fillCircle(hx + hr/6, hy - hr/3, 2, SEAL_EYE);
  for (int i=-1;i<=1;i++){
    tft.drawLine(hx - 2, hy + 2 + 3*i, hx - 10 - 6*i, hy + 2 + 3*i, SEAL_WHISK);
    tft.drawLine(hx + 2, hy + 2 + 3*i, hx + 10 + 6*i, hy + 2 + 3*i, SEAL_WHISK);
  }
  ftri(cx-6*scale/10, cy+br-8, cx-20*scale/10, cy+br+6, cx-2*scale/10, cy+br+6, SEAL_MID);
  ftri(cx+10*scale/10, cy+br-6, cx+22*scale/10, cy+br+6, cx+2*scale/10, cy+br+6, SEAL_MID);
  int tx = cx - br - 10*scale/10, ty = cy + 2*scale/10;
  ftri(tx, ty, tx-12*scale/10, ty-8*scale/10, tx-12*scale/10, ty+8*scale/10, SEAL_MID);
  tft.drawCircle(cx, cy, br+7, SEAL_MID);
}
void drawSealBackground() {
  drawOceanGradient();
  int baseY = H*3/4;
  tft.fillRoundRect(W/4, baseY, W/2, 12, 6, 0xC618);
  tft.drawRoundRect(W/4, baseY, W/2, 12, 6, 0x8410);
  drawSeal(W/2 - 10, baseY - 18, 12);
}

// ---------- Seven-seg helpers ----------
void drawHSeg(int x,int y,int len,uint16_t c){tft.fillRoundRect(x,y,len,SEG_THICK,SEG_THICK/2,c);}
void drawVSeg(int x,int y,int len,uint16_t c){tft.fillRoundRect(x,y,SEG_THICK,len,SEG_THICK/2,c);}

// draw only the segments contained in 'mask'
void drawDigitSegments(int x0,int y0,uint8_t mask,bool erase){
  uint16_t col = erase ? COL_BG : COL_GLOW;
  if(mask & (1<<SEG_A)) drawHSeg(x0+SEG_HGAP, y0, SEG_LEN, col);
  if(mask & (1<<SEG_G)) drawHSeg(x0+SEG_HGAP, y0+SEG_VLEN+SEG_THICK, SEG_LEN, col);
  if(mask & (1<<SEG_D)) drawHSeg(x0+SEG_HGAP, y0+2*SEG_VLEN+2*SEG_THICK, SEG_LEN, col);
  if(mask & (1<<SEG_B)) drawVSeg(x0+SEG_HGAP+SEG_LEN, y0+SEG_THICK/2, SEG_VLEN, col);
  if(mask & (1<<SEG_C)) drawVSeg(x0+SEG_HGAP+SEG_LEN, y0+SEG_VLEN+(3*SEG_THICK)/2, SEG_VLEN, col);
  if(mask & (1<<SEG_F)) drawVSeg(x0, y0+SEG_THICK/2, SEG_VLEN, col);
  if(mask & (1<<SEG_E)) drawVSeg(x0, y0+SEG_VLEN+(3*SEG_THICK)/2, SEG_VLEN, col);
}

// outline once (cosmetic)
void drawDigitOutline(int x0,int y0){
  tft.drawRoundRect(x0-2,y0-2,SEG_LEN+2*SEG_HGAP+4,2*SEG_VLEN+3*SEG_THICK+4,6,COL_GLOW2);
}

// ---------- Pulsing colons ----------
void drawColonColor(uint16_t col){
  int r = SEG_THICK/2 + 1;
  for(int i=0;i<2;i++){
    tft.fillCircle(COLON_X[i], COLON_Y, r, col);
    tft.fillCircle(COLON_X[i], COLON_Y + SEG_VLEN, r, col);
  }
}

// compute smooth pulse color based on fractional second
void updateColonPulse(unsigned long ms){
  // phase in [0..1) within the current second
  unsigned long frac = (ms >= lastTickMs) ? (ms - lastTickMs) : 0;
  if (frac > 999) frac = 999;
  float phase = frac / 1000.0f;

  // cosine ease: 0→1→0 across the second
  // brightness = 0.5*(1 - cos(2π*phase))
  float bright = 0.5f * (1.0f - cosf(2.0f * 3.1415926f * phase));
  uint8_t t = (uint8_t)(bright * 255.0f + 0.5f);

  // blend between dim and bright cyan
  uint16_t col = lerp565(COL_GLOW_DIM, COL_GLOW, t);
  drawColonColor(col);
}

// --- Seconds bar (frame once, fill only) ---
int barX=10, barY, barW, barH=10;
void drawSecondsBarFrameOnce(){
  barY = H - 26; barW = W - 20;
  tft.drawRoundRect(barX-1,barY-1,barW+2,barH+2,4,COL_GLOW2);
}
int prevFillW = -1;
void drawSecondsBarFill(uint8_t sec){
  int fillW = map(sec,0,59,0,barW);
  if (fillW == prevFillW) return;  // nothing to change
  if (prevFillW < 0) { // first time
    tft.fillRect(barX, barY, fillW, barH, COL_BAR);
    prevFillW = fillW;
    return;
  }
  if (fillW > prevFillW) {
    // extend fill
    tft.fillRect(barX + prevFillW, barY, fillW - prevFillW, barH, COL_BAR);
  } else {
    // shrink fill: clear the tail
    tft.fillRect(barX + fillW, barY, (prevFillW - fillW), barH, COL_BG);
  }
  prevFillW = fillW;
}

// Two-line date
void drawDateLine(const ClockTime &ct){
  char line1[16];snprintf(line1,sizeof(line1),"%02d-%02d-%04d",ct.day,ct.month,ct.year);
  if(strcmp(line1,prevDateStr)!=0){
    tft.fillRect(0,H-70,W,45,COL_BG);
    tft.setTextSize(2);tft.setTextColor(COL_TEXT,COL_BG);
    int16_t x1,y1;uint16_t w1,h1;tft.getTextBounds(line1,0,0,&x1,&y1,&w1,&h1);
    tft.setCursor((W-w1)/2,H-70);tft.print(line1);
    int wd=weekdayIndex(ct.year,ct.month,ct.day);
    const char* wn=WEEKDAY_NAME[wd];const char* mn=MONTH_NAME[ct.month-1];
    char line2[32];snprintf(line2,sizeof(line2),"%s , %s %d",wn,mn,ct.day);
    int16_t x2,y2;uint16_t w2,h2;tft.getTextBounds(line2,0,0,&x2,&y2,&w2,&h2);
    tft.setCursor((W-w2)/2,H-50);tft.print(line2);
    strncpy(prevDateStr,line1,sizeof(prevDateStr));
  }
}

// draw digits with per-segment delta (minimizes redraw)
void drawTimeDigits(const ClockTime &ct){
  char cur[9];snprintf(cur,sizeof(cur),"%02d:%02d:%02d",ct.hour,ct.minute,ct.second);

  for(int i=0;i<8;i++){
    if(i==2||i==5) continue;        // colons handled separately
    if(cur[i]==prevTimeStr[i]) continue;

    int di=(i<2)?i:(i<5?i-1:i-2);   // map to digit index
    int dx=DIG_X[di];

    // new & old masks
    uint8_t newMask = (cur[i]>='0'&&cur[i]<='9')? DIGIT_MASK[cur[i]-'0'] : 0;
    uint8_t oldMask = prevDigitMask[di];

    // segments that turned OFF
    uint8_t offMask = (uint8_t)(oldMask & ~newMask);
    if (offMask) drawDigitSegments(dx, DIGIT_Y, offMask, /*erase=*/true);

    // segments that turned ON
    uint8_t onMask  = (uint8_t)(newMask & ~oldMask);
    if (onMask)  drawDigitSegments(dx, DIGIT_Y, onMask, /*erase=*/false);

    // store
    prevDigitMask[di] = newMask;
  }
  strncpy(prevTimeStr,cur,sizeof(prevTimeStr));
}

void animateScanline(){
#if ENABLE_SCANLINE
  static int y=28;tft.drawFastHLine(5,y,W-10,COL_BG);
  y+=2;if(y>(int)(H-30))y=28;
  tft.drawFastHLine(5,y,W-10,COL_GLOW);
#endif
}

// ---------- Layout ----------
void setupLayout(){
  DIGIT_Y=56;int dboxW=SEG_LEN+2*SEG_HGAP+4;
  int totalW=6*dboxW+2*COLON_W+7*E_SP;
  int startX=max(4,(int)W-totalW>0?(int)((W-totalW)/2):4);
  int x=startX;
  DIG_X[0]=x;x+=dboxW+E_SP;DIG_X[1]=x;x+=dboxW+E_SP;
  COLON_X[0]=x+COLON_W/2;x+=COLON_W+E_SP;
  DIG_X[2]=x;x+=dboxW+E_SP;DIG_X[3]=x;x+=dboxW+E_SP;
  COLON_X[1]=x+COLON_W/2;x+=COLON_W+E_SP;
  DIG_X[4]=x;x+=dboxW+E_SP;DIG_X[5]=x;
  COLON_Y=DIGIT_Y+SEG_VLEN/2-2;

  // draw static digit outlines once (cosmetic)
  for (int di=0; di<6; ++di) drawDigitOutline(DIG_X[di], DIGIT_Y);
}

// ---------- Arduino ----------
void setup(){
  Serial.begin(9600);
  uint16_t id=tft.readID();if(id==0xD3D3)id=0x9486;
  tft.begin(id);tft.setRotation(1);W=tft.width();H=tft.height();

  parseCompileDateTime(nowTime);
  autoSizeToFit();
  setupLayout();

  // Background
  drawSealBackground();

  // Title strip (readable over the background)
  tft.fillRect(0,0,W,24,COL_GLOW2);
  tft.setTextSize(2);tft.setTextColor(COL_BG,COL_GLOW2);
  tft.setCursor(6,4);tft.print("SYNC:");
  tft.print(__DATE__);tft.print(" ");tft.print(__TIME__);

  // Initial UI
  memset(prevTimeStr,0,sizeof(prevTimeStr));
  memset(prevDateStr,0,sizeof(prevDateStr));
  for (int i=0;i<6;i++) prevDigitMask[i]=0;  // force first draw

  drawTimeDigits(nowTime);     // draws all segments
  drawDateLine(nowTime);
  drawSecondsBarFrameOnce();   // frame once
  drawSecondsBarFill(nowTime.second);

  lastTickMs=millis();
  subAnimMs=lastTickMs;
}

void loop(){
  unsigned long ms=millis();

  // ~30ms: animation + pulsing colons
  if(ms-subAnimMs>=30){
    subAnimMs=ms;
    animateScanline();                    // does nothing when ENABLE_SCANLINE=0
    updateColonPulse(ms);                 // smooth brightness synced to the current second
  }

  // 1s: tick clock and update digits/bar/date
  if(ms-lastTickMs>=1000){
    lastTickMs+=1000;
    tickSeconds(nowTime,1);

    drawTimeDigits(nowTime);              // per-segment delta
    drawSecondsBarFill(nowTime.second);   // only changed width

    if(nowTime.hour==0&&nowTime.minute==0&&nowTime.second==0){
      drawDateLine(nowTime);
    }
  }
}

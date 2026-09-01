#pragma once
// gr_icons.h — 1:1 translation of artifact 28f46b57 icon() function
// Every icon drawn with Arduino_GFX primitives matching artifact canvas code exactly.
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "gr_config.h"

// arc outline helper
static inline void grArc(Arduino_GFX* g, int cx, int cy, int r,
                          float sa, float ea, uint16_t c) {
  int n = max(8, (int)(fabsf(ea - sa) * r * 0.5f));
  if (n > 40) n = 40;
  float st = (ea - sa) / n;
  int px = cx + (int)(cosf(sa) * r), py = cy + (int)(sinf(sa) * r);
  for (int i = 1; i <= n; i++) {
    float a = sa + i * st;
    int nx = cx + (int)(cosf(a) * r), ny = cy + (int)(sinf(a) * r);
    g->drawLine(px, py, nx, ny, c);
    px = nx; py = ny;
  }
}
// ellipse outline helper
static inline void grEllipse(Arduino_GFX* g, int cx, int cy, int rx, int ry, uint16_t c) {
  int n = max(16, max(rx, ry) * 2);
  if (n > 48) n = 48;
  float st = 2.0f * PI / n;
  int px = cx + rx, py = cy;
  for (int i = 1; i <= n; i++) {
    float a = i * st;
    int nx = cx + (int)(cosf(a) * rx), ny = cy + (int)(sinf(a) * ry);
    g->drawLine(px, py, nx, ny, c);
    px = nx; py = ny;
  }
}
// quadratic bezier helper
static inline void grQuad(Arduino_GFX* g, int x0, int y0, int cpx, int cpy,
                           int x1, int y1, uint16_t c) {
  int px = x0, py = y0;
  for (int i = 1; i <= 10; i++) {
    float t = i * 0.1f, u = 1.0f - t;
    int nx = (int)(u*u*x0 + 2*u*t*cpx + t*t*x1);
    int ny = (int)(u*u*y0 + 2*u*t*cpy + t*t*y1);
    g->drawLine(px, py, nx, ny, c);
    px = nx; py = ny;
  }
}

// icon type enum — matches artifact color map order
enum GrIco : uint8_t {
  ICO_GARAGE, ICO_WEATHER, ICO_CAR_KEY, ICO_ALARM, ICO_SMART_HOME,
  ICO_RADAR, ICO_BRAIN, ICO_MESH, ICO_CROSSHAIR, ICO_SHIELD,
  ICO_GHOST, ICO_GECKO, ICO_DATABASE, ICO_GEAR, ICO_SKULL,
  ICO_EYE, ICO_CPU, ICO_SWEEP, ICO_SPEAKER, ICO_MUTE,
  ICO_NODE_UP, ICO_NODE_DOWN, ICO_MESH_OK, ICO_SD_CARD,
  ICO_RADIO_TX, ICO_RADIO_IDLE, ICO_GATE, ICO_TIRE, ICO_BELL,
  ICO_MEDICAL, ICO_METER, ICO_WRENCH, ICO_CAR, ICO_BADGE,
  ICO_PAGER, ICO_BABY, ICO_LEAF, ICO_CONTROLLER, ICO_TOWER,
  ICO_BOLT, ICO_QUESTION, ICO_HOURGLASS, ICO_BOOK, ICO_FLAME,
  ICO_CHAIN, ICO_WIFI, ICO_HOUSE, ICO_SIREN, ICO_ANTENNA
};

static uint16_t grIcoColor(uint8_t t) {
  switch (t) {
    case ICO_GARAGE: case ICO_CAR_KEY: case ICO_RADAR: case ICO_CROSSHAIR:
    case ICO_CPU: case ICO_SD_CARD: case ICO_CAR: case ICO_CONTROLLER:
    case ICO_ANTENNA: return GR_C_CYAN;
    case ICO_WEATHER: case ICO_SMART_HOME: case ICO_GECKO: case ICO_SPEAKER:
    case ICO_NODE_UP: case ICO_MESH_OK: case ICO_RADIO_IDLE: case ICO_TIRE:
    case ICO_BELL: case ICO_BABY: case ICO_LEAF: case ICO_WIFI:
    case ICO_HOUSE: return GR_C_GRN;
    case ICO_ALARM: case ICO_SHIELD: case ICO_EYE: case ICO_SWEEP:
    case ICO_GATE: case ICO_SIREN: return GR_C_MAG;
    case ICO_MESH: case ICO_GHOST: case ICO_TOWER: case ICO_BOOK:
      return GR_C_PUR;
    case ICO_SKULL: case ICO_MUTE: case ICO_NODE_DOWN: case ICO_RADIO_TX:
    case ICO_MEDICAL: return GR_C_RED;
    case ICO_DATABASE: case ICO_BADGE: case ICO_METER: case ICO_BOLT:
    case ICO_HOURGLASS: case ICO_FLAME: return GR_C_AMB;
    default: return GR_C_DIM;
  }
}

// main icon draw — exact port of artifact icon(x,y,s,t)
static void grIcon(Arduino_GFX* g, int x, int y, int s, uint8_t t) {
  int h = s / 2;
  uint16_t c = grIcoColor(t);

  switch (t) {

  case ICO_GARAGE:
    g->drawRect(x+1,y+1,s-2,s-2,c);
    g->drawFastHLine(x+2, y+(int)(h*0.6f), s-4, c);
    g->drawFastHLine(x+2, y+h, s-4, c);
    g->drawFastHLine(x+2, y+(int)(h*1.4f), s-4, c);
    g->fillCircle(x+h+(int)(h*0.3f), y+(int)(h*1.7f), max(1,(int)(s*0.06f)), c);
    break;

  case ICO_WEATHER: {
    int tx2=x+h, bw=(int)(s*0.15f), bh2=(int)(s*0.6f);
    g->fillRect(tx2-bw, y+(int)(s*0.1f), bw*2, bh2, GR_C_BG);
    g->drawRect(tx2-bw, y+(int)(s*0.1f), bw*2, bh2, c);
    g->fillCircle(tx2, y+(int)(s*0.78f), (int)(s*0.2f), c);
    g->fillRect(tx2-(int)(bw*0.5f), y+(int)(s*0.35f), bw, (int)(bh2*0.45f), c);
    for(int i=0;i<3;i++)
      g->drawLine(tx2+bw+1, y+(int)(s*0.2f)+i*(int)(s*0.15f),
                  tx2+bw+(int)(s*0.12f), y+(int)(s*0.2f)+i*(int)(s*0.15f), c);
  } break;

  case ICO_CAR_KEY:
    g->drawRoundRect(x+(int)(s*0.12f),y+(int)(s*0.2f),(int)(s*0.76f),(int)(s*0.6f),(int)(s*0.06f),c);
    g->fillRect(x+(int)(s*0.82f),y+h-(int)(s*0.04f),(int)(s*0.15f),(int)(s*0.08f),c);
    for(int k=0;k<3;k++)
      g->fillRect(x+(int)(s*0.85f)+k*(int)(s*0.04f),y+h+(int)(s*0.04f),(int)(s*0.03f),(int)(s*0.04f),GR_C_BG);
    g->fillCircle(x+(int)(s*0.3f),y+h,(int)(s*0.07f),c);
    g->drawCircle(x+(int)(s*0.5f),y+h,(int)(s*0.07f),c);
    g->fillRect(x+(int)(s*0.64f),y+h-(int)(s*0.04f),(int)(s*0.1f),(int)(s*0.08f),c);
    g->drawCircle(x+(int)(s*0.2f),y+(int)(s*0.12f),(int)(s*0.06f),c);
    break;

  case ICO_ALARM: case ICO_SIREN:
    grArc(g,x+h,y+(int)(h*0.85f),(int)(h*0.35f),PI,0,c);
    g->fillRect(x+h-(int)(h*0.45f),y+(int)(h*0.85f),(int)(h*0.9f),(int)(s*0.1f),c);
    for(int i=1;i<=2;i++)
      grArc(g,x+h,y+(int)(h*0.55f),(int)(h*0.15f)+i*(int)(s*0.06f),PI*1.25f,PI*1.75f,c);
    break;

  case ICO_SMART_HOME: case ICO_HOUSE:
    g->drawLine(x+h,y+(int)(s*0.15f),x+(int)(s*0.85f),y+h,c);
    g->drawLine(x+h,y+(int)(s*0.15f),x+(int)(s*0.15f),y+h,c);
    g->drawLine(x+(int)(s*0.85f),y+h,x+(int)(s*0.15f),y+h,c);
    g->drawRect(x+(int)(s*0.22f),y+h,(int)(s*0.56f),(int)(s*0.38f),c);
    g->fillRect(x+h-(int)(s*0.07f),y+h+(int)(s*0.15f),(int)(s*0.14f),(int)(s*0.23f),c);
    grArc(g,x+(int)(s*0.7f),y+(int)(s*0.25f),(int)(s*0.08f),PI*1.2f,PI*1.8f,c);
    break;

  case ICO_RADAR: case ICO_ANTENNA:
    grArc(g,x+h,y+(int)(h*0.7f),(int)(h*0.55f),PI*0.85f,PI*0.15f+2*PI,c);
    g->drawLine(x+h,y+(int)(h*0.7f),x+h+(int)(h*0.3f),y+(int)(s*0.2f),c);
    g->fillCircle(x+h+(int)(h*0.3f),y+(int)(s*0.2f),(int)(s*0.06f),c);
    g->drawLine(x+h,y+(int)(h*1.2f),x+h,y+(int)(s*0.9f),c);
    g->drawLine(x+h-(int)(h*0.3f),y+(int)(s*0.9f),x+h+(int)(h*0.3f),y+(int)(s*0.9f),c);
    break;

  case ICO_BRAIN:
    g->drawCircle(x+h-(int)(h*0.15f),y+(int)(h*0.75f),(int)(h*0.4f),c);
    g->drawCircle(x+h+(int)(h*0.15f),y+(int)(h*0.75f),(int)(h*0.4f),c);
    g->drawLine(x+h,y+(int)(h*0.35f),x+h,y+(int)(h*1.15f),c);
    grArc(g,x+h-(int)(h*0.15f),y+(int)(h*0.65f),(int)(h*0.2f),PI*0.3f,PI*1.2f,c);
    grArc(g,x+h+(int)(h*0.15f),y+(int)(h*0.65f),(int)(h*0.2f),PI*1.8f,PI*2.7f,c);
    g->fillRect(x+h-(int)(s*0.04f),y+(int)(h*1.15f),(int)(s*0.08f),(int)(s*0.15f),c);
    break;

  case ICO_MESH: {
    int pts[][2] = {{h,(int)(s*0.2f)},{(int)(s*0.2f),h},{(int)(s*0.8f),h},{h,(int)(s*0.8f)}};
    for(int i=0;i<4;i++) g->fillCircle(x+pts[i][0],y+pts[i][1],(int)(s*0.07f),c);
    for(int i=0;i<4;i++) for(int j=i+1;j<4;j++)
      g->drawLine(x+pts[i][0],y+pts[i][1],x+pts[j][0],y+pts[j][1],c);
  } break;

  case ICO_CROSSHAIR:
    g->drawCircle(x+h,y+h,(int)(h*0.4f),c);
    g->drawLine(x+h,y+(int)(s*0.1f),x+h,y+(int)(s*0.9f),c);
    g->drawLine(x+(int)(s*0.1f),y+h,x+(int)(s*0.9f),y+h,c);
    g->fillCircle(x+h,y+h,(int)(s*0.04f),c);
    break;

  case ICO_SHIELD:
    grQuad(g,x+h,y+(int)(s*0.1f),x+(int)(s*0.85f),y+(int)(s*0.25f),x+(int)(s*0.85f),y+(int)(h*1.1f),c);
    grQuad(g,x+(int)(s*0.85f),y+(int)(h*1.1f),x+(int)(s*0.85f),y+(int)(h*1.1f),x+h,y+(int)(s*0.9f),c);
    grQuad(g,x+h,y+(int)(s*0.9f),x+(int)(s*0.15f),y+(int)(h*1.1f),x+(int)(s*0.15f),y+(int)(h*1.1f),c);
    grQuad(g,x+(int)(s*0.15f),y+(int)(h*1.1f),x+(int)(s*0.15f),y+(int)(s*0.25f),x+h,y+(int)(s*0.1f),c);
    g->drawLine(x+h-(int)(s*0.1f),y+h,x+h,y+h+(int)(s*0.12f),c);
    g->drawLine(x+h,y+h+(int)(s*0.12f),x+h+(int)(s*0.15f),y+h-(int)(s*0.1f),c);
    break;

  case ICO_GHOST:
    grArc(g,x+h,y+(int)(h*0.65f),(int)(h*0.5f),PI,0,c);
    g->drawLine(x+h+(int)(h*0.5f),y+(int)(h*0.65f),x+h+(int)(h*0.5f),y+(int)(s*0.82f),c);
    grQuad(g,x+h+(int)(h*0.5f),y+(int)(s*0.82f),x+h+(int)(h*0.25f),y+(int)(s*0.72f),x+h,y+(int)(s*0.82f),c);
    grQuad(g,x+h,y+(int)(s*0.82f),x+h-(int)(h*0.25f),y+(int)(s*0.72f),x+h-(int)(h*0.5f),y+(int)(s*0.82f),c);
    g->drawLine(x+h-(int)(h*0.5f),y+(int)(s*0.82f),x+h-(int)(h*0.5f),y+(int)(h*0.65f),c);
    g->fillCircle(x+h-(int)(h*0.18f),y+(int)(h*0.58f),(int)(s*0.06f),c);
    g->fillCircle(x+h+(int)(h*0.18f),y+(int)(h*0.58f),(int)(s*0.06f),c);
    break;

  case ICO_GECKO:
    if (grSdReady) grAssetDraw(g, "/assets/sprites/gecko/gecko_boot_140.raw", x, y);
    break;

  case ICO_DATABASE: {
    int ew=(int)(h*0.6f), eh=(int)(h*0.2f);
    for(int i=0;i<3;i++)
      grEllipse(g,x+h,y+(int)(s*0.25f)+i*(int)(s*0.22f),ew,eh,c);
    g->drawLine(x+h-ew,y+(int)(s*0.25f),x+h-ew,y+(int)(s*0.69f),c);
    g->drawLine(x+h+ew,y+(int)(s*0.25f),x+h+ew,y+(int)(s*0.69f),c);
  } break;

  case ICO_GEAR:
    g->drawCircle(x+h,y+h,(int)(h*0.25f),c);
    for(int a=0;a<6;a++){
      float an=a*PI/3.0f;
      int ox=(int)(cosf(an)*h*0.38f), oy=(int)(sinf(an)*h*0.38f);
      g->fillRect(x+h+ox-(int)(s*0.05f),y+h+oy-(int)(s*0.05f),(int)(s*0.1f),(int)(s*0.1f),c);
    }
    break;

  case ICO_SKULL:
    grArc(g,x+h,y+(int)(h*0.6f),(int)(h*0.45f),PI*1.1f,PI*-0.1f+2*PI,c);
    grQuad(g,x+h-(int)(h*0.4f),y+(int)(h*0.85f),x+h,y+(int)(h*1.3f),x+h+(int)(h*0.4f),y+(int)(h*0.85f),c);
    g->fillCircle(x+h-(int)(h*0.18f),y+(int)(h*0.5f),(int)(h*0.12f),GR_C_BG);
    g->drawCircle(x+h-(int)(h*0.18f),y+(int)(h*0.5f),(int)(h*0.12f),c);
    g->fillCircle(x+h+(int)(h*0.18f),y+(int)(h*0.5f),(int)(h*0.12f),GR_C_BG);
    g->drawCircle(x+h+(int)(h*0.18f),y+(int)(h*0.5f),(int)(h*0.12f),c);
    g->drawLine(x+h,y+(int)(h*0.65f),x+h-(int)(s*0.04f),y+(int)(h*0.78f),c);
    g->drawLine(x+h,y+(int)(h*0.65f),x+h+(int)(s*0.04f),y+(int)(h*0.78f),c);
    g->drawLine(x+h-(int)(s*0.04f),y+(int)(h*0.78f),x+h+(int)(s*0.04f),y+(int)(h*0.78f),c);
    for(int i=-2;i<=2;i++)
      g->fillRect(x+h+i*(int)(s*0.06f)-(int)(s*0.02f),y+(int)(h*0.88f),(int)(s*0.04f),(int)(s*0.08f),c);
    break;

  case ICO_EYE:
    grQuad(g,x+(int)(s*0.1f),y+h,x+h,y+(int)(s*0.2f),x+(int)(s*0.9f),y+h,c);
    grQuad(g,x+(int)(s*0.9f),y+h,x+h,y+(int)(s*0.8f),x+(int)(s*0.1f),y+h,c);
    g->fillCircle(x+h,y+h,(int)(h*0.2f),c);
    g->fillCircle(x+h,y+h,(int)(h*0.08f),GR_C_BG);
    for(int i=-2;i<=2;i++){
      int ax=x+h+i*(int)(s*0.1f);
      g->drawLine(ax,y+(int)(s*0.25f),ax+i*(int)(s*0.03f),y+(int)(s*0.12f),c);
    }
    break;

  case ICO_CPU: {
    int p=(int)(s*0.22f);
    g->drawRect(x+p,y+p,s-p*2,s-p*2,c);
    for(int i=0;i<3;i++){
      int o=(int)(s*0.3f)+i*(int)(s*0.15f);
      g->fillRect(x+o,y+(int)(s*0.08f),(int)(s*0.06f),p-(int)(s*0.08f),c);
      g->fillRect(x+o,y+s-p,(int)(s*0.06f),p-(int)(s*0.08f),c);
      g->fillRect(x+(int)(s*0.08f),y+o,p-(int)(s*0.08f),(int)(s*0.06f),c);
      g->fillRect(x+s-p,y+o,p-(int)(s*0.08f),(int)(s*0.06f),c);
    }
    g->fillCircle(x+h,y+h,(int)(s*0.08f),c);
  } break;

  case ICO_SWEEP:
    g->drawCircle(x+h,y+h,(int)(h*0.65f),c);
    g->drawCircle(x+h,y+h,(int)(h*0.35f),c);
    g->drawLine(x+h,y+h,x+h+(int)(h*0.55f),y+(int)(h*0.5f),c);
    grArc(g,x+h,y+h,(int)(h*0.5f),PI*1.6f,PI*1.85f,c);
    g->fillCircle(x+h,y+h,(int)(s*0.05f),c);
    break;

  case ICO_SPEAKER:
    g->drawLine(x+(int)(s*0.15f),y+h-(int)(s*0.1f),x+(int)(s*0.3f),y+h-(int)(s*0.1f),c);
    g->drawLine(x+(int)(s*0.3f),y+h-(int)(s*0.1f),x+(int)(s*0.55f),y+(int)(s*0.15f),c);
    g->drawLine(x+(int)(s*0.55f),y+(int)(s*0.15f),x+(int)(s*0.55f),y+(int)(s*0.85f),c);
    g->drawLine(x+(int)(s*0.55f),y+(int)(s*0.85f),x+(int)(s*0.3f),y+h+(int)(s*0.1f),c);
    g->drawLine(x+(int)(s*0.3f),y+h+(int)(s*0.1f),x+(int)(s*0.15f),y+h+(int)(s*0.1f),c);
    g->drawLine(x+(int)(s*0.15f),y+h+(int)(s*0.1f),x+(int)(s*0.15f),y+h-(int)(s*0.1f),c);
    grArc(g,x+(int)(s*0.6f),y+h,(int)(s*0.12f),PI*1.6f,PI*0.4f,c);
    grArc(g,x+(int)(s*0.6f),y+h,(int)(s*0.22f),PI*1.6f,PI*0.4f,c);
    break;

  case ICO_MUTE:
    g->drawLine(x+(int)(s*0.1f),y+h-(int)(s*0.08f),x+(int)(s*0.25f),y+h-(int)(s*0.08f),c);
    g->drawLine(x+(int)(s*0.25f),y+h-(int)(s*0.08f),x+(int)(s*0.45f),y+(int)(s*0.2f),c);
    g->drawLine(x+(int)(s*0.45f),y+(int)(s*0.2f),x+(int)(s*0.45f),y+(int)(s*0.8f),c);
    g->drawLine(x+(int)(s*0.45f),y+(int)(s*0.8f),x+(int)(s*0.25f),y+h+(int)(s*0.08f),c);
    g->drawLine(x+(int)(s*0.25f),y+h+(int)(s*0.08f),x+(int)(s*0.1f),y+h+(int)(s*0.08f),c);
    g->drawLine(x+(int)(s*0.1f),y+h+(int)(s*0.08f),x+(int)(s*0.1f),y+h-(int)(s*0.08f),c);
    g->drawLine(x+(int)(s*0.55f),y+(int)(s*0.3f),x+(int)(s*0.85f),y+(int)(s*0.7f),GR_C_RED);
    g->drawLine(x+(int)(s*0.85f),y+(int)(s*0.3f),x+(int)(s*0.55f),y+(int)(s*0.7f),GR_C_RED);
    break;

  case ICO_NODE_UP:
    g->fillTriangle(x+h,y+(int)(s*0.15f),x+(int)(s*0.8f),y+h,x+(int)(s*0.6f),y+h,c);
    g->fillTriangle(x+h,y+(int)(s*0.15f),x+(int)(s*0.4f),y+h,x+(int)(s*0.2f),y+h,c);
    g->fillRect(x+(int)(s*0.4f),y+h,(int)(s*0.2f),(int)(s*0.35f),c);
    break;

  case ICO_NODE_DOWN:
    g->drawLine(x+(int)(s*0.2f),y+(int)(s*0.2f),x+(int)(s*0.8f),y+(int)(s*0.8f),c);
    g->drawLine(x+(int)(s*0.8f),y+(int)(s*0.2f),x+(int)(s*0.2f),y+(int)(s*0.8f),c);
    break;

  case ICO_MESH_OK:
    g->fillCircle(x+h,y+(int)(s*0.2f),(int)(s*0.06f),c);
    g->fillCircle(x+(int)(s*0.2f),y+(int)(s*0.7f),(int)(s*0.06f),c);
    g->fillCircle(x+(int)(s*0.8f),y+(int)(s*0.7f),(int)(s*0.06f),c);
    g->drawLine(x+h,y+(int)(s*0.2f),x+(int)(s*0.2f),y+(int)(s*0.7f),c);
    g->drawLine(x+h,y+(int)(s*0.2f),x+(int)(s*0.8f),y+(int)(s*0.7f),c);
    g->drawLine(x+(int)(s*0.2f),y+(int)(s*0.7f),x+(int)(s*0.8f),y+(int)(s*0.7f),c);
    break;

  case ICO_SD_CARD:
    g->drawLine(x+(int)(s*0.2f),y+(int)(s*0.1f),x+(int)(s*0.6f),y+(int)(s*0.1f),c);
    g->drawLine(x+(int)(s*0.6f),y+(int)(s*0.1f),x+(int)(s*0.8f),y+(int)(s*0.3f),c);
    g->drawLine(x+(int)(s*0.8f),y+(int)(s*0.3f),x+(int)(s*0.8f),y+(int)(s*0.9f),c);
    g->drawLine(x+(int)(s*0.8f),y+(int)(s*0.9f),x+(int)(s*0.2f),y+(int)(s*0.9f),c);
    g->drawLine(x+(int)(s*0.2f),y+(int)(s*0.9f),x+(int)(s*0.2f),y+(int)(s*0.1f),c);
    for(int i=0;i<3;i++)
      g->fillRect(x+(int)(s*0.3f)+i*(int)(s*0.12f),y+(int)(s*0.15f),(int)(s*0.06f),(int)(s*0.2f),c);
    break;

  case ICO_RADIO_TX:
    g->drawLine(x+h,y+(int)(s*0.3f),x+h,y+(int)(s*0.85f),c);
    g->fillRect(x+h-(int)(s*0.1f),y+(int)(s*0.8f),(int)(s*0.2f),(int)(s*0.08f),c);
    g->fillCircle(x+h,y+(int)(s*0.25f),(int)(s*0.05f),GR_C_RED);
    for(int i=1;i<=2;i++)
      grArc(g,x+h,y+(int)(s*0.25f),(int)(s*0.1f)*i,PI*1.2f,PI*1.8f,GR_C_RED);
    break;

  case ICO_RADIO_IDLE:
    g->drawLine(x+h,y+(int)(s*0.3f),x+h,y+(int)(s*0.85f),c);
    g->fillRect(x+h-(int)(s*0.1f),y+(int)(s*0.8f),(int)(s*0.2f),(int)(s*0.08f),c);
    g->fillCircle(x+h,y+(int)(s*0.25f),(int)(s*0.05f),c);
    grArc(g,x+h,y+(int)(s*0.25f),(int)(s*0.12f),PI*1.3f,PI*1.7f,c);
    break;

  case ICO_GATE:
    g->fillRect(x+(int)(s*0.15f),y+(int)(s*0.3f),(int)(s*0.1f),(int)(s*0.6f),c);
    g->fillRect(x+(int)(s*0.15f),y+(int)(s*0.3f),(int)(s*0.7f),(int)(s*0.1f),c);
    for(int i=0;i<3;i++)
      g->fillRect(x+(int)(s*0.35f)+i*(int)(s*0.15f),y+(int)(s*0.3f),(int)(s*0.06f),(int)(s*0.1f),GR_C_RED);
    g->fillCircle(x+(int)(s*0.2f),y+(int)(s*0.35f),(int)(s*0.05f),GR_C_AMB);
    break;

  case ICO_TIRE:
    g->drawCircle(x+h,y+h,(int)(h*0.55f),c);
    g->drawCircle(x+h,y+h,(int)(h*0.35f),c);
    for(int a=0;a<8;a++){
      float an=a*PI/4.0f;
      g->drawLine(x+h+(int)(cosf(an)*h*0.35f),y+h+(int)(sinf(an)*h*0.35f),
                  x+h+(int)(cosf(an)*h*0.55f),y+h+(int)(sinf(an)*h*0.55f),c);
    }
    break;

  case ICO_BELL:
    grArc(g,x+h,y+(int)(h*0.55f),(int)(h*0.4f),PI*1.15f,PI*-0.15f+2*PI,c);
    g->drawLine(x+h-(int)(h*0.5f),y+(int)(h*1.1f),x+h+(int)(h*0.5f),y+(int)(h*1.1f),c);
    g->fillCircle(x+h,y+(int)(h*1.2f),(int)(s*0.06f),c);
    g->fillCircle(x+h,y+(int)(s*0.12f),(int)(s*0.04f),c);
    break;

  case ICO_MEDICAL:
    g->fillRect(x+h-(int)(s*0.08f),y+(int)(s*0.15f),(int)(s*0.16f),(int)(s*0.7f),c);
    g->fillRect(x+(int)(s*0.15f),y+h-(int)(s*0.08f),(int)(s*0.7f),(int)(s*0.16f),c);
    break;

  case ICO_METER:
    grArc(g,x+h,y+(int)(h*1.1f),(int)(h*0.6f),PI,0,c);
    g->drawLine(x+h,y+(int)(h*1.1f),x+h+(int)(h*0.35f),y+(int)(h*0.55f),c);
    for(int a=0;a<5;a++){
      float an=PI+a*PI/4.0f;
      g->drawLine(x+h+(int)(cosf(an)*h*0.5f),y+(int)(h*1.1f)+(int)(sinf(an)*h*0.5f),
                  x+h+(int)(cosf(an)*h*0.6f),y+(int)(h*1.1f)+(int)(sinf(an)*h*0.6f),c);
    }
    g->fillCircle(x+h,y+(int)(h*1.1f),(int)(s*0.05f),c);
    break;

  case ICO_WRENCH:
    g->drawLine(x+(int)(s*0.2f),y+(int)(s*0.15f),x+(int)(s*0.35f),y+(int)(s*0.3f),c);
    g->drawLine(x+(int)(s*0.35f),y+(int)(s*0.3f),x+(int)(s*0.65f),y+(int)(s*0.7f),c);
    g->drawLine(x+(int)(s*0.65f),y+(int)(s*0.7f),x+(int)(s*0.8f),y+(int)(s*0.85f),c);
    g->drawCircle(x+(int)(s*0.2f),y+(int)(s*0.15f),(int)(s*0.1f),c);
    g->drawCircle(x+(int)(s*0.8f),y+(int)(s*0.85f),(int)(s*0.1f),c);
    break;

  case ICO_CAR:
    g->drawLine(x+(int)(s*0.1f),y+h+(int)(s*0.05f),x+(int)(s*0.2f),y+h-(int)(s*0.15f),c);
    g->drawLine(x+(int)(s*0.2f),y+h-(int)(s*0.15f),x+(int)(s*0.35f),y+h-(int)(s*0.25f),c);
    g->drawLine(x+(int)(s*0.35f),y+h-(int)(s*0.25f),x+(int)(s*0.65f),y+h-(int)(s*0.25f),c);
    g->drawLine(x+(int)(s*0.65f),y+h-(int)(s*0.25f),x+(int)(s*0.8f),y+h-(int)(s*0.15f),c);
    g->drawLine(x+(int)(s*0.8f),y+h-(int)(s*0.15f),x+(int)(s*0.9f),y+h+(int)(s*0.05f),c);
    g->drawLine(x+(int)(s*0.9f),y+h+(int)(s*0.05f),x+(int)(s*0.1f),y+h+(int)(s*0.05f),c);
    g->fillCircle(x+(int)(s*0.3f),y+h+(int)(s*0.1f),(int)(s*0.08f),c);
    g->fillCircle(x+(int)(s*0.7f),y+h+(int)(s*0.1f),(int)(s*0.08f),c);
    g->drawLine(x+(int)(s*0.45f),y+h-(int)(s*0.24f),x+(int)(s*0.45f),y+h-(int)(s*0.08f),c);
    break;

  case ICO_BADGE:
    g->drawRect(x+(int)(s*0.15f),y+(int)(s*0.1f),(int)(s*0.7f),(int)(s*0.8f),c);
    g->drawRect(x+(int)(s*0.25f),y+(int)(s*0.2f),(int)(s*0.25f),(int)(s*0.25f),c);
    g->fillRect(x+(int)(s*0.2f),y+(int)(s*0.55f),(int)(s*0.6f),(int)(s*0.08f),c);
    g->fillRect(x+(int)(s*0.55f),y+(int)(s*0.22f),(int)(s*0.2f),(int)(s*0.04f),c);
    g->fillRect(x+(int)(s*0.55f),y+(int)(s*0.32f),(int)(s*0.15f),(int)(s*0.04f),c);
    break;

  case ICO_PAGER:
    g->drawRect(x+(int)(s*0.15f),y+(int)(s*0.15f),(int)(s*0.7f),(int)(s*0.7f),c);
    g->fillRect(x+(int)(s*0.22f),y+(int)(s*0.22f),(int)(s*0.56f),(int)(s*0.3f),c);
    for(int i=0;i<3;i++) g->fillCircle(x+(int)(s*0.3f)+i*(int)(s*0.15f),y+(int)(s*0.7f),(int)(s*0.04f),c);
    g->fillRect(x+(int)(s*0.7f),y+(int)(s*0.08f),(int)(s*0.06f),(int)(s*0.12f),c);
    break;

  case ICO_BABY:
    g->drawCircle(x+h,y+(int)(h*0.7f),(int)(h*0.35f),c);
    g->fillCircle(x+h,y+(int)(h*0.7f),(int)(h*0.15f),c);
    g->drawLine(x+h,y+(int)(h*0.35f),x+h+(int)(h*0.2f),y+(int)(s*0.1f),c);
    g->fillCircle(x+h+(int)(h*0.2f),y+(int)(s*0.1f),(int)(s*0.04f),c);
    g->fillRect(x+h-(int)(s*0.04f),y+(int)(h*1.05f),(int)(s*0.08f),(int)(s*0.15f),c);
    g->fillRect(x+h-(int)(h*0.25f),y+(int)(h*1.2f),(int)(h*0.5f),(int)(s*0.04f),c);
    break;

  case ICO_LEAF:
    grQuad(g,x+h,y+(int)(s*0.85f),x+(int)(s*0.1f),y+(int)(h*0.6f),x+h,y+(int)(s*0.1f),c);
    grQuad(g,x+h,y+(int)(s*0.1f),x+(int)(s*0.9f),y+(int)(h*0.6f),x+h,y+(int)(s*0.85f),c);
    g->drawLine(x+h,y+(int)(s*0.15f),x+h,y+(int)(s*0.8f),c);
    g->drawLine(x+h,y+(int)(h*0.6f),x+h-(int)(h*0.3f),y+(int)(h*0.35f),c);
    g->drawLine(x+h,y+(int)(h*0.8f),x+h+(int)(h*0.3f),y+(int)(h*0.55f),c);
    break;

  case ICO_CONTROLLER:
    g->drawLine(x+(int)(s*0.25f),y+h-(int)(s*0.05f),x+(int)(s*0.35f),y+h-(int)(s*0.15f),c);
    g->drawLine(x+(int)(s*0.35f),y+h-(int)(s*0.15f),x+(int)(s*0.65f),y+h-(int)(s*0.15f),c);
    g->drawLine(x+(int)(s*0.65f),y+h-(int)(s*0.15f),x+(int)(s*0.75f),y+h-(int)(s*0.05f),c);
    grQuad(g,x+(int)(s*0.75f),y+h-(int)(s*0.05f),x+(int)(s*0.9f),y+h+(int)(s*0.15f),x+(int)(s*0.7f),y+h+(int)(s*0.2f),c);
    g->drawLine(x+(int)(s*0.7f),y+h+(int)(s*0.2f),x+(int)(s*0.3f),y+h+(int)(s*0.2f),c);
    grQuad(g,x+(int)(s*0.3f),y+h+(int)(s*0.2f),x+(int)(s*0.1f),y+h+(int)(s*0.15f),x+(int)(s*0.25f),y+h-(int)(s*0.05f),c);
    g->fillRect(x+(int)(s*0.28f),y+h-(int)(s*0.02f),(int)(s*0.04f),(int)(s*0.12f),c);
    g->fillRect(x+(int)(s*0.24f),y+h+(int)(s*0.02f),(int)(s*0.12f),(int)(s*0.04f),c);
    g->fillCircle(x+(int)(s*0.6f),y+h-(int)(s*0.02f),(int)(s*0.03f),c);
    g->fillCircle(x+(int)(s*0.66f),y+h+(int)(s*0.04f),(int)(s*0.03f),c);
    break;

  case ICO_TOWER:
    g->drawLine(x+h,y+(int)(s*0.1f),x+h-(int)(h*0.35f),y+(int)(s*0.85f),c);
    g->drawLine(x+h,y+(int)(s*0.1f),x+h+(int)(h*0.35f),y+(int)(s*0.85f),c);
    g->drawLine(x+h-(int)(h*0.12f),y+(int)(h*0.6f),x+h+(int)(h*0.12f),y+(int)(h*0.6f),c);
    g->drawLine(x+h-(int)(h*0.22f),y+h,x+h+(int)(h*0.22f),y+h,c);
    grArc(g,x+h,y+(int)(s*0.1f),(int)(s*0.12f),PI*1.3f,PI*1.7f,c);
    grArc(g,x+h,y+(int)(s*0.1f),(int)(s*0.2f),PI*1.3f,PI*1.7f,c);
    break;

  case ICO_BOLT:
    g->fillTriangle(x+h+(int)(s*0.05f),y+(int)(s*0.1f),
                    x+h-(int)(s*0.12f),y+h,
                    x+h+(int)(s*0.05f),y+h, c);
    g->fillTriangle(x+h+(int)(s*0.05f),y+h,
                    x+h-(int)(s*0.1f),y+(int)(s*0.9f),
                    x+h+(int)(s*0.15f),y+(int)(h*0.75f), c);
    break;

  case ICO_QUESTION:
    grArc(g,x+h,y+(int)(h*0.55f),(int)(h*0.3f),PI*1.2f,PI*0.3f+2*PI,c);
    g->drawLine(x+h+(int)(h*0.15f),y+(int)(h*0.75f),x+h,y+(int)(h*1.05f),c);
    g->fillCircle(x+h,y+(int)(h*1.3f),(int)(s*0.06f),c);
    break;

  case ICO_HOURGLASS:
    g->drawTriangle(x+(int)(s*0.2f),y+(int)(s*0.15f),x+(int)(s*0.8f),y+(int)(s*0.15f),x+h,y+h,c);
    g->drawTriangle(x+h,y+h,x+(int)(s*0.8f),y+(int)(s*0.85f),x+(int)(s*0.2f),y+(int)(s*0.85f),c);
    g->fillRect(x+h-(int)(s*0.06f),y+h,(int)(s*0.12f),(int)(s*0.2f),c);
    break;

  case ICO_BOOK:
    grQuad(g,x+h,y+(int)(s*0.2f),x+(int)(s*0.1f),y+(int)(s*0.2f),x+(int)(s*0.1f),y+(int)(s*0.3f),c);
    g->drawLine(x+(int)(s*0.1f),y+(int)(s*0.3f),x+(int)(s*0.1f),y+(int)(s*0.8f),c);
    grQuad(g,x+(int)(s*0.1f),y+(int)(s*0.8f),x+h,y+(int)(s*0.75f),x+h,y+(int)(s*0.85f),c);
    grQuad(g,x+h,y+(int)(s*0.2f),x+(int)(s*0.9f),y+(int)(s*0.2f),x+(int)(s*0.9f),y+(int)(s*0.3f),c);
    g->drawLine(x+(int)(s*0.9f),y+(int)(s*0.3f),x+(int)(s*0.9f),y+(int)(s*0.8f),c);
    grQuad(g,x+(int)(s*0.9f),y+(int)(s*0.8f),x+h,y+(int)(s*0.75f),x+h,y+(int)(s*0.85f),c);
    g->drawLine(x+h,y+(int)(s*0.2f),x+h,y+(int)(s*0.85f),c);
    break;

  case ICO_FLAME:
    grQuad(g,x+h,y+(int)(s*0.1f),x+(int)(s*0.75f),y+(int)(h*0.6f),x+(int)(s*0.7f),y+(int)(h*1.1f),c);
    grQuad(g,x+(int)(s*0.7f),y+(int)(h*1.1f),x+h,y+(int)(s*0.95f),x+(int)(s*0.3f),y+(int)(h*1.1f),c);
    grQuad(g,x+(int)(s*0.3f),y+(int)(h*1.1f),x+(int)(s*0.25f),y+(int)(h*0.6f),x+h,y+(int)(s*0.1f),c);
    g->fillCircle(x+h,y+(int)(h*0.9f),(int)(h*0.15f),0xFFE0);
    break;

  case ICO_CHAIN:
    grEllipse(g,x+h-(int)(s*0.08f),y+h,(int)(s*0.15f),(int)(s*0.25f),c);
    grEllipse(g,x+h+(int)(s*0.08f),y+h,(int)(s*0.15f),(int)(s*0.25f),c);
    break;

  case ICO_WIFI:
    g->fillCircle(x+h,y+(int)(s*0.75f),(int)(s*0.05f),c);
    for(int i=1;i<=3;i++)
      grArc(g,x+h,y+(int)(s*0.75f),(int)(s*0.12f)*i,PI*1.2f,PI*1.8f,c);
    break;

  default: break;
  }
}

// device type → icon enum mapping (for capture ticker / folder list)
static uint8_t grDevToIco(uint8_t dt) {
  switch (dt) {
    case 1: case 2: return ICO_GARAGE;
    case 3:  return ICO_GATE;
    case 4:  return ICO_CAR_KEY;
    case 5:  return ICO_TIRE;
    case 6:  return ICO_BELL;
    case 7:  return ICO_WEATHER;
    case 8: case 9: return ICO_SIREN;
    case 10: return ICO_SMART_HOME;
    case 11: return ICO_METER;
    case 12: return ICO_WRENCH;
    case 13: return ICO_MEDICAL;
    case 14: return ICO_CAR;
    case 15: return ICO_BADGE;
    case 16: return ICO_PAGER;
    case 17: return ICO_BABY;
    case 18: return ICO_LEAF;
    case 19: return ICO_CONTROLLER;
    case 20: return ICO_TOWER;
    default: return ICO_QUESTION;
  }
}

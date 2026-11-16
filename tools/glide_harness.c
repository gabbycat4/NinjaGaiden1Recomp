/* glide_harness.c - runs src/ng1_extras.c's smooth motion on a recorded piece
 * (no window) and measures what the 240 Hz pictures would show.
 *   gcc -std=c11 -O1 -w -I nesrecomp/runner/cyc -I recomp-ui/src -I src tools/glide_harness.c -o ~/gh -lm
 *   ~/gh SEG.oam SEG.scroll [lag 0/1] [v]
 * SEG.* from tools/segment_logs.py (build-play/diagnostics/measure/).
 * Prints, for the item tiles $ED-$FB (they stand still in the world): how far
 * they wobble against the scenery between pictures (reversals = back-and-forth
 * moves of more than 0.1 px). With "v": one line per picture with the
 * scenery's position and step (starts and stops). CHR is stubbed solid. */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static uint8_t H_oam[256]; static int H_x, H_y, H_pf;   /* H_pf: playfield band present */
#include "cyc_host_extras.h"
#include "cyc_core.h"
#include "cyc_render.h"
const uint8_t *cyc_render_oam(void){return H_oam;}
uint8_t cyc_render_line_mask(int y){(void)y;return 0x1e;}
bool cyc_render_line_sprite16(int y){(void)y;return true;}
uint16_t cyc_render_line_sprite_table(int y){(void)y;return 0;}
uint16_t cyc_render_line_bg_table(int y){(void)y;return 0x1000;}
/* HUD: lines 0-55 at (0,8); playfield from line 56 at (H_x,H_y) */
int cyc_render_line_scroll_x(int y){return y<56?0:H_x;}
int cyc_render_line_scroll_y(int y){return y<56?8+y:(H_y+(y-56))%480;}
uint8_t cyc_render_chr(uint16_t a){(void)a;return 0xFF;}
uint32_t cyc_render_color(int i){return (uint32_t)i;}
uint8_t cyc_render_palette(int e){(void)e;return 0;}
void cyc_render_capture_background(bool e){(void)e;}
bool cyc_render_background(uint32_t *o){(void)o;return false;}
const uint32_t *cyc_render_present(int *w,int *h){*w=*h=0;return NULL;}
const uint8_t *cyc_frame_bg_opaque(void){static uint8_t z[256*240];return z;}
#include "ng1_extras.c"

typedef struct { int key; long wc, wp; double drawn; bool have, seen, still; } Track;
typedef struct { int key; double last, step; bool hs; long f; } RTrack;
static RTrack ry[64]; static int nry; static long rpics, rbad; static double rsum;
int main(int argc,char**argv){
  if(argc<3){fprintf(stderr,"usage: gh OAM SCROLL [lag] [v]\n");return 2;}
  FILE*fo=fopen(argv[1],"r"),*fs=fopen(argv[2],"r"); if(!fo||!fs){perror("open");return 1;}
  s_lag_enable = argc>3 ? atoi(argv[3]) : 1;
  if(getenv("GH_EASE")) s_ease = atoi(getenv("GH_EASE"));
  int verbose = argc>4;
  char lo[4096],ls[1024];
  s_present_rate=240; s_glide=1; s_shots=1;
  Track tr[512]; int ntr=0; long ctot=0, frames=0, still=0, pics=0, shaky=0; double wob=0;
  double prev_bg=0; bool have_bg=false;
  while(fgets(lo,sizeof lo,fo)){
    if(lo[0]=='#')continue;
    long f; char*p=lo; f=strtol(p,&p,10);
    for(int i=0;i<64;i++){unsigned v=(unsigned)strtoul(p,&p,16);H_oam[i*4]=v>>24;H_oam[i*4+1]=(v>>16)&255;H_oam[i*4+2]=(v>>8)&255;H_oam[i*4+3]=v&255;}
    long sf=-1; int hud=0;
    while(sf<f && fgets(ls,sizeof ls,fs)){
      if(ls[0]=='#')continue;
      char b0[64]="",b1[64]=""; int nb=0;
      if(sscanf(ls,"%ld %*d %*d %d %63s %63s",&sf,&nb,b0,b1)>=3){
        hud = strcmp(b0,"0:0,8")==0;
        int x,y;
        if(hud && sscanf(b1,"56:%d,%d",&x,&y)==2){H_x=x;H_y=y;}
        else if(hud){H_x=0;H_y=64;}
      }
    }
    if(!hud){ smooth_reset(); have_bg=false; ntr=0; continue; }
    bands_frame(); glide_frame(s_nband>0, s_cam_dx, s_cam_dy); memcpy(s_snap_oam,H_oam,256);
    ctot += s_cam_dx; frames++;
    if(getenv("GH_FRAME") && (f==atol(getenv("GH_FRAME"))||f==atol(getenv("GH_FRAME"))-1)){
      for(int sl=0;sl<64;sl++){const uint8_t*o=H_oam+sl*4; if(o[0]>=0xEF)continue;
        printf("  dump %ld slot%2d y%3d t%02x a%02x x%3d ok%d lag%d sc%d spd%.2f gdx%d\n",f,sl,o[0],o[1],o[2],o[3],s_gl_ok[sl],s_glag[sl].lag,s_glag[sl].score,s_gm[sl].valid?motion_speed(&s_gm[sl]):99,s_gdx[sl]);}
    }
    for(int i=0;i<ntr;i++) tr[i].seen=false;
    for(int ph=0; ph<4; ++ph){
      double phase=ph/4.0, bg = ctot - band_offset(s_cam_band,phase);
      if(verbose) printf("F %ld.%d cam %+d bg %.2f step %+.2f\n",f,ph,s_cam_dx,bg,have_bg?bg-prev_bg:0);
      prev_bg=bg; have_bg=true;
      for(int sl=1;sl<64;sl++){const uint8_t*o=H_oam+sl*4;
        if(o[0]>=0xEF||o[1]<0xED||o[1]>0xFB||(o[2]&3))continue;
        int key=(o[1]<<8)|o[2], dup=0;
        for(int j=1;j<64;j++) if(j!=sl&&H_oam[j*4]<0xEF&&H_oam[j*4+1]==o[1]&&H_oam[j*4+2]==o[2]) dup=1;
        if(dup)continue;
        double gx,gy; sprite_glide_offset(sl,phase,&gx,&gy);
        double w=o[3]+gx+bg;
        if(getenv("GH_ITEM") && o[2]==0 && o[3]<128) printf("  %ld.%d t%02x x%3d cam%+d gx%+.2f world %.2f ok%d lag%d sc%d mv%d spd%.2f\n",f,ph,o[1],o[3],s_cam_dx,gx,w,s_gl_ok[sl],s_glag[sl].lag,s_glag[sl].score,s_gm[sl].valid,s_gm[sl].valid?motion_speed(&s_gm[sl]):0);
        long wc=o[3]+ctot, wp=o[3]+ctot-s_cam_dx;
        int t=-1; for(int i=0;i<ntr;i++) if(tr[i].key==key){t=i;break;}
        if(t<0){ if(ntr<512){tr[ntr]=(Track){key,wc,wp,w,true,true,false};ntr++;} continue; }
        if(ph==0){
          bool st = tr[t].have && (wc==tr[t].wc || wp==tr[t].wp);
          tr[t].have = st || true;
          tr[t].still=st;
          if(!st){ tr[t].wc=wc; tr[t].wp=wp; tr[t].drawn=w; tr[t].seen=true; continue; }
          still++;
          tr[t].wc=wc; tr[t].wp=wp;
        }
        if(!tr[t].still){ tr[t].drawn=w; tr[t].seen=true; continue; }
        double d=fabs(w-tr[t].drawn); pics++; wob+=d; if(d>0.3) shaky++;
        tr[t].drawn=w; tr[t].seen=true;
      }
    }
    /* Ryu: palette 0, not item tiles, lines 80-200; per piece key: drawn screen X per picture */
    for(int ph=0; ph<4; ++ph){
      double phase=ph/4.0;
      for(int sl=1;sl<64;sl++){const uint8_t*o=H_oam+sl*4;
        if(o[0]>=0xEF||(o[2]&3)||(o[1]>=0xED&&o[1]<=0xFB)||o[0]<80||o[0]>200)continue;
        int key=(o[1]<<8)|o[2], dup=0;
        for(int j=1;j<64;j++) if(j!=sl&&H_oam[j*4]<0xEF&&H_oam[j*4+1]==o[1]&&H_oam[j*4+2]==o[2]) dup=1;
        if(dup)continue;
        double gx,gy; sprite_glide_offset(sl,phase,&gx,&gy);
        double sx=o[3]+gx;
        int t=-1; for(int i=0;i<nry;i++) if(ry[i].key==key){t=i;break;}
        if(t<0){ if(nry<64){ry[nry]=(RTrack){key,sx,0,false,f};nry++;} continue; }
        if(ry[t].f < f-1){ ry[t]=(RTrack){key,sx,0,false,f}; continue; }
        double st=sx-ry[t].last;
        if(fabs(st)>4){ ry[t]=(RTrack){key,sx,0,false,f}; continue; }
        if(ry[t].hs && s_cam_dx!=0){ double ch=fabs(st-ry[t].step); rpics++; rsum+=ch; if(ch>0.25){ rbad++; if(getenv("GH_RYU")) printf("  R %ld.%d t%02x a%02x x%3d gx%+.2f step%+.2f prev%+.2f lag%d lead%d spd%.2f cam%+d\n",f,ph,o[1],o[2],o[3],gx,st,ry[t].step,s_glag[sl].lag,s_lagshift[sl],s_gm[sl].valid?motion_speed(&s_gm[sl]):99,s_cam_dx);} }
        ry[t].last=sx; ry[t].step=st; ry[t].hs=true; ry[t].f=f;
      }
    }
    /* drop items not seen this frame */
    int k=0; for(int i=0;i<ntr;i++) if(tr[i].seen) tr[k++]=tr[i]; ntr=k;
  }
  printf("ryu: pictures %ld, uneven (step change > 0.25 px) %ld, mean step change %.3f px\n", rpics, rbad, rpics? rsum/rpics:0);
  printf("lag %d: frames %ld, still item-frames %ld, pictures %ld, moved >0.3 px vs scenery: %ld, total drift %.1f px\n",
         s_lag_enable, frames, still, pics, shaky, wob);
  return 0;
}

/* glide_harness.c - runs src/ng1_extras.c's glide on a recorded piece (no window):
 *   gcc -std=c11 -O1 -w -I nesrecomp/runner/cyc -I src tools/glide_harness.c -o /tmp/gh -lm
 *   /tmp/gh build-play/diagnostics/measure/segment-000032.oam build-play/diagnostics/measure/segment-000032.scroll
 * One line per frame: S = steady shots drawn (x,y,tile,palette,speed), O = shots in OAM
 * (x,y,tile,palette,gliding,speed). CHR is not in the logs: tiles 0e-11 (the shots) are
 * stubbed as small, everything else as solid. */
/* feeds recorded OAM + scroll into ng1_extras.c's glide (no window) */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t H_oam[256]; static int H_x, H_y;
#define STUBS
#include "cyc_host_extras.h"
#include "cyc_core.h"
#include "cyc_render.h"
const uint8_t *cyc_render_oam(void){return H_oam;}
uint8_t cyc_render_line_mask(int y){(void)y;return 0x18;}
bool cyc_render_line_sprite16(int y){(void)y;return true;}
uint16_t cyc_render_line_sprite_table(int y){(void)y;return 0;}
uint16_t cyc_render_line_bg_table(int y){(void)y;return 0x1000;}
int cyc_render_line_scroll_x(int y){(void)y;return H_x;}
int cyc_render_line_scroll_y(int y){int b=H_y-H_y%240, r=H_y%240+y; return b+(r%240);}
/* CHR stub: tiles 0e/0f/10/11 (the shots) have 8 pixels, every other tile 64 */
uint8_t cyc_render_chr(uint16_t a){int tile=(a>>4)&0xFF; int plane=(a>>3)&1; if(plane)return 0;
  return (tile==0x0e||tile==0x0f||tile==0x10||tile==0x11)?0x10:0xFF;}
uint32_t cyc_render_color(int i){return (uint32_t)i;}
uint8_t cyc_render_palette(int e){(void)e;return 0;}
void cyc_render_capture_background(bool e){(void)e;}
bool cyc_render_background(uint32_t *o){(void)o;return false;}
const uint32_t *cyc_render_present(int *w,int *h){*w=*h=0;return NULL;}
const uint8_t *cyc_frame_bg_opaque(void){static uint8_t z[256*240];return z;}
#include "ng1_extras.c"
int main(int argc,char**argv){
  /* argv: oam log, scroll log */
  FILE*fo=fopen(argv[1],"r"),*fs=fopen(argv[2],"r"); char lo[4096],ls[512];
  s_present_rate=240; s_glide=1; s_shots=1;
  while(fgets(lo,sizeof lo,fo)){
    long f; char*p=lo; f=strtol(p,&p,10);
    for(int i=0;i<64;i++){unsigned v=(unsigned)strtoul(p,&p,16);H_oam[i*4]=v>>24;H_oam[i*4+1]=(v>>16)&255;H_oam[i*4+2]=(v>>8)&255;H_oam[i*4+3]=v&255;}
    long sf=-1; int x=0,y=0;
    while(sf<f && fgets(ls,sizeof ls,fs)){ if(ls[0]=='#')continue; char b[64]; if(sscanf(ls,"%ld %*d %*d %*d %63s",&sf,b)==2){ if(sscanf(b,"%*d:%d,%d",&x,&y)!=2){x=0;y=0;} } }
    H_x=x;H_y=y;
    bands_frame(); glide_frame(s_nband>0, s_cam_dx, s_cam_dy);
    printf("F %ld cam %d",f,s_cam_dx);
    for(int i=0;i<s_nshot;i++) printf(" S%d,%d,%02x,%x,%.2f",s_shot[i].x,s_shot[i].y,s_shot[i].tile,s_shot[i].attr&3,motion_speed(&s_shot[i].m));
    for(int sl=0;sl<64;sl++){const uint8_t*o=H_oam+sl*4; if(o[0]<0xEF&&(o[1]==0x0e||o[1]==0x10)) printf(" O%d,%d,%02x,%x,%d,%.2f",o[3],o[0],o[1],o[2]&3,s_gl_ok[sl],s_gm[sl].valid?motion_speed(&s_gm[sl]):99);}
    printf("\n");
  }
  return 0;
}

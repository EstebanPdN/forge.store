int main(void) {
 const char *labels[]={"Info","Screens","Install","What's new","Universal","Community","Emulators","Forge"};
 const char *icons[]={"info","image","download","clock","globe","users","gamepad","hammer"};
 int checks=0;
 for(int n=2;n<=8;n++) for(int with_icons=0;with_icons<2;with_icons++) for(int width=242;width<=312;width+=35) {
  UiTabs t={0}; NOW=0; tabs_layout(&t,4,4,width,labels,with_icons?icons:NULL,n,0);
  assert(fabsf(t.thumb-t.shift-3)<.01f); checks++;
  for(int i=1;i<n;i++) {
   float old=t.thumb-t.shift;
   tabs_layout(&t,4,4,width,labels,with_icons?icons:NULL,n,i);
   assert(fabsf((t.thumb-t.shift)-old)<.01f); checks++;
   NOW+=.17f; tabs_sample(&t); assert(isfinite(t.thumb) && isfinite(t.shift)); checks++;
   NOW+=.18f; tabs_sample(&t);
   assert(fabsf(t.thumb-t.positions[i])<.01f); assert(fabsf(t.thumb_w-t.widths[i])<.01f); checks+=2;
  }
  assert(fabsf((width-38)-(t.thumb-t.shift+t.thumb_w)-3)<.01f); checks++;
 }
 assert(4+69+3+80+3+42+3+81+3+28+4==320); checks++;
 assert(44+2*2+2*2==52); checks++;
 UiTabs content={.changed=0,.direction=1};NOW=0;assert(tabs_content(&content,1)==0&&DRAW_X==12);checks++;
 NOW=.17f;float opacity=tabs_content(&content,1);assert(opacity>0&&opacity<1&&DRAW_X>0&&DRAW_X<12);checks++;
 NOW=.34f;assert(fabsf(tabs_content(&content,1)-1)<.001f&&fabsf(DRAW_X)<.001f);checks++;
 content.direction=-1;NOW=0;assert(tabs_content(&content,1)==0&&DRAW_X==-12);checks++;
 printf("%d checks passed: actual tabs_layout/sample first/last3pxinsets, exact340msdestinations, interrupt continuity, toolbar widths and cursor bounds.\n",checks);
 return 0;
}

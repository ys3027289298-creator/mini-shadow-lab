#include <string.h>
#define MAXC 4
typedef struct { float splits[MAXC]; int n; } Cascades;
static int build(Cascades* c,int n){
  memset(c,0,sizeof *c);
  if(n<=0||n>MAXC) return -1;
  for(int i=1;i<=n;i++) c->splits[i-1]=(float)i;
  c->n=n; return 0; }

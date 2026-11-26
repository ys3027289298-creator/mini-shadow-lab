#include <stdio.h>
static int in_shadow(float fd,float md,float b){ return fd-b>md?1:0; }
int main(void){ float clear=0.49000000953674316; int sh=0;
    for(int i=0;i<1000;i++) sh+=in_shadow(0.5f,clear,0.0f);
    int black=(sh==1000);
    printf("black=%d shadowed=%d\n",black,sh); return 0; }

typedef struct { float m[16]; } Mat4;
static void mat_ortho(Mat4* m,float l,float r,float n,float f){
  if(f==n||l==r) return;
  m->m[0]=2.0f/(r-l); m->m[10]=-1.0f/(f-n); }

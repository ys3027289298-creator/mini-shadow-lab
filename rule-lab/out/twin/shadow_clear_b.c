static int in_shadow(float frag_depth,float map_depth,float bias){
  return frag_depth-bias>map_depth?1:0; }

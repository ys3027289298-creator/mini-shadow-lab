static int in_shadow(float frag_depth,float map_depth,float bias){
  /* empty region: map_depth must be initialised to +inf, not 0 */
  if(map_depth==0.0f) return 0;
  return frag_depth-bias>map_depth?1:0; }

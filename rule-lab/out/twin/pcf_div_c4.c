static float penumbra_texels(float blocker_depth,float receiver_depth,float light_size,
                             float texel_world,float projection_scale){
  if(texel_world<=0.0f||projection_scale<=0.0f||receiver_depth==0.0f) return -1.0f;
  float d=receiver_depth-blocker_depth; if(d<0.0f)d=0.0f;
  return (light_size*d/receiver_depth)*projection_scale/texel_world; }

__kernel void clgl_pick_selection(float4 O, float4 D, 
   __global float* vbo, __global ushort* ibo,
   __global float* t_glob, __local float* t_loc) {

  float3 E, F, G, K, L, M;
  float4 out1;
  float t_test, t, k, l;
  ushort3 indices;
  uint i;

  t_loc[get_local_id(0)] = 10000.0f;

  if(get_global_id(0) < 528) {

    /* Read coordinates of triangle vertices */
    indices = vload3(get_global_id(0), ibo);
    K = vload3(indices.x, vbo);
    L = vload3(indices.y, vbo);
    M = vload3(indices.z, vbo);

    /* Compute vectors */
    E = K - M;
    F = L - M;

    /* Compute and test determinant */
    t_test = dot(cross(D.s012, F), E);
    if(t_test > 0.0001f) {

      /* Compute and test k */
      G = O.s012 - M;
      k = dot(cross(D.s012, F), G);
      if(k > 0.0f && k <= t_test) {

        /* Compute and test l */
        l = dot(cross(G, E), D.s012);
        if(l > 0.0f && k + l <= t_test) {

          /* Compute distance from ray to triangle */
          t_loc[get_local_id(0)] = dot(cross(G, E), F)/t_test;
        }
      }
    }
  }
  barrier(CLK_LOCAL_MEM_FENCE);

  /* Cycle through values to find smallest t */
  if(get_local_id(0) == 0) {

    t_test = 1000.0f;
    for(i=0; i<get_local_size(0); i++) {
      if(t_loc[i] > 0.0001f && t_loc[i] < t_test) {
        t_test = t_loc[i];
      }
    }
    t_glob[get_group_id(0)] = t_test;
  }
}

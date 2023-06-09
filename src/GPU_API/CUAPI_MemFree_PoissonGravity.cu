#include "CUAPI.h"

#if ( defined GPU  &&  defined GRAVITY )



extern real (*d_Rho_Array_P    )[ CUBE(RHO_NXT) ];
extern real (*d_Pot_Array_P_In )[ CUBE(POT_NXT) ];
extern real (*d_Pot_Array_P_Out)[ CUBE(GRA_NXT) ];
#ifdef UNSPLIT_GRAVITY
extern real (*d_Pot_Array_USG_G)[ CUBE(USG_NXT_G) ];
extern real (*d_Flu_Array_USG_G)[GRA_NIN-1][ CUBE(PS1) ];
#endif
extern real (*d_Flu_Array_G    )[GRA_NIN  ][ CUBE(PS1) ];
extern double (*d_Corner_Array_PGT)[3];
#ifdef DUAL_ENERGY
extern char (*d_DE_Array_G     )[ CUBE(PS1) ];
#endif
#ifdef MHD
extern real (*d_Emag_Array_G   )[ CUBE(PS1) ];
#endif
extern real (*d_Pot_Array_T    )[ CUBE(GRA_NXT) ];
extern real  *d_ExtPotTable;
extern void **d_ExtPotGenePtr;




//-------------------------------------------------------------------------------------------------------
// Function    :  CUAPI_MemFree_PoissonGravity
// Description :  Free the device and host memory previously allocated by CUAPI_MemAllocate_PoissonGravity()
//
// Parameter   :  None
//-------------------------------------------------------------------------------------------------------
void CUAPI_MemFree_PoissonGravity()
{

// free the device memory
   if ( d_Rho_Array_P      != NULL ) {  CUDA_CHECK_ERROR(  cudaFree( d_Rho_Array_P      )  );  d_Rho_Array_P      = NULL; }
   if ( d_Pot_Array_P_In   != NULL ) {  CUDA_CHECK_ERROR(  cudaFree( d_Pot_Array_P_In   )  );  d_Pot_Array_P_In   = NULL; }
   if ( d_Pot_Array_P_Out  != NULL ) {  CUDA_CHECK_ERROR(  cudaFree( d_Pot_Array_P_Out  )  );  d_Pot_Array_P_Out  = NULL; }
#  ifdef UNSPLIT_GRAVITY
   if ( d_Pot_Array_USG_G  != NULL ) {  CUDA_CHECK_ERROR(  cudaFree( d_Pot_Array_USG_G  )  );  d_Pot_Array_USG_G  = NULL; }
   if ( d_Flu_Array_USG_G  != NULL ) {  CUDA_CHECK_ERROR(  cudaFree( d_Flu_Array_USG_G  )  );  d_Flu_Array_USG_G  = NULL; }
#  endif
   if ( d_Flu_Array_G      != NULL ) {  CUDA_CHECK_ERROR(  cudaFree( d_Flu_Array_G      )  );  d_Flu_Array_G      = NULL; }
   if ( d_Corner_Array_PGT != NULL ) {  CUDA_CHECK_ERROR(  cudaFree( d_Corner_Array_PGT )  );  d_Corner_Array_PGT = NULL; }
#  ifdef DUAL_ENERGY
   if ( d_DE_Array_G       != NULL ) {  CUDA_CHECK_ERROR(  cudaFree( d_DE_Array_G       )  );  d_DE_Array_G       = NULL; }
#  endif
#  ifdef MHD
   if ( d_Emag_Array_G     != NULL ) {  CUDA_CHECK_ERROR(  cudaFree( d_Emag_Array_G     )  );  d_Emag_Array_G     = NULL; }
#  endif
   if ( d_Pot_Array_T      != NULL ) {  CUDA_CHECK_ERROR(  cudaFree( d_Pot_Array_T      )  );  d_Pot_Array_T      = NULL; }
   if ( d_ExtPotTable      != NULL ) {  CUDA_CHECK_ERROR(  cudaFree( d_ExtPotTable      )  );  d_ExtPotTable      = NULL; }
   if ( d_ExtPotGenePtr    != NULL ) {  CUDA_CHECK_ERROR(  cudaFree( d_ExtPotGenePtr    )  );  d_ExtPotGenePtr    = NULL; }


// free the host memory allocated by CUDA
   for (int t=0; t<2; t++)
   {
      if ( h_Rho_Array_P     [t] != NULL ) {  CUDA_CHECK_ERROR(  cudaFreeHost( h_Rho_Array_P     [t] )  );  h_Rho_Array_P     [t] = NULL; }
      if ( h_Pot_Array_P_In  [t] != NULL ) {  CUDA_CHECK_ERROR(  cudaFreeHost( h_Pot_Array_P_In  [t] )  );  h_Pot_Array_P_In  [t] = NULL; }
      if ( h_Pot_Array_P_Out [t] != NULL ) {  CUDA_CHECK_ERROR(  cudaFreeHost( h_Pot_Array_P_Out [t] )  );  h_Pot_Array_P_Out [t] = NULL; }
#     ifdef UNSPLIT_GRAVITY
      if ( h_Pot_Array_USG_G [t] != NULL ) {  CUDA_CHECK_ERROR(  cudaFreeHost( h_Pot_Array_USG_G [t] )  );  h_Pot_Array_USG_G [t] = NULL; }
      if ( h_Flu_Array_USG_G [t] != NULL ) {  CUDA_CHECK_ERROR(  cudaFreeHost( h_Flu_Array_USG_G [t] )  );  h_Flu_Array_USG_G [t] = NULL; }
#     endif
      if ( h_Flu_Array_G     [t] != NULL ) {  CUDA_CHECK_ERROR(  cudaFreeHost( h_Flu_Array_G     [t] )  );  h_Flu_Array_G     [t] = NULL; }
      if ( h_Corner_Array_PGT[t] != NULL ) {  CUDA_CHECK_ERROR(  cudaFreeHost( h_Corner_Array_PGT[t] )  );  h_Corner_Array_PGT[t] = NULL; }
#     ifdef DUAL_ENERGY
      if ( h_DE_Array_G      [t] != NULL ) {  CUDA_CHECK_ERROR(  cudaFreeHost( h_DE_Array_G      [t] )  );  h_DE_Array_G      [t] = NULL; }
#     endif
#     ifdef MHD
      if ( h_Emag_Array_G    [t] != NULL ) {  CUDA_CHECK_ERROR(  cudaFreeHost( h_Emag_Array_G    [t] )  );  h_Emag_Array_G    [t] = NULL; }
#     endif
      if ( h_Pot_Array_T     [t] != NULL ) {  CUDA_CHECK_ERROR(  cudaFreeHost( h_Pot_Array_T     [t] )  );  h_Pot_Array_T     [t] = NULL; }
   } // for (int t=0; t<2; t++)

      if ( h_ExtPotTable         != NULL ) {  CUDA_CHECK_ERROR(  cudaFreeHost( h_ExtPotTable         )  );  h_ExtPotTable         = NULL; }
      if ( h_ExtPotGenePtr       != NULL ) {  CUDA_CHECK_ERROR(  cudaFreeHost( h_ExtPotGenePtr       )  );  h_ExtPotGenePtr       = NULL; }

} // FUNCTION : CUAPI_MemFree_PoissonGravity



#endif // #if ( defined GPU  &&  defined GRAVITY )

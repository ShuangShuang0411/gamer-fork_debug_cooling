#include "CUFLU.h"
#include "Global.h"

#if ( MODEL == HYDRO )


// external functions and GPU-related set-up
#ifdef __CUDACC__

#include "CUDA_CheckError.h"
#include "CUFLU_Shared_FluUtility.cu"
#include "CUDA_ConstMemory.h"

extern real *d_SrcEC_TEF_lambda;
extern real *d_SrcEC_TEF_alpha;
extern real *d_SrcEC_TEFc;

#endif // #ifdef __CUDACC__


// local function prototypes
#ifndef __CUDACC__

void Src_SetAuxArray_ExactCooling( double [], int [] );
void Src_SetConstMemory_ExactCooling( const double AuxArray_Flt[], const int AuxArray_Int[],
                                      double *&DevPtr_Flt, int *&DevPtr_Int );
void Src_PassData2GPU_ExactCooling();
void Src_SetCPUFunc_ExactCooling( SrcFunc_t & );
#ifdef GPU
void Src_SetGPUFunc_ExactCooling( SrcFunc_t & );
#endif
void Src_WorkBeforeMajorFunc_ExactCooling( const int lv, const double TimeNew, const double TimeOld, const double dt,
                                           double AuxArray_Flt[], int AuxArray_Int[] );
void Src_End_ExactCooling();

void Cool_fct( double Dens, double Temp, double Emis, double Lambdat, double Z, double cl_moli_mole, double mp );
#endif
GPU_DEVICE static
double TEF( double TEMP, int k, const real TEF_lambda[], const real TEF_alpha[], const real TEFc[],
            const double AuxArray_Flt[], const int AuxArray_Int[] );
GPU_DEVICE static
double TEFinv( double Y, int k, const real TEF_lambda[], const real TEF_alpha[], const real TEFc[],
               const double AuxArray_Flt[], const int AuxArray_Int[] );


/********************************************************
1. Template of a user-defined source term
   --> Enabled by the runtime option "SRC_USER"

2. This file is shared by both CPU and GPU

   CUSRC_Src_ExactCooling.cu -> CPU_Src_ExactCooling.cpp

3. Four steps are required to implement a source term

   I.   Set auxiliary arrays
   II.  Implement the source-term function
   III. [Optional] Add the work to be done every time
        before calling the major source-term function
   IV.  Set initialization functions

4. The source-term function must be thread-safe and
   not use any global variable
********************************************************/



// =======================
// I. Set auxiliary arrays
// =======================

//-------------------------------------------------------------------------------------------------------
// Function    :  Src_SetAuxArray_ExactCooling
// Description :  Set the auxiliary arrays AuxArray_Flt/Int[]
//
// Note        :  1. Invoked by Src_Init_ExactCooling()
//                2. AuxArray_Flt/Int[] have the size of SRC_NAUX_USER defined in Macro.h (default = 10)
//                3. Add "#ifndef __CUDACC__" since this routine is only useful on CPU
//
// Parameter   :  AuxArray_Flt/Int : Floating-point/Integer arrays to be filled up
//
// Return      :  AuxArray_Flt/Int[]
//-------------------------------------------------------------------------------------------------------
#ifndef __CUDACC__
void Src_SetAuxArray_ExactCooling( double AuxArray_Flt[], int AuxArray_Int[] )
{

   const int    TEF_N      = SrcTerms.EC_TEF_N;   // number of points for lambda(T) sampling in LOG
   const int    TEF_int    = TEF_N-1;   // number of intervals
   const double TEF_TN     = 1.e14;   // == Tref, high enough, but affects sampling resolution
   const double TEF_Tmin   = MIN_TEMP;   // MIN temperature 
   const double TEF_dltemp = (log10(TEF_TN) - log10(TEF_Tmin))/TEF_int;   // sampling resolution (Kelvin), LOG!

   const double cl_Z         = 0.3;    // metallicity (in Zsun)
   const double cl_moli_mole = 1.464;  // Assume the molecular weights are constant, mu_e*mu_i = 1.464
   const double cl_mol       = 0.61;   // mean (total) molecular weights 

// Store them in the aux array 
   AuxArray_Flt[0] = 1.0/(GAMMA-1.0);
   AuxArray_Flt[1] = TEF_TN;
   AuxArray_Flt[2] = TEF_Tmin;
   AuxArray_Flt[3] = TEF_dltemp;
   AuxArray_Flt[4] = cl_Z;
   AuxArray_Flt[5] = cl_moli_mole;
   AuxArray_Flt[6] = cl_mol;
   AuxArray_Flt[7] = Const_mp;
   AuxArray_Flt[8] = Const_kB;

   AuxArray_Int[0] = TEF_N;
   

} // FUNCTION : Src_SetAuxArray_ExactCooling
#endif // #ifndef __CUDACC__


// ======================================
// II. Implement the source-term function
// ======================================

//-------------------------------------------------------------------------------------------------------
// Function    :  Src_ExactCooling
// Description :  Major source-term function
//
// Note        :  1. Invoked by CPU/GPU_SrcSolver_IterateAllCells()
//                2. See Src_SetAuxArray_ExactCooling() for the values stored in AuxArray_Flt/Int[]
//                3. Shared by both CPU and GPU
//
// Parameter   :  fluid             : Fluid array storing both the input and updated values
//                                    --> Including both active and passive variables
//                B                 : Cell-centered magnetic field
//                SrcTerms          : Structure storing all source-term variables
//                dt                : Time interval to advance solution
//                dh                : Grid size
//                x/y/z             : Target physical coordinates
//                TimeNew           : Target physical time to reach
//                TimeOld           : Physical time before update
//                                    --> This function updates physical time from TimeOld to TimeNew
//                MinDens/Pres/Eint : Density, pressure, and internal energy floors
//                EoS               : EoS object
//                AuxArray_*        : Auxiliary arrays (see the Note above)
//
// Return      :  fluid[]
//-----------------------------------------------------------------------------------------
GPU_DEVICE_NOINLINE
static void Src_ExactCooling( real fluid[], const real B[],
                               const SrcTerms_t *SrcTerms, const real dt, const real dh,
                               const double x, const double y, const double z,
                               const double TimeNew, const double TimeOld,
                               const real MinDens, const real MinPres, const real MinEint,
                               const EoS_t *EoS, const double AuxArray_Flt[], const int AuxArray_Int[] )
{

// check
#  ifdef GAMER_DEBUG
   if ( AuxArray_Flt == NULL )   printf( "ERROR : AuxArray_Flt == NULL in %s !!\n", __FUNCTION__ );
   if ( AuxArray_Int == NULL )   printf( "ERROR : AuxArray_Int == NULL in %s !!\n", __FUNCTION__ );
#  endif

// example
   /*
   const real CoolingRate      = (real)AuxArray_Flt[0];
   Eint  = Hydro_Con2Eint( fluid[DENS], fluid[MOMX], fluid[MOMY], fluid[MOMZ], fluid[ENGY],
                           CheckMinEint_Yes, MinEint, Emag );
   Enth  = fluid[ENGY] - Eint;
   Eint -= fluid[DENS]*CoolingRate*dt;
   */


   const int    TEF_N        = AuxArray_Int[0];   // number of points for lambda(T) sampling in LOG
   const double cl_CV        = AuxArray_Flt[0];   // 1.0/(GAMMA-1.0)
   const double TEF_TN       = AuxArray_Flt[1];   // == Tref, high enough, but affects sampling resolution
   const double TEF_Tmin     = AuxArray_Flt[2];   // MIN temperature 
   const double TEF_dltemp   = AuxArray_Flt[3];   // sampling resolution (Kelvin), LOG!
   const double cl_moli_mole = AuxArray_Flt[5];   // Assume the molecular weights are constant, mu_e*mu_i = 1.464 
   const double cl_mol       = AuxArray_Flt[6];   // mean (total) molecular weights 
   const double cl_mp        = AuxArray_Flt[7];   // proton mass
   const double cl_kB        = AuxArray_Flt[8];   // Boltzmann constant in erg/K

#  ifdef __CUDACC__
   const real *TEF_lambda = SrcTerms->EC_TEF_lambda_DevPtr;
   const real *TEF_alpha  = SrcTerms->EC_TEF_alpha_DevPtr;
   const real *TEFc       = SrcTerms->EC_TEFc_DevPtr;
#  else
   const real *TEF_lambda = h_SrcEC_TEF_lambda;
   const real *TEF_alpha  = h_SrcEC_TEF_alpha;
   const real *TEFc       = h_SrcEC_TEFc;
#  endif

   double Temp, Eint, Enth, Emag, rho_num, Tini, Eintf, dedtmean, Tk, lambdaTini, tcool, Ynew;
   int k, knew;
   const bool CheckMinTemp_Yes = true;

// (1) Get the temperature and compute the old internal energy
#  ifdef MHD
   Emag  = (real)0.5*( SQR(B[MAGX]) + SQR(B[MAGY]) + SQR(B[MAGZ]) );
#  else
   Emag  = (real)0.0;
#  endif
// EoS->DensEint2Temp_GPUPtr, EoS_DensEint2Temp_CPUPtr
#  ifdef __CUDACC__
   Temp = (real) Hydro_Con2Temp( fluid[DENS], fluid[MOMX], fluid[MOMY], fluid[MOMZ], fluid[ENGY], fluid+NCOMP_FLUID, 
                                 CheckMinTemp_Yes, TEF_Tmin, Emag, EoS->DensEint2Temp_FuncPtr, 
                                 EoS->AuxArrayDevPtr_Flt, EoS->AuxArrayDevPtr_Int, EoS->Table ); 
#  else
   Temp = (real) Hydro_Con2Temp( fluid[DENS], fluid[MOMX], fluid[MOMY], fluid[MOMZ], fluid[ENGY], fluid+NCOMP_FLUID, 
                                 CheckMinTemp_Yes, TEF_Tmin, Emag, EoS_DensEint2Temp_CPUPtr, 
                                 EoS_AuxArray_Flt, EoS_AuxArray_Int, h_EoS_Table );
#  endif

   rho_num = fluid[DENS]/cl_mp;   // gas number density
   Eint = cl_CV*cl_kB*rho_num*Temp;   // internal energy before cooling
   Enth = fluid[ENGY] - Eint;
   Tini = Temp;

// (2) Decide the index k (an interval) where Tini falls into
   k = int((log10(Tini)-log10(TEF_Tmin))/TEF_dltemp);   // Note: array index changed, now starts from k=0 
   Tk = POW(10.0, (log10(TEF_Tmin)+k*TEF_dltemp));
   lambdaTini = TEF_lambda[k] * POW((Tini/Tk), TEF_alpha[k]);
// Compute the cooling time
   tcool = cl_CV*(cl_kB*cl_moli_mole*Tini)/(rho_num*cl_mol*lambdaTini);

// (3) Calculate Ynew
   Ynew  = TEF( Tini, k, TEF_lambda, TEF_alpha, TEFc, AuxArray_Flt, AuxArray_Int ) + (Tini/TEF_TN)*(TEF_lambda[TEF_N-1]/lambdaTini)*(dt/tcool);

// (4) Find the new power law interval where Ynew resides
   for (int i=k; i>=0; i--){
      if( Ynew < TEFc[i] ){
         knew = i;
         Temp = TEFinv( Ynew, knew, TEF_lambda, TEF_alpha, TEFc, AuxArray_Flt, AuxArray_Int );
         goto label;
      }
   }
   Temp = TEF_Tmin; // reached the floor: Tn+1 < Tfloor
   knew = 0;  
   label: // label for goto statement

// (5) Calculate the new internal energy and update fluid[ENGY]
   Eintf = cl_CV*cl_kB*rho_num*Temp;
   dedtmean = -(Eintf-Eint)/dt;
   fluid[ENGY] = Enth + Eintf;

} // FUNCTION : Src_ExactCooling


GPU_DEVICE static
// the temporal evolution function (TEF)
double TEF( double TEMP, int k, const real TEF_lambda[], const real TEF_alpha[], const real TEFc[],
            const double AuxArray_Flt[], const int AuxArray_Int[] ){

   const int    TEF_N      = AuxArray_Int[0];   // number of points for lambda(T) sampling in LOG
   const double TEF_TN     = AuxArray_Flt[1];   // == Tref, high enough, but affects sampling resolution
   const double TEF_Tmin   = AuxArray_Flt[2];   // MIN temperature 
   const double TEF_dltemp = AuxArray_Flt[3];   // sampling resolution (Kelvin), LOG!

/*
#  ifdef __CUDACC__
   const real *TEF_lambda = SrcTerms->EC_TEF_lambda_DevPtr;
   const real *TEF_alpha  = SrcTerms->EC_TEF_alpha_DevPtr;
   const real *TEFc       = SrcTerms->EC_TEFc_DevPtr;
#  else
   const real *TEF_lambda = h_SrcEC_TEF_lambda;
   const real *TEF_alpha  = h_SrcEC_TEF_alpha;
   const real *TEFc       = h_SrcEC_TEFc;
#  endif
*/
   double TEF, Tk;
   Tk = POW(10.0, log10(TEF_Tmin) + k*TEF_dltemp);
// Do the integration in Gapari (2009) Eq. (24)
   if ( TEF_alpha[k] != 1.0 ){
       TEF = TEFc[k] + ((1.0/(1.0-TEF_alpha[k])) * (TEF_lambda[TEF_N-1] / TEF_lambda[k]) * (Tk/TEF_TN) * (1.0-POW((Tk/TEMP), (TEF_alpha[k]-1.0))));
   }
   else   TEF = TEFc[k] + ((TEF_lambda[TEF_N-1]/TEF_lambda[k]) * (Tk/TEF_TN) * log(Tk/TEMP));
   
   return TEF;
}


GPU_DEVICE static
// the INVERSE temporal evolution function (TEF^-1)
double TEFinv( double Y, int k, const real TEF_lambda[], const real TEF_alpha[], const real TEFc[],
               const double AuxArray_Flt[], const int AuxArray_Int[] ){

   const int    TEF_N      = AuxArray_Int[0];   // number of points for lambda(T) sampling in LOG
   const double TEF_TN     = AuxArray_Flt[1];   // == Tref, high enough, but affects sampling resolution
   const double TEF_Tmin   = AuxArray_Flt[2];   // MIN temperature 
   const double TEF_dltemp = AuxArray_Flt[3];   // sampling resolution (Kelvin), LOG!

/*
#  ifdef __CUDACC__
   const real *TEF_lambda = SrcTerms->EC_TEF_lambda_DevPtr;
   const real *TEF_alpha  = SrcTerms->EC_TEF_alpha_DevPtr;
   const real *TEFc       = SrcTerms->EC_TEFc_DevPtr;
#  else
   const real *TEF_lambda = h_SrcEC_TEF_lambda;
   const real *TEF_alpha  = h_SrcEC_TEF_alpha;
   const real *TEFc       = h_SrcEC_TEFc;
#  endif
*/
   double TEFinv, Tk, Yk;
   Tk = POW(10.0, log10(TEF_Tmin) + k*TEF_dltemp); 
   Yk = TEFc[k]; 
 
   if ( TEF_alpha[k] != 1.0 ){
      TEFinv = Tk*POW(1.0-(1.0-TEF_alpha[k])*(TEF_lambda[k]/TEF_lambda[TEF_N-1])*(TEF_TN/Tk)*(Y-Yk), 1.0/(1.0-TEF_alpha[k]));
   } 
   else   TEFinv = Tk * exp(-((TEF_lambda[k]/TEF_lambda[TEF_N-1]) * (TEF_TN/Tk) * (Y-Yk)));
 
   return TEFinv;
}



// ==================================================
// III. [Optional] Add the work to be done every time
//      before calling the major source-term function
// ==================================================

//-------------------------------------------------------------------------------------------------------
// Function    :  Src_WorkBeforeMajorFunc_ExactCooling
// Description :  Specify work to be done every time before calling the major source-term function
//
// Note        :  1. Invoked by Src_WorkBeforeMajorFunc()
//                   --> By linking to "Src_WorkBeforeMajorFunc_EC_Ptr" in Src_Init_ExactCooling()
//                2. Add "#ifndef __CUDACC__" since this routine is only useful on CPU
//
// Parameter   :  lv               : Target refinement level
//                TimeNew          : Target physical time to reach
//                TimeOld          : Physical time before update
//                                   --> The major source-term function will update the system from TimeOld to TimeNew
//                dt               : Time interval to advance solution
//                                   --> Physical coordinates : TimeNew - TimeOld == dt
//                                       Comoving coordinates : TimeNew - TimeOld == delta(scale factor) != dt
//                AuxArray_Flt/Int : Auxiliary arrays
//                                   --> Can be used and/or modified here
//                                   --> Must call Src_SetConstMemory_ExactCooling() after modification
//
// Return      :  AuxArray_Flt/Int[]
//-------------------------------------------------------------------------------------------------------
#ifndef __CUDACC__
void Src_WorkBeforeMajorFunc_ExactCooling( const int lv, const double TimeNew, const double TimeOld, const double dt,
                                           double AuxArray_Flt[], int AuxArray_Int[] )
{

// uncomment the following lines if the auxiliary arrays have been modified
#  ifdef GPU
   Src_SetConstMemory_ExactCooling( AuxArray_Flt, AuxArray_Int,
                                    SrcTerms.EC_AuxArrayDevPtr_Flt, SrcTerms.EC_AuxArrayDevPtr_Int );
#  endif

#  ifdef GPU
   Src_PassData2GPU_ExactCooling();
#  endif


} // FUNCTION : Src_WorkBeforeMajorFunc_ExactCooling
#endif



#ifdef __CUDACC__
//-------------------------------------------------------------------------------------------------------
// Function    :  Src_PassData2GPU_ExactCooling
// Description :  Transfer data to GPU
//
// Note        :  1. Invoked by Src_WorkBeforeMajorFunc_ExactCooling()
//                2. Use synchronous transfer
//
// Parameter   :  None
// Return      :  None
// -------------------------------------------------------------------------------------------------------
void Src_PassData2GPU_ExactCooling()
{

   const int TEF_N = SrcTerms.EC_TEF_N;   // number of points for lambda(T) sampling in LOG

// use synchronous transfer
   CUDA_CHECK_ERROR(  cudaMemcpy( d_SrcEC_TEF_lambda, h_SrcEC_TEF_lambda, TEF_N, cudaMemcpyHostToDevice )  );
   CUDA_CHECK_ERROR(  cudaMemcpy( d_SrcEC_TEF_alpha,  h_SrcEC_TEF_alpha,  TEF_N, cudaMemcpyHostToDevice )  );
   CUDA_CHECK_ERROR(  cudaMemcpy( d_SrcEC_TEFc,       h_SrcEC_TEFc,       TEF_N, cudaMemcpyHostToDevice )  );

} // FUNCTION : Src_PassData2GPU_ExactCooling
#endif // #ifdef __CUDACC__



// ================================
// IV. Set initialization functions
// ================================

#ifdef __CUDACC__
#  define FUNC_SPACE __device__ static
#else
#  define FUNC_SPACE            static
#endif

FUNC_SPACE SrcFunc_t SrcFunc_Ptr = Src_ExactCooling;

//-----------------------------------------------------------------------------------------
// Function    :  Src_SetCPU/GPUFunc_ExactCooling
// Description :  Return the function pointer of the CPU/GPU source-term function
//
// Note        :  1. Invoked by Src_Init_ExactCooling()
//                2. Call-by-reference
//
// Parameter   :  SrcFunc_CPU/GPUPtr : CPU/GPU function pointer to be set
//
// Return      :  SrcFunc_CPU/GPUPtr
//-----------------------------------------------------------------------------------------
#ifdef __CUDACC__
__host__
void Src_SetGPUFunc_ExactCooling( SrcFunc_t &SrcFunc_GPUPtr )
{
   CUDA_CHECK_ERROR(  cudaMemcpyFromSymbol( &SrcFunc_GPUPtr, SrcFunc_Ptr, sizeof(SrcFunc_t) )  );
}

#else

void Src_SetCPUFunc_ExactCooling( SrcFunc_t &SrcFunc_CPUPtr )
{
   SrcFunc_CPUPtr = SrcFunc_Ptr;
}

#endif // #ifdef __CUDACC__ ... else ...



#ifdef __CUDACC__
//-------------------------------------------------------------------------------------------------------
// Function    :  Src_SetConstMemory_ExactCooling
// Description :  Set the constant memory variables on GPU
//
// Note        :  1. Adopt the suggested approach for CUDA version >= 5.0
//                2. Invoked by Src_Init_ExactCooling() and, if necessary, Src_WorkBeforeMajorFunc_ExactCooling()
//                3. SRC_NAUX_USER is defined in Macro.h
//
// Parameter   :  AuxArray_Flt/Int : Auxiliary arrays to be copied to the constant memory
//                DevPtr_Flt/Int   : Pointers to store the addresses of constant memory arrays
//
// Return      :  c_Src_EC_AuxArray_Flt[], c_Src_EC_AuxArray_Int[], DevPtr_Flt, DevPtr_Int
//---------------------------------------------------------------------------------------------------
void Src_SetConstMemory_ExactCooling( const double AuxArray_Flt[], const int AuxArray_Int[],
                                       double *&DevPtr_Flt, int *&DevPtr_Int )
{

// copy data to constant memory
   CUDA_CHECK_ERROR(  cudaMemcpyToSymbol( c_Src_EC_AuxArray_Flt, AuxArray_Flt, SRC_NAUX_USER*sizeof(double) )  );
   CUDA_CHECK_ERROR(  cudaMemcpyToSymbol( c_Src_EC_AuxArray_Int, AuxArray_Int, SRC_NAUX_USER*sizeof(int   ) )  );

// obtain the constant-memory pointers
   CUDA_CHECK_ERROR(  cudaGetSymbolAddress( (void **)&DevPtr_Flt, c_Src_EC_AuxArray_Flt) );
   CUDA_CHECK_ERROR(  cudaGetSymbolAddress( (void **)&DevPtr_Int, c_Src_EC_AuxArray_Int) );

} // FUNCTION : Src_SetConstMemory_ExactCooling
#endif // #ifdef __CUDACC__



#ifndef __CUDACC__

// function pointer
//extern void (*Src_WorkBeforeMajorFunc_EC_Ptr)( const int lv, const double TimeNew, const double TimeOld, const double dt,
//                                               double AuxArray_Flt[], int AuxArray_Int[] );
//extern void (*Src_End_EC_Ptr)();

//-----------------------------------------------------------------------------------------
// Function    :  Src_Init_ExactCooling
// Description :  Initialize a user-specified source term
//
// Note        :  1. Set auxiliary arrays by invoking Src_SetAuxArray_*()
//                   --> Copy to the GPU constant memory and store the associated addresses
//                2. Set the source-term function by invoking Src_SetCPU/GPUFunc_*()
//                3. Set the function pointers "Src_WorkBeforeMajorFunc_EC_Ptr" and "Src_End_EC_Ptr"
//                4. Invoked by Src_Init()
//                   --> Enable it by linking to the function pointer "Src_Init_EC_Ptr"
//                5. Add "#ifndef __CUDACC__" since this routine is only useful on CPU
//
// Parameter   :  None
//
// Return      :  None
//-----------------------------------------------------------------------------------------
void Src_Init_ExactCooling()
{

// set the auxiliary arrays
   Src_SetAuxArray_ExactCooling( Src_EC_AuxArray_Flt, Src_EC_AuxArray_Int );

// copy the auxiliary arrays to the GPU constant memory and store the associated addresses
#  ifdef GPU
   Src_SetConstMemory_ExactCooling( Src_EC_AuxArray_Flt, Src_EC_AuxArray_Int,
                                    SrcTerms.EC_AuxArrayDevPtr_Flt, SrcTerms.EC_AuxArrayDevPtr_Int );
#  else
   SrcTerms.EC_AuxArrayDevPtr_Flt = Src_EC_AuxArray_Flt;
   SrcTerms.EC_AuxArrayDevPtr_Int = Src_EC_AuxArray_Int;
#  endif

// set the major source-term function
   Src_SetCPUFunc_ExactCooling( SrcTerms.EC_CPUPtr );

#  ifdef GPU
   Src_SetGPUFunc_ExactCooling( SrcTerms.EC_GPUPtr );
   SrcTerms.EC_FuncPtr = SrcTerms.EC_GPUPtr;
#  else
   SrcTerms.EC_FuncPtr = SrcTerms.EC_CPUPtr;
#  endif

// set the auxiliary functions
//   Src_WorkBeforeMajorFunc_EC_Ptr = Src_WorkBeforeMajorFunc_ExactCooling;
//   Src_End_EC_Ptr                 = Src_End_ExactCooling;

   printf( "Debugging!! h_SrcEC_TEF_lambda[0] = %14.8e\n", h_SrcEC_TEF_lambda[0]);

//#  ifndef __CUDACC__
// Initialize the cooling function
   const int    TEF_N        = Src_EC_AuxArray_Int[0];   // number of points for lambda(T) sampling in LOG
   const int    TEF_int      = TEF_N-1;                  // number of intervals
   const double TEF_TN       = Src_EC_AuxArray_Flt[1];   // == Tref, high enough, but affects sampling resolution
   const double TEF_Tmin     = Src_EC_AuxArray_Flt[2];   // MIN temperature 
//   printf( "Finish loading parameters!!\n" );
   const double TEF_dltemp   = Src_EC_AuxArray_Flt[3];   // sampling resolution (Kelvin), LOG!
   const double cl_Z         = Src_EC_AuxArray_Flt[4];   // metallicity (in Zsun)
   const double cl_moli_mole = Src_EC_AuxArray_Flt[5];   // Assume the molecular weights are constant, mu_e*mu_i = 1.464 
   const double cl_mp        = Src_EC_AuxArray_Flt[7];   // proton mass

   printf( "Debugging!! TEF_N = %d\n", TEF_N);

   double emis, LAMBDAT, Ti, Tip1;
// k = TEF_N-1
   Cool_fct(1e-25, TEF_TN, emis, LAMBDAT, cl_Z, cl_moli_mole, cl_mp);
   printf( "Debugging!! LAMBDAT = %14.8e, emis = %14.8e, TEF_TN = %14.8e, h_SrcEC_TEF_lambda[0] = %14.8e\n", LAMBDAT, emis, TEF_TN, h_SrcEC_TEF_lambda[0] ); 
   h_SrcEC_TEF_lambda[TEF_N-1] = LAMBDAT;
//   printf( "Finish loading parameters!!\n" );
   h_SrcEC_TEF_alpha[TEF_N-1]  = 0.0;  //h_SrcEC_TEF_alpha[TEF_N-2];   // is never required >> just as N-2
   
   for (int i=TEF_N-2; i>=0; i--){
      Ti   = POW(10, log10(TEF_Tmin) + i*TEF_dltemp);
      Tip1 = POW(10, log10(TEF_Tmin) + (i+1)*TEF_dltemp);
  
      Cool_fct(1e-25, Ti, emis, LAMBDAT, cl_Z, cl_moli_mole, cl_mp);
      h_SrcEC_TEF_lambda[i] = LAMBDAT;
      h_SrcEC_TEF_alpha[i]  = (log10(h_SrcEC_TEF_lambda[i+1]) - log10(h_SrcEC_TEF_lambda[i])) / (log10(Tip1) - log10(Ti));
   }

// Initialize the constant of intregration
   double Ti_2, Tip1_2;
   h_SrcEC_TEFc[TEF_N-1] = 0.0;   // TEF(Tref)
   for (int i=TEF_N-2; i>=0; i--){
      Ti_2   = POW(10.0, log10(TEF_Tmin) + i*TEF_dltemp);
      Tip1_2 = POW(10.0, log10(TEF_Tmin) + (i+1)*TEF_dltemp);
 
      if (h_SrcEC_TEF_alpha[i] != 1.0){
         h_SrcEC_TEFc[i] = h_SrcEC_TEFc[i+1] - (1.0/(1.0-h_SrcEC_TEF_alpha[i]))*(h_SrcEC_TEF_lambda[TEF_N-1]/h_SrcEC_TEF_lambda[i])*(Ti_2/TEF_TN)*(1.0-POW(Ti_2/Tip1_2, h_SrcEC_TEF_alpha[i]-1.0));
      } 
      else   h_SrcEC_TEFc[i] = h_SrcEC_TEFc[i+1] - (h_SrcEC_TEF_lambda[TEF_N-1]/h_SrcEC_TEF_lambda[i])*(Ti_2/TEF_TN)*log(Ti_2/Tip1_2);
   }
//#  endif

} // FUNCTION : Src_Init_ExactCooling



// Sutherland-Dopita cooling function, with optimal parmetrization over a wide range of T and Z
void Cool_fct( double Dens, double Temp, double Emis, double Lambdat, double Z, double cl_moli_mole, double mp ){ 
 
   double TLOGC = 5.65;
   double QLOGC = -21.566;
   double QLOGINFTY = -23.1;
   double PPP = 0.8;
   double TLOGM = 5.1;
   double QLOGM = -20.85;
   double SIG = 0.65;
   double Zm = Z;   
   double TLOG = log10(Temp);
 
   Lambdat = 0.;   
   if (Zm < 0)   Zm = 0;                
   double QLOG0, ARG, BUMP1RHS, BUMP2LHS, QLAMBDA0, QLOG1, QLAMBDA1, ne_ni;

   if (TLOG >= 6.1)   QLOG0 = -26.39 + 0.471*log10(Temp + 3.1623e6);
   else if (TLOG >= 4.9){
      ARG = pow(10.0, (-(TLOG-4.9)/0.5)) + 0.077302;
      QLOG0 = -22.16 + log10(ARG);
   }            
   else if (TLOG >= 4.25){
      BUMP1RHS = -21.98 - ((TLOG-4.25)/0.55);
      BUMP2LHS = -22.16 - pow((TLOG-4.9)/0.284, 2);
      QLOG0 = fmax(BUMP1RHS, BUMP2LHS);
   }            
   else   QLOG0 = -21.98 - pow((TLOG-4.25)/0.2, 2);
                
   if (QLOG0 < -30.0)   QLOG0 = -30.0;
   QLAMBDA0 = pow(10.0, QLOG0);
                
   if (TLOG >= 5.65){
      QLOG1 = QLOGC - PPP*(TLOG-TLOGC);
      QLOG1 = fmax(QLOG1, QLOGINFTY);
   }
   else   QLOG1 = QLOGM - pow((TLOG-TLOGM)/SIG, 2);
   
   if (QLOG1 < -30.0)   QLOG1 = -30.0;
   QLAMBDA1 = pow(10.0, QLOG1);
   
//   Lambdat = QLAMBDA0 + Zm*QLAMBDA1;
// For testing purpose
   Lambdat = 3.2217e-27 * sqrt(Temp);  // / (UNIT_E*POW(UNIT_L, 3)/UNIT_T);
   
   ne_ni = (Dens*Dens) / (cl_moli_mole*mp*mp);
   Emis = ne_ni * Lambdat; // emissivity: lum/vol

   printf( "Debugging!! I'm working! Lambdat = %14.8e\n", Lambdat); 
}



//-----------------------------------------------------------------------------------------
// Function    :  Src_End_ExactCooling
// Description :  Free the resources used by a user-specified source term
//
// Note        :  1. Invoked by Src_End()
//                   --> Enable it by linking to the function pointer "Src_End_EC_Ptr"
//                2. Add "#ifndef __CUDACC__" since this routine is only useful on CPU
//
// Parameter   :  None
//
// Return      :  None
//-----------------------------------------------------------------------------------------
void Src_End_ExactCooling()
{


} // FUNCTION : Src_End_ExactCooling

#endif // #ifndef __CUDACC__



#endif // #if ( MODEL == HYDRO )  

#include "GAMER.h"
#include "TestProb.h"



// problem-specific global variables
// =======================================================================================
static double EC_Temp;
static double EC_Dens;
       int    count = 0;
// =======================================================================================




//-------------------------------------------------------------------------------------------------------
// Function    :  Validate
// Description :  Validate the compilation flags and runtime parameters for this test problem
//
// Note        :  None
//
// Parameter   :  None
//
// Return      :  None
//-------------------------------------------------------------------------------------------------------
void Validate()
{

   if ( MPI_Rank == 0 )    Aux_Message( stdout, "   Validating test problem %d ...\n", TESTPROB_ID );

#  if ( MODEL != HYDRO )
   Aux_Error( ERROR_INFO, "MODEL != HYDRO !!\n" );
#  endif

// examples
/*
// errors
#  if ( MODEL != HYDRO )
   Aux_Error( ERROR_INFO, "MODEL != HYDRO !!\n" );
#  endif

#  ifndef GRAVITY
   Aux_Error( ERROR_INFO, "GRAVITY must be enabled !!\n" );
#  endif

#  ifdef PARTICLE
   Aux_Error( ERROR_INFO, "PARTICLE must be disabled !!\n" );
#  endif

#  ifdef GRAVITY
   if ( OPT__BC_FLU[0] == BC_FLU_PERIODIC  ||  OPT__BC_POT == BC_POT_PERIODIC )
      Aux_Error( ERROR_INFO, "do not use periodic BC for this test !!\n" );
#  endif


// warnings
   if ( MPI_Rank == 0 )
   {
#     ifndef DUAL_ENERGY
         Aux_Message( stderr, "WARNING : it's recommended to enable DUAL_ENERGY for this test !!\n" );
#     endif

      if ( FLAG_BUFFER_SIZE < 5 )
         Aux_Message( stderr, "WARNING : it's recommended to set FLAG_BUFFER_SIZE >= 5 for this test !!\n" );
   } // if ( MPI_Rank == 0 )
*/


   if ( MPI_Rank == 0 )    Aux_Message( stdout, "   Validating test problem %d ... done\n", TESTPROB_ID );

} // FUNCTION : Validate



// replace HYDRO by the target model (e.g., MHD/ELBDM) and also check other compilation flags if necessary (e.g., GRAVITY/PARTICLE)
#if ( MODEL == HYDRO )
//-------------------------------------------------------------------------------------------------------
// Function    :  SetParameter
// Description :  Load and set the problem-specific runtime parameters
//
// Note        :  1. Filename is set to "Input__TestProb" by default
//                2. Major tasks in this function:
//                   (1) load the problem-specific runtime parameters
//                   (2) set the problem-specific derived parameters
//                   (3) reset other general-purpose parameters if necessary
//                   (4) make a note of the problem-specific parameters
//                3. Must call EoS_Init() before calling any other EoS routine
//
// Parameter   :  None
//
// Return      :  None
//-------------------------------------------------------------------------------------------------------
void SetParameter()
{

   if ( MPI_Rank == 0 )    Aux_Message( stdout, "   Setting runtime parameters ...\n" );


// (1) load the problem-specific runtime parameters
   const char FileName[] = "Input__TestProb";
   ReadPara_t *ReadPara  = new ReadPara_t;

// (1-1) add parameters in the following format:
// --> note that VARIABLE, DEFAULT, MIN, and MAX must have the same data type
// --> some handy constants (e.g., Useless_bool, Eps_double, NoMin_int, ...) are defined in "include/ReadPara.h"
// ********************************************************************************************************************************
// ReadPara->Add( "KEY_IN_THE_FILE",   &VARIABLE,              DEFAULT,       MIN,              MAX               );
// ********************************************************************************************************************************
   ReadPara->Add( "EC_Temp",        &EC_Temp,                  1000000.0,     Eps_double,       NoMax_double      );
   ReadPara->Add( "EC_Dens",        &EC_Dens,                  1.0,           Eps_double,       NoMax_double      );

   ReadPara->Read( FileName );

   delete ReadPara;

// (1-2) set the default values

// (1-3) check the runtime parameters


// (2) set the problem-specific derived parameters


// (3) reset other general-purpose parameters
//     --> a helper macro PRINT_WARNING is defined in TestProb.h
   const long   End_Step_Default = __INT_MAX__;
   const double End_T_Default    = 10.0*Const_Gyr/UNIT_T;

   if ( END_STEP < 0 ) {
      END_STEP = End_Step_Default;
      PRINT_WARNING( "END_STEP", END_STEP, FORMAT_LONG );
   }

   if ( END_T < 0.0 ) {
      END_T = End_T_Default;
      PRINT_WARNING( "END_T", END_T, FORMAT_REAL );
   }


// (4) make a note
   if ( MPI_Rank == 0 )
   {
      Aux_Message( stdout, "=============================================================================\n" );
      Aux_Message( stdout, "  test problem ID           = %d\n",     TESTPROB_ID );
      Aux_Message( stdout, "  EC_Temp                   = %13.7e\n", EC_Temp     );
      Aux_Message( stdout, "  EC_Dens                   = %13.7e\n", EC_Dens     );
      Aux_Message( stdout, "=============================================================================\n" );
   }


   if ( MPI_Rank == 0 )    Aux_Message( stdout, "   Setting runtime parameters ... done\n" );

} // FUNCTION : SetParameter



//-------------------------------------------------------------------------------------------------------
// Function    :  SetGridIC
// Description :  Set the problem-specific initial condition on grids
//
// Note        :  1. This function may also be used to estimate the numerical errors when OPT__OUTPUT_USER is enabled
//                   --> In this case, it should provide the analytical solution at the given "Time"
//                2. This function will be invoked by multiple OpenMP threads when OPENMP is enabled
//                   (unless OPT__INIT_GRID_WITH_OMP is disabled)
//                   --> Please ensure that everything here is thread-safe
//                3. Even when DUAL_ENERGY is adopted for HYDRO, one does NOT need to set the dual-energy variable here
//                   --> It will be calculated automatically
//                4. For MHD, do NOT add magnetic energy (i.e., 0.5*B^2) to fluid[ENGY] here
//                   --> It will be added automatically later
//
// Parameter   :  fluid    : Fluid field to be initialized
//                x/y/z    : Physical coordinates
//                Time     : Physical time
//                lv       : Target refinement level
//                AuxArray : Auxiliary array
//
// Return      :  fluid
//-------------------------------------------------------------------------------------------------------
void SetGridIC( real fluid[], const double x, const double y, const double z, const double Time,
                const int lv, double AuxArray[] )
{

   double Dens, MomX, MomY, MomZ, Pres, Eint, Etot;
// Convert the input number density into mass density rho
   double cl_dens = (EC_Dens*Const_mp*0.61) / UNIT_D;
   double cl_pres = EC_Dens*Const_kB*EC_Temp / UNIT_P; 

   Dens = cl_dens;
   MomX = 0.0;
   MomY = 0.0;
   MomZ = 0.0;
   Pres = cl_pres;
   Eint = EoS_DensPres2Eint_CPUPtr( Dens, Pres, NULL, EoS_AuxArray_Flt,
                                    EoS_AuxArray_Int, h_EoS_Table );   // assuming EoS requires no passive scalars
   Etot = Hydro_ConEint2Etot( Dens, MomX, MomY, MomZ, Eint, 0.0 );     // do NOT include magnetic energy here

// set the output array
   fluid[DENS] = Dens;
   fluid[MOMX] = MomX;
   fluid[MOMY] = MomY;
   fluid[MOMZ] = MomZ;
   fluid[ENGY] = Etot;

   double Temp_tmp;
   Temp_tmp = (real) Hydro_Con2Temp( fluid[DENS], fluid[MOMX], fluid[MOMY], fluid[MOMZ], fluid[ENGY], fluid+NCOMP_FLUID, 
                                 true, MIN_TEMP, 0.0, EoS_DensEint2Temp_CPUPtr, 
                                 EoS_AuxArray_Flt, EoS_AuxArray_Int, h_EoS_Table );
   count += 1;
   if ( count < 300 && count > 257 ){
//      printf("Debugging in Init!! fluid[DENS] = %14.8e, fluid[MOMX] = %14.8e, fluid[ENGY] = %14.8e, Temp = %14.8e\n", fluid[DENS], fluid[MOMX], fluid[ENGY], Temp_tmp);
   }
} // FUNCTION : SetGridIC



#ifdef MHD
//-------------------------------------------------------------------------------------------------------
// Function    :  SetBFieldIC
// Description :  Set the problem-specific initial condition of magnetic field
//
// Note        :  1. This function will be invoked by multiple OpenMP threads when OPENMP is enabled
//                   (unless OPT__INIT_GRID_WITH_OMP is disabled)
//                   --> Please ensure that everything here is thread-safe
//
// Parameter   :  magnetic : Array to store the output magnetic field
//                x/y/z    : Target physical coordinates
//                Time     : Target physical time
//                lv       : Target refinement level
//                AuxArray : Auxiliary array
//
// Return      :  magnetic
//-------------------------------------------------------------------------------------------------------
void SetBFieldIC( real magnetic[], const double x, const double y, const double z, const double Time,
                  const int lv, double AuxArray[] )
{

   /*
// example
   magnetic[MAGX] = 1.0;
   magnetic[MAGY] = 2.0;
   magnetic[MAGZ] = 3.0;
   */

} // FUNCTION : SetBFieldIC
#endif // #ifdef MHD
#endif // #if ( MODEL == HYDRO )



//-------------------------------------------------------------------------------------------------------
// Function    :  Init_TestProb_Hydro_ExactCooling
// Description :  Test problem initializer
//
// Note        :  None
//
// Parameter   :  None
//
// Return      :  None
//-------------------------------------------------------------------------------------------------------
void Init_TestProb_Hydro_ExactCooling()
{

   if ( MPI_Rank == 0 )    Aux_Message( stdout, "%s ...\n", __FUNCTION__ );


// validate the compilation flags and runtime parameters
   Validate();


// replace HYDRO by the target model (e.g., MHD/ELBDM) and also check other compilation flags if necessary (e.g., GRAVITY/PARTICLE)
#  if ( MODEL == HYDRO )
// set the problem-specific runtime parameters
   SetParameter();


// procedure to enable a problem-specific function:
// 1. define a user-specified function (example functions are given below)
// 2. declare its function prototype on the top of this file
// 3. set the corresponding function pointer below to the new problem-specific function
// 4. enable the corresponding runtime option in "Input__Parameter"
//    --> for instance, enable OPT__OUTPUT_USER for Output_User_Ptr

   Init_Function_User_Ptr         = SetGridIC;
//   End_User_Ptr                   = End_ClusterMerger;
//   Aux_Record_User_Ptr            = Aux_Record_ClusterMerger;

#  endif
/*
   Init_Function_User_Ptr            = SetGridIC;
#  ifdef MHD
   Init_Function_BField_User_Ptr     = SetBFieldIC;
#  endif
// comment out Init_ByFile_User_Ptr to use the default
// Init_ByFile_User_Ptr              = NULL; // option: OPT__INIT=3;             example: Init/Init_ByFile.cpp -> Init_ByFile_Default()
   Init_Field_User_Ptr               = NULL; // set NCOMP_PASSIVE_USER;          example: TestProblem/Hydro/Plummer/Init_TestProb_Hydro_Plummer.cpp --> AddNewField()
   Flag_Region_Ptr                   = NULL; // option: OPT__FLAG_REGION;        example: Refing/Flag_Region.cpp
   Flag_User_Ptr                     = NULL; // option: OPT__FLAG_USER;          example: Refine/Flag_User.cpp
   Mis_GetTimeStep_User_Ptr          = NULL; // option: OPT__DT_USER;            example: Miscellaneous/Mis_GetTimeStep_User.cpp
   Mis_UserWorkBeforeNextLevel_Ptr   = NULL; //                                  example: Miscellaneous/Mis_UserWorkBeforeNextLevel.cpp
   Mis_UserWorkBeforeNextSubstep_Ptr = NULL; //                                  example: Miscellaneous/Mis_UserWorkBeforeNextSubstep.cpp
   BC_User_Ptr                       = NULL; // option: OPT__BC_FLU_*=4;         example: TestProblem/ELBDM/ExtPot/Init_TestProb_ELBDM_ExtPot.cpp --> BC()
#  ifdef MHD
   BC_BField_User_Ptr                = NULL; // option: OPT__BC_FLU_*=4;
#  endif
   Flu_ResetByUser_Func_Ptr          = NULL; // option: OPT__RESET_FLUID;        example: Fluid/Flu_ResetByUser.cpp
   Init_DerivedField_User_Ptr        = NULL; // option: OPT__OUTPUT_USER_FIELD;  example: Fluid/Flu_DerivedField_User.cpp
   Output_User_Ptr                   = NULL; // option: OPT__OUTPUT_USER;        example: TestProblem/Hydro/AcousticWave/Init_TestProb_Hydro_AcousticWave.cpp --> OutputError()
   Output_UserWorkBeforeOutput_Ptr   = NULL; // option: none;                    example: Output/Output_UserWorkBeforeOutput.cpp
   Aux_Record_User_Ptr               = NULL; // option: OPT__RECORD_USER;        example: Auxiliary/Aux_Record_User.cpp
   Init_User_Ptr                     = NULL; // option: none;                    example: none
   End_User_Ptr                      = NULL; // option: none;                    example: TestProblem/Hydro/ClusterMerger_vs_Flash/Init_TestProb_ClusterMerger_vs_Flash.cpp --> End_ClusterMerger()
#  ifdef GRAVITY
   Init_ExtAcc_Ptr                   = NULL; // option: OPT__EXT_ACC;            example: SelfGravity/CPU_Gravity/CPU_ExtAcc_PointMass.cpp
   End_ExtAcc_Ptr                    = NULL;
   Init_ExtPot_Ptr                   = NULL; // option: OPT__EXT_POT;            example: SelfGravity/CPU_Poisson/CPU_ExtPot_PointMass.cpp
   End_ExtPot_Ptr                    = NULL;
   Poi_AddExtraMassForGravity_Ptr    = NULL; // option: OPT__GRAVITY_EXTRA_MASS; example: none
   Poi_UserWorkBeforePoisson_Ptr     = NULL; // option: none;                    example: SelfGravity/Poi_UserWorkBeforePoisson.cpp
#  endif
#  ifdef PARTICLE
   Par_Init_ByFunction_Ptr           = NULL; // option: PAR_INIT=1;              example: Particle/Par_Init_ByFunction.cpp
   Par_Init_Attribute_User_Ptr       = NULL; // set PAR_NATT_USER;               example: TestProblem/Hydro/AGORA_IsolatedGalaxy/Init_TestProb_Hydro_AGORA_IsolatedGalaxy.cpp --> AddNewParticleAttribute()
#  endif
#  if ( EOS == EOS_USER )
   EoS_Init_Ptr                      = NULL; // option: EOS in the Makefile;     example: EoS/User_Template/CPU_EoS_User_Template.cpp
   EoS_End_Ptr                       = NULL;
#  endif
#  endif // #if ( MODEL == HYDRO )
   Src_Init_User_Ptr                 = NULL; // option: SRC_USER;                example: SourceTerms/User_Template/CPU_Src_User_Template.cpp
*/

   if ( MPI_Rank == 0 )    Aux_Message( stdout, "%s ... done\n", __FUNCTION__ );

} // FUNCTION : Init_TestProb_Hydro_ExactCooling

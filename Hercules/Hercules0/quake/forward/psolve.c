/*
 * psolve.c: Generate an unstructured mesh and solve the linear system
 *
 * Copyright (c) 2005 Tiankai Tu & Leonardo Ramirez
 * 2008 modifications Urbanic
 *
 *
 *
 * All rights reserved.  May not be used, modified, or copied
 * without permission.
 *
 * Contact:
 * Tiankai Tu
 * tutk@cs.cmu.edu
 *   or
 * Leonardo Ramirez-Guzman
 * leoramirezg@gmail.com
 * LRamirezG@iingen.unam.mx 
 * version_lb_1.0.2
 *
 */

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <float.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <assert.h>
#include <errno.h>
#include <stdarg.h>

#include "util.h"
#include "htimer.h"
#include "etree.h"
#include "octor.h"
#include "psolve.h"
#include "cvm.h"
#include "nrutila.h"
#include "quakesource.h"
#include "output.h"

#include "geodataq.h"
#include "geodataqinit.h"

#define DEBUGMATLAB 1

#ifndef PROCPERNODE
#define PROCPERNODE     4
#endif

#define PI		3.14159265358979323846264338327

#define ERROR           HERC_ERROR

#define GOAHEAD_MSG     100
#define MESH_MSG        101
#define STAT_MSG        102
#define OUT4D_MSG       103
#define DN_MASS_MSG     104
#define AN_MASS_MSG     105
#define DN_FORCE_MSG    106
#define AN_FORCE_MSG    107
#define DN_DISP_MSG     108
#define AN_DISP_MSG     109
#define CVMRECORD_MSG   110

#define BATCH           (1 << 20)
#define LINESIZE        512
#define FILEBUFSIZE     (1 << 25)


#define CONTRIBUTION    901  /* Harboring processors to owner processors */
#define SHARING         902  /* Owner processors to harboring processors */

#define DISTRIBUTION    903  /* Dangling nodes to anchored nodes */
#define ASSIGNMENT      904  /* Anchored nodes to dangling nodes */


double theGlobalDeltaT;
static double theCriticalT;

/* Timing, all variables are in second unit */

/* Calculate the time difference in seconds, assume a - b > 0 */
#define DIFFTIME(a,b)	tv_difftime( &a, &b)

static double theE2ETime = 0;
static double theOctorTime = 0;
static double theOctorNewTreeTime = 0, theOctorRefineTreeTime = 0;
static double theOctorBalanceTreeTime = 0, theOctorBalanceLoadTime = 0;
static double theOctorExtractMeshTime = 0;

/**
 * Timers for the solving time
 * 
 */

static double theSolverTime          = 0;

static double the_B_timer = 0;
static double the_C_timer = 0;
static double the_D_timer = 0;
static double the_E_timer = 0;
static double the_F_timer = 0;
static double the_G_timer = 0;
static double the_H_timer = 0;
static double the_I_timer = 0;
static double the_J_timer = 0;
static double the_K_timer = 0;
static double the_L_timer = 0;
static double the_s_timer = 0;
static double the_E1_timer = 0;
static double the_E2_timer = 0;
static double the_E3_timer = 0;

double theMin_B_timer = 0;
double theMin_C_timer = 0;
double theMin_D_timer = 0;
double theMin_E_timer = 0;
double theMin_F_timer = 0;
double theMin_G_timer = 0;
double theMin_H_timer = 0;
double theMin_I_timer = 0;
double theMin_J_timer = 0;
double theMin_K_timer = 0;
double theMin_L_timer = 0;
double theMin_s_timer = 0;
double theMin_E1_timer = 0;
double theMin_E2_timer = 0;
double theMin_E3_timer = 0;
double theMax_B_timer = 0;
double theMax_C_timer = 0;
double theMax_D_timer = 0;
double theMax_E_timer = 0;
double theMax_F_timer = 0;
double theMax_G_timer = 0;
double theMax_H_timer = 0;
double theMax_I_timer = 0;
double theMax_J_timer = 0;
double theMax_K_timer = 0;
double theMax_L_timer = 0;
double theMax_s_timer = 0;
double theMax_E1_timer = 0;
double theMax_E2_timer = 0;
double theMax_E3_timer = 0;

/* static double theReadSourceForceTime = 0; */
/* static double theAddForceSTime       = 0; */
/* static double theAddForceETime       = 0; */

static htimerv_t timer_monitor		= HTIMERV_ZERO;
static htimerv_t timer_checkpoint_write = HTIMERV_ZERO;


/**
 * Some flags
 */

static double theMeshOutTime = 0;
static double the4DOutTime   = 0;
static double the4DOutSize   = 0;
static int    theMeshOutFlag = DO_OUTPUT;

/** Global: parameters for writting the 4D output */
static output_parameters_t theOutputParameters;

static int    theTimingBarriersFlag = 0;

/*TOPO*/
  
int doIIncludeTopo=1;

/* BUILDINGS */
double doIIncludeBuildings=1.;


/* -----------------------------Non etree property input-------------- */

int theGeodataSwitch = 1;
database_t database;
char theDatabasePath[256];

static double theLatUMesh,theLonUMesh,theLatLMesh,theLonLMesh,theDepthUMesh,theDepthLMesh;
/*---------------Initialization and cleanup routines----------------------*/

static void    local_init(int argc, char **argv);
static void    local_finalize();
static int32_t initparameters(const char *physicsin, const char *numericalin);


/*---------------- Mesh generation data structures -----------------------*/

/* to test different mesh schemes */
double theMeshCriteriaNorth=1, theMeshCriteriaEast=1, theMeshCriteriaZ=1;


#ifdef USECVMDB

#ifndef CVMBUFSIZE
#define CVMBUFSIZE      100
#endif

static off_t    theDBSize;
static double   theDBReplicateTime = 0;

static etree_t* theCVMEp;
static int32_t  theCVMQueryStage; /* 0: refinement; 1: balance */
static double   theCVMQueryTime_Refinement = 0, theCVMQueryTime_Balance = 0;
static double   theCVMQueryTime_Refinement_MAX = 0;
static double   theCVMQueryTime_Balance_MAX = 0;
static int64_t  theCVMQueryCount_Refinement = 0, theCVMQueryCount_Balance = 0;
static double   theXForMeshOrigin, theYForMeshOrigin, theZForMeshOrigin;

static void     replicateDB(const char *dbname);
static void     openDB(const char *dbname);

#else

static double theSliceCVMTime;

/**
 * cvmrecord_t: cvm record.
 *
 */
typedef struct cvmrecord_t {
  char key[12];
  float Vp, Vs, density;
} cvmrecord_t;


static char theCVMFlatFile[128];
static const int theCVMRecordSize = sizeof(cvmrecord_t);
static int theCVMRecordCount;
static cvmrecord_t * theCVMRecord;

static int32_t zsearch(void *base, int32_t count, int32_t recordsize,
                       const point_t *searchpt);
static cvmrecord_t *sliceCVM(const char *cvm_flatfile);

#endif


/**
 * edata_t: Mesh element data fields
 *
 */
typedef struct edata_t {
  float edgesize, Vp, Vs, rho;
  int32_t typeofelement;  /* -1=air, 0=surface; 1=nonsurface*/
} edata_t;


/**
 * mrecord_t: Complete mesh database record
 *
 */
typedef struct mrecord_t {
  etree_addr_t addr;
  mdata_t mdata;
} mrecord_t;

/* Mesh generation related routines */
static int32_t toexpand(octant_t *leaf, double ticksize, const void *data);
static void    setrec(octant_t *leaf, double ticksize, void *data);

static void    mesh_generate();
static void    mesh_printstat();
static int32_t bulkload(etree_t *mep, mrecord_t *partTable, int32_t count);
static void    mesh_output();



/* Solver computation and communication routines */
/**
 * messenger_t: A messenger keeps track of the data exchange.
 *
 */
typedef struct messenger_t {
  int32_t procid;             /* Remote processor id */

  int32_t outsize;            /* Outgoing record size  */
  int32_t insize;             /* Incoming record size  */

  void *outdata;
  void *indata;

  int32_t nodecount;          /* Number of mesh nodes involved */
  int32_t nidx;               /* Current index into the mapping table */
  int32_t *mapping;           /* An array of local node ids of the involved*/

  struct messenger_t *next;

} messenger_t;


/**
 * schedule_t:  Communication schedule.
 *
 */
typedef struct schedule_t {
  int32_t c_count;             /* Num. of processors I need to contribute */
  messenger_t *first_c;        /* or retrieve data cuz I harbor their nodes*/
  messenger_t **messenger_c;   /* Fast lookup table to build c-list*/
  MPI_Request *irecvreqs_c;    /* control for non-blocking receives */
  MPI_Status *irecvstats_c;

  int32_t s_count;             /* Num. of processors who share nodes */
  messenger_t *first_s;        /* owned by me */
  messenger_t **messenger_s;   /* Fast lookup table to build s-list */
  MPI_Request *irecvreqs_s;    /* controls for non-blocking MPI receives */
  MPI_Status  *irecvstats_s;
} schedule_t;


typedef struct mysolver_t {
  e_t *eTable;                  /* Element computation-invariant table */
  n_t *nTable;                  /* Node compuation-invariant table */

  fvector_t *tm1;               /* Displacements at timestep t - 1 */
  fvector_t *tm2;               /* Displacements at timestep t - 2 */
  fvector_t *force;             /* Force accumulation at timestep t */

  schedule_t *dn_sched;         /* Dangling node communication schedule */
  schedule_t *an_sched;         /* Anchored node communication schedule */
} mysolver_t;


static void    solver_init();
static void    solver_printstat();
static void    solver_delete();
static void    solver_run();
void    solver_output_seq();
static int     solver_print_schedules(mysolver_t* solver);

static schedule_t * schedule_new();
static void    schedule_build(mesh_t *mesh, schedule_t *dnsched,
                              schedule_t *ansched);
static void    schedule_allocMPIctl(schedule_t *sched);
static void    schedule_allocmapping(schedule_t *sched);
static void    schedule_delete(schedule_t *sched);
static void    schedule_prepare(schedule_t *sched, int32_t c_outsize,
                                int32_t c_insize, int32_t s_outsize,
                                int32_t s_insize);

static void    schedule_senddata(schedule_t *sched, void *valuetable,
                                 int32_t doublesperentry, int32_t direction,
                                 int32_t msgtag);

static int     schedule_print( schedule_t *sched, char type, FILE* out );
static int     schedule_print_detail( schedule_t* sched, char type, FILE* out );
static int     schedule_print_messenger_list( schedule_t* sched,
					      messenger_t* msg, int count,
					      char type, char cs, FILE* out );

static messenger_t *messenger_new(int32_t procid);
static void    messenger_delete(messenger_t *messenger);
static void    messenger_set(messenger_t *messenger, int32_t outsize,
                             int32_t insize);
static int32_t messenger_countnodes(messenger_t *first);


static void    compute_K();

#ifdef BOUNDARY
static char    compute_setflag(tick_t ldb[3], tick_t ruf[3],
                               tick_t nearendp[3], tick_t farendp[3]);
static void    compute_setboundary(float size, float Vp, float Vs,
                                   float rho, int flag, double dashpot[8][3]);
#endif /* BOUNDARY */

static void    compute_setab(double freq, double *aBasePtr, double *bBasePtr);


static void    compute_addforce_s(int32_t timestep);
static void    compute_addforce_e();

static void    compute_adjust(void *valuetable, int32_t doublesperentry,
                              int32_t how);

static int     interpolate_station_displacements(int32_t step);


/* ---------- Static global variables ------------------------------------ */

static int32_t myID = -1, theGroupSize = -1;
static octree_t *myOctree;
static mesh_t *myMesh;

static int64_t theETotal, theNTotal, theDNTotal;
static mysolver_t *mySolver;
static char *theMEtree, *theOutFile;
static FILE *theOutFp;
static ptsrc_t *thePtSrc; /* Not NULL if the point source falls in my range*/

static fvector_t *myVelocityTable;

static FILE *theMonitorFileFp; /* File that will output the screen */
static char theMonitorFileName[256];

/**
 * \note In the future we should erase K3 from being a global variable and
 * make it a local variable inside the compute_K function.  This is
 * because it is no longer needed for the solution.
 */
static fmatrix_t theK1[8][8], theK2[8][8], theK3[8][8];


static double theABase, theBBase;

/* Proc 0 broadcast these following parameters using message buffers  */
static double theVsCut, theFactor, theFreq, theDeltaT, theDeltaTSquared;
static double theEndT, theStartT;
static double theDomainX, theDomainY, theDomainZ; /* Domain size */
static double theDomainAzimuth;

static int32_t theTotalSteps;
static int32_t theRate;

static damping_type_t theTypeOfDamping;


/**
 * These thresehold values are read from the numerical file and define the
 * top values the target damping and the Vp over Vs ratio can have.  The
 * former affects only the damping terms but the later affects in general
 * the stiffnesses matrices and its influence in the damping terms.  This
 * has shown improvements in the delta_t of up to double to what was needed
 * before without compromising the solution.
 */
static double theThresholdDamping;
static double theThresholdVpVs;
static int    theDampingStatisticsFlag;

static int         theSchedulePrintErrorCheckFlag = 0;
static int         theSchedulePrintToStdout       = 0;
static int         theSchedulePrintToFile         = 1;
static const char* theSchedulePrintFilename       = "schedule_info.txt";


/* ------------------------------------------------------------------------- *
 *                Earthquake source variables and routines
 * ------------------------------------------------------------------------- */
static numerics_info_t theNumericsInformation;
static      mpi_info_t theMPIInformation;

static vector3D_t *myForces;
static int32_t theNodesLoaded;
static int32_t *theNodesLoadedList;

FILE *fpsource;


/* ------------------------------------------------------------------------- *
 *               Output planes variables and data structures
 * ------------------------------------------------------------------------- */

typedef struct plane_strip_element_t {

  int32_t nodestointerpolate[8];
  vector3D_t localcoords;  /* csi, eta, dzeta in (-1,1) x (-1,1) x (-1,1)*/
  double h;

} plane_strip_element_t;

#define MAX_STRIPS_PER_PLANE 1500
/*Could be determined dynmamically but would cost search_point calls*/

typedef struct plane_t {    
  vector3D_t origincoords;		/**< cartesian */
  FILE *fpoutputfile,*fpplanecoordsfile;

  int fieldtooutput; /* 0-(u,v,w) 1-(dudx,dudy,dudz) 2-(dvdx,dvdy,dvdz) 3-(dwdx,dwdy,dwdz)*/
  int numberofstepsalongstrike, numberofstepsdowndip;
  int numberofstripsthisplane;
  int globalnumberofstripsthisplane; /*valid only on output PE's*/
  int typeplaneinput;


  double stepalongstrike, stepdowndip, strike, dip, rake;
  double originlon,originlat,origindepth;
  double *latIn,*lonIn,*depthIn;
  double *vp,*vs,*rho;
  double *elementtype;
  
  int stripstart[MAX_STRIPS_PER_PLANE];
  int stripend[MAX_STRIPS_PER_PLANE];
  plane_strip_element_t * strip[MAX_STRIPS_PER_PLANE];
    
} plane_t;

static char thePlaneDirOut[256];

static double theSurfaceCornersLong[4], theSurfaceCornersLat[4];

static int theNumberOfPlanes;
static int thePlanePrintingTimeStep;
static int howManyStepsForPlane = 0;

static int32_t theTimeStepsToPrint;

static double theRegionLong, theRegionLat, theRegionDepth;

static plane_t *thePlanes;
static int32_t thePlanePrintRate;

static double thePrintPlaneTime = 0;
static double theCollectPlaneTime = 0;

static int planes_GlobalLargestStripCount;
static int planes_LocalLargestStripCount;
static double * planes_output_buffer;
static double * planes_stripMPISendBuffer;
static double * planes_stripMPIRecvBuffer;

#define PLANESTRIP        10001  /* Printing planes identifier */

static print_plane_displacements(int iPlane);
static void output_planes_construct_strips();

/* ----------------------- End output plane variables ---------------------- */

/* ------------------------------------------------------------------------- *
 *               Output stations variables and data structures
 * ------------------------------------------------------------------------- */

static int      theStationsPrintRate ;
static double   *theStationX, *theStationY, *theStationZ;
static int32_t  theStationIntputType, theNumberOfStations, myNumberOfStations = 0;;
static char     theStationsDirOut[256];


typedef struct station_t {
    
  int32_t    id, nodestointerpolate[8];
  double h;
  double     *displacementsX,*displacementsY,*displacementsZ;
    
  vector3D_t coords;	    /**< cartesian */
  vector3D_t localcoords;   /* csi, eta, dzeta in (-1,1) x (-1,1) x (-1,1)*/
  FILE       *fpoutputfile;  
  octant_t *octant;
    
}station_t;

station_t *myStations;

/* ----------------------- End output staStions variables ------------------ */


/* Damping model q(vs)*/
double qualityfactor(double vsinm){
  double qs;
  int dampingModel=3;
  /* Old formula for damping */
  /* zeta        = (edata->Vs < 1500) ? 25 / edata->Vs : 5 / edata->Vs; */
    
  /* New formula for damping acording to Graves from Harmsen et al*/	
  /* vsInKm=(edata->Vs)/1000;
     qs=-16+vsInKm*(104.13+vsInKm*(-25.225+8.2184*vsInKm));
     if( vsInKm <= 300 )
     qs=15;
     zeta = 1/(2*qs);*/    
    
      
  /*CEUS damping */
  if(dampingModel ==1){
    if(vsinm <=  350)qs=35;
    if(vsinm >= 2000)qs=700; 
    if(vsinm > 350 && vsinm <= 500)qs=.1*vsinm;
    if( vsinm <= 2000 && vsinm >= 500)
      qs=-166.667+(650/1500)*vsinm;
  }

  if(dampingModel ==2){
    if(vsinm <=  350)qs=35;
    if(vsinm >= 2000)qs=1000; 
    if(vsinm > 350 && vsinm <= 500)qs=.1*vsinm;
    if( vsinm <= 2000 && vsinm >= 500)
      qs=-266.667+(950/1500)*vsinm;
  } 
    
  if(dampingModel ==3){
    /*Hartzell (for bay area)*/
    if(vsinm < 500)
      qs=0.05*vsinm;
    if(vsinm <= 1000 && vsinm >= 500)
      qs=.23*vsinm-90;
    if(vsinm < 4000 && vsinm > 1000)
      qs=0.14*vsinm;
    if(vsinm >= 4000 )
      qs=700;
  }

  if(dampingModel ==4){
    /* New CEUS */
    if(vsinm < 500)
      qs=0.1*vsinm;
    if(vsinm <= 1000 && vsinm >= 500)
      qs=.18*vsinm-40;
    if(vsinm < 4000 && vsinm > 1000)
      qs=0.14*vsinm;
    if(vsinm >= 4000 )
      qs=700;
    if(vsinm < 350)
      qs=35;
  }
    
  if(dampingModel ==5){
    /* New CEUS II*/
    if(vsinm < 500)
      qs=0.1*vsinm;
    if(vsinm <= 1000 && vsinm >= 500)
      qs=.3*vsinm-100;
    if(vsinm < 4000 && vsinm > 1000)
      qs=0.167*vsinm+33;
    if(vsinm >= 4000 )
      qs=700;
    if(vsinm < 350)
      qs=35;
  }

  if(dampingModel ==6){
    /* New CEUS II*/
      qs=10000;
  }


  return qs; 
}



/* ------------------------------------------------------------------------- *
 *                           Checkpoint variables
 * ------------------------------------------------------------------------- */

static int  theCheckPointingRate, 
  theUseCheckPoint,
  currentCheckPointFile = 0;
static char theCheckPointingDirOut[256];

/* ------------------------------------------------------------------------- */


/**
 * Convenience function to broadcast a string to all processes in a MPI
 * communicator from the PE with the given rank.
 *
 * \param string    Character string to broadcast.
 * \param root_rank Rank (i.e, PE ID) of the process sending the string.
 *                  That is, root of the broadcast.
 * \param comm      MPI communicator structure.
 *
 * \return 0 on success, -1 on error (really abort through \c solver_abort() ).
 *
 * \author jclopez
 */
static int
broadcast_string (char** string, int root_rank, MPI_Comm comm)
{
  int32_t string_len = 0;

  HU_ASSERT_PTR( string );

  if (myID == root_rank) {
    HU_ASSERT_PTR( *string );
    string_len = strlen( *string );
  }

  MPI_Bcast (&string_len, 1, MPI_INT, root_rank, comm);

  if (myID != root_rank) {
    *string = (char*)malloc( (string_len + 1) * sizeof (char) );

    if (*string == NULL) {
      solver_abort( __FUNCTION_NAME, "string memory allocation", NULL );
      return -1;
    }
  }

  /* include terminating \0 character in the broadcast */
  MPI_Bcast( *string, string_len + 1, MPI_CHAR, root_rank, comm );

  (*string)[string_len] = '\0';	/* paranoid safeguard */

  return 0;
}


static int
broadcast_char_array (char string[], size_t len, int root_rank, MPI_Comm comm)
{
  int ret    = -1;
  char* sptr = NULL;

  if (root_rank == myID) {
    string[len - 1] = '\0';	/* safeguard */
    sptr = string;
  }

  ret = broadcast_string( &sptr, root_rank, comm );

  if (0 == ret && root_rank != myID) {
    strncpy( string, sptr, len );
    string[len-1] = '\0'; /* safeguard */
    xfree_char( &sptr );
  }

  return ret;
}




static inline int
monitor_print( const char* format, ... )
{
  int ret = 0;

  if (format != NULL) {
    va_list args;

    htimerv_start( &timer_monitor );
    va_start( args, format );

    if (theMonitorFileFp == NULL) {
      ret = vfprintf( stderr, format, args );
    } else {
      ret = hu_print_tee_va( theMonitorFileFp, format, args );
    }

    va_end( args );
    htimerv_stop( &timer_monitor );
  }

  return ret;
}





/*-----------Parameter parsing routines---------------------------------*/

/**
 * Initialize parameters, replicate database and open database.
 *
 * \param argc Argument count (as in \c main).
 * \param argv Argument character array (as in \c main).
 */
static void
local_init (int argc, char** argv)
{
  int32_t namelen[2];
  double  double_message[16];
  int     int_message[9];

  myOctree = NULL;
  myMesh   = NULL;
  mySolver = NULL;
  thePtSrc = NULL;

  /* Show usage if the number of parameters is wrong */

  if (argc != 6) {
    if (myID == 0) {
      fputs ( "Usage: psolve <cvmdb> <physics.in> <numerical.in> "
	      "<meshetree> <4D-output>\n"
	      "  cvmdb:        path to a CVM etree or a flat file.\n"
	      "  physics.in:   path to physics.in.\n"
	      "  numerical.in: path to numerical.in.\n"
	      "  meshetree:    path to the output mesh etree.\n"
	      "  4D-output:    path to the 4D simulation results.\n\n",
	      stderr);
    }
	
    MPI_Finalize();
    exit(1);
  }


  /* Broadcast the names of the output etree and 4-d output file */
  /* jclopez: Question, why are these two names broadcasted if they are passed
   * in the command line arguments anyway ???
   */
  if (myID == 0) {
    /* string len + trailing null */
    namelen[0] = strlen(argv[4]) + 1;
    namelen[1] = strlen(argv[5]) + 1;
  }

  MPI_Bcast(namelen, 2, MPI_INT, 0, MPI_COMM_WORLD);

  theMEtree = (char *)calloc(namelen[0], 1);
  if (theMEtree == NULL) {
    fprintf(stderr, "Thread %d: local_init: out of memory\n", myID);
    MPI_Abort(MPI_COMM_WORLD, ERROR);
    exit(1);
  }

  theOutFile = (char *)calloc(namelen[1], 1);
  if (theOutFile == NULL) {
    fprintf(stderr, "Thread %d: local_init: out of memory\n", myID);
    MPI_Abort(MPI_COMM_WORLD, ERROR);
    exit(1);
  }

  if (myID == 0) {
    strcpy(theMEtree, argv[4]);
    strcpy(theOutFile, argv[5]);
  }

  MPI_Bcast(theMEtree, namelen[0], MPI_CHAR, 0, MPI_COMM_WORLD);
  MPI_Bcast(theOutFile, namelen[1], MPI_CHAR, 0, MPI_COMM_WORLD);

  /* Mark 4d output file as unopened */
  theOutFp = NULL;

#ifdef USECVMDB
  MPI_Barrier(MPI_COMM_WORLD);
  theDBReplicateTime = -MPI_Wtime();

  replicateDB(argv[1]);

  MPI_Barrier(MPI_COMM_WORLD);
  theDBReplicateTime += MPI_Wtime();

  /* Not required for input in geodata */
  if(theGeodataSwitch == 0)
    openDB(argv[1]);
#else
  strcpy(theCVMFlatFile, argv[1]);
#endif

  if (myID == 0) {
    if (initparameters(argv[2], argv[3]) != 0) {
      fprintf(stderr, "Thread 0: local_init: error initparameters\n");
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }

    /* read simulation output parameters */
  }

  double_message[0]  = theVsCut;
  double_message[1]  = theFactor;
  double_message[2]  = theFreq;
  double_message[3]  = theDeltaT;
  double_message[4]  = theDeltaTSquared;
  double_message[5]  = theEndT;
  double_message[6]  = theStartT;
  double_message[7]  = theDomainX;
  double_message[8]  = theDomainY;
  double_message[9]  = theDomainZ;
  double_message[10] = theDomainAzimuth;
  double_message[11] = theThresholdDamping;
  double_message[12] = theThresholdVpVs;

  MPI_Bcast(double_message, 13, MPI_DOUBLE, 0, MPI_COMM_WORLD);

  theVsCut            = double_message[0];
  theFactor           = double_message[1];
  theFreq             = double_message[2];
  theDeltaT           = double_message[3];
  theDeltaTSquared    = double_message[4];
  theEndT             = double_message[5];
  theStartT           = double_message[6];
  theDomainX          = double_message[7];
  theDomainY          = double_message[8];
  theDomainZ          = double_message[9];
  theDomainAzimuth    = double_message[10];
  theThresholdDamping = double_message[11];
  theThresholdVpVs    = double_message[12];

#ifdef USECVMDB

  double_message[13] = theXForMeshOrigin;
  double_message[14] = theYForMeshOrigin;
  double_message[15] = theZForMeshOrigin;

  MPI_Bcast(&double_message[13], 3, MPI_DOUBLE, 0, MPI_COMM_WORLD);

  theXForMeshOrigin = double_message[13];
  theYForMeshOrigin = double_message[14];
  theZForMeshOrigin = double_message[15];

#endif

  int_message[0] = theTotalSteps;
  int_message[1] = theRate;
  int_message[2] = theNumberOfPlanes;
  int_message[3] = theNumberOfStations;
  int_message[4] = (int)theTypeOfDamping;
  int_message[5] = theDampingStatisticsFlag;
  int_message[6] = theMeshOutFlag;
  int_message[7] = theCheckPointingRate;
  int_message[8] = theUseCheckPoint;

  MPI_Bcast(int_message, 9, MPI_INT, 0, MPI_COMM_WORLD);

  theTotalSteps            = int_message[0];
  theRate                  = int_message[1];
  theNumberOfPlanes        = int_message[2];
  theNumberOfStations      = int_message[3];
  theTypeOfDamping         = int_message[4];
  theDampingStatisticsFlag = int_message[5];
  theMeshOutFlag           = int_message[6];
  theCheckPointingRate     = int_message[7]; 
  theUseCheckPoint         = int_message[8];

  MPI_Bcast (theCheckPointingDirOut, 256, MPI_CHAR, 0, MPI_COMM_WORLD);
  MPI_Bcast ( theDatabasePath, 256, MPI_CHAR, 0, MPI_COMM_WORLD);

  /* Assign the theDeltaT to theGlobalDeltaT for vis */
  theGlobalDeltaT   = theDeltaT;

  /* Broad cast the trim parameters */
  MPI_Bcast (&theLatUMesh, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
  MPI_Bcast (&theLonUMesh, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
  MPI_Bcast (&theLatLMesh, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
  MPI_Bcast (&theLonLMesh, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
  MPI_Bcast (&theDepthUMesh, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
  MPI_Bcast (&theDepthLMesh, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
  /* Broadcast corners information */


  MPI_Bcast (&theSurfaceCornersLong,4,MPI_DOUBLE,0,MPI_COMM_WORLD);
  MPI_Bcast (&theSurfaceCornersLat ,4,MPI_DOUBLE,0,MPI_COMM_WORLD);


  return;

}



static void
local_finalize()
{
  /* Free memory held by file name strings */
  if (theMEtree != NULL) {
    free(theMEtree);
  }

  if (theOutFile != NULL) {
    free(theOutFile);
  }

  /* Free memory associated with the octree and mesh */
  octor_deletetree(myOctree);
  octor_deletemesh(myMesh);

  /* Free the memory used by the source */
  free(myForces);

  if (myVelocityTable != NULL)
    free(myVelocityTable);

  /* Free memory associated with the solver */
  solver_delete();

  /* Free memory associated with output planes */
  /* MISSING */

  return;
}


/**
 * Parse a text file and return the value of a match string.
 *
 * \return 0 if OK, -1 on error.
 */
int
parsetext (FILE* fp, const char* querystring, const char type, void* result)
{
  const static char delimiters[] = " =\n\t";

  int32_t res = 0, found = 0;

  /* Start from the beginning */
  rewind(fp);


  /* Look for the string until found */
  while (!found) {
    char line[LINESIZE];
    char *name, *value;

    /* Read in one line */
    if (fgets(line, LINESIZE, fp) == NULL)
      break;

    name = strtok(line, delimiters);
    if ((name != NULL) && (strcmp(name, querystring) == 0)) {
      found = 1;
      value = strtok(NULL, delimiters);

      switch (type) {
      case 'i':
	res = sscanf(value, "%d", (int *)result);
	break;
      case 'f':
	res = sscanf(value, "%f", (float *)result);
	break;
      case 'd':
	res = sscanf(value, "%lf", (double *)result);
	break;
      case 's':
	res = 1;
	strcpy((char *)result, value);
	break;
      case 'u':
	res = sscanf(value, "%u", (uint32_t *)result);
	break;
      default:
	fprintf(stderr, "parsetext: unknown type %c\n", type);
	return -1;
      }
    }

  }

  return (res == 1) ? 0 : -1;
}




/**
 * This is like \c parsetext with the following differences:
 * - works only for strings;
 * - avoids writting past the end of the string;
 * - return convention is different, it distinguishes between "key not found"
 *   and other type of errors.
 *
 * \return
 *	1 if the key name is found and the value is stored in \c value;
 *	0 if the key name was not found; -1 on error.
 */
static int
read_config_string (FILE* fp, const char* key, char* value_ptr, size_t size)
{
  static const char delimiters[] = " =\n\t";

  int  ret;
  char line[LINESIZE];
  char state[LINESIZE];
  char *name, *value, *state_ptr;


  HU_ASSERT_PTR_ALWAYS( fp );
  HU_ASSERT_PTR_ALWAYS( value_ptr );
  HU_ASSERT_PTR_ALWAYS( key );

  rewind (fp);
  ret   = 0;
  *value_ptr = '\0';

  while (0 == ret && !ferror (fp)) {

    if (fgets (line, LINESIZE, fp) == NULL) {
      if (!feof (fp)) {
	ret = -1;	/* input error */
      }
      break;
    }

    state_ptr = state;
    name      = strtok_r (line, delimiters, &state_ptr);

    if ((name != NULL) && (strcmp (name, key) == 0)) {
      size_t value_len;

      value = strtok_r (NULL, delimiters, &state_ptr);

      if (NULL != value) {
	value_len = strlen (value);

	if (value_len >= size) {
	  ret = -2;	/* return buffer is too short */
	} else {
	  strncpy (value_ptr, value, size);
	  ret = 1;
	}
      }

      break;
    }
  }

  return ret;
}


/**
 * Open material database and initialize various static global variables.
 *
 * \return 0 if OK, -1 on error.
 */
static int32_t
initparameters(const char *physicsin, const char *numericalin)
{
  FILE     *fp;
  int32_t   samples, rate;
  int       number_output_planes, number_output_stations,output_stations_inputtype,
    damping_statistics, use_checkpoint, checkpointing_rate;

  double    freq, vscut,
    region_origin_latitude_deg, region_origin_longitude_deg,
    region_azimuth_leftface_deg,
    region_depth_shallow_m, region_length_east_m,
    region_length_north_m, region_depth_deep_m,
    startT, endT, deltaT,
    threshold_damping, threshold_VpVs;
  char      type_of_damping[64],
    checkpoint_path[256],
    database_path[256];


  damping_type_t typeOfDamping = -1;

#ifdef USECVMDB
  dbctl_t  *myctl;
#endif

  /* Obtain the specficiation of the simulation */
  if ((fp = fopen(physicsin, "r")) == NULL)
    {
      fprintf(stderr, "Error opening %s\n", physicsin);
      return -1;
    }

  if ((parsetext(fp, "region_origin_latitude_deg", 'd',
		 &region_origin_latitude_deg) != 0) ||
      (parsetext(fp, "region_origin_longitude_deg", 'd',
		 &region_origin_longitude_deg) != 0) ||
      (parsetext(fp, "region_depth_shallow_m", 'd',
		 &region_depth_shallow_m) != 0) ||
      (parsetext(fp, "region_length_east_m", 'd',
		 &region_length_east_m) != 0) ||
      (parsetext(fp, "region_length_north_m", 'd',
		 &region_length_north_m) != 0) ||
      (parsetext(fp, "region_depth_deep_m", 'd',
		 &region_depth_deep_m) != 0) ||
      (parsetext(fp, "region_azimuth_leftface_deg", 'd',
		 &region_azimuth_leftface_deg)) ||
      (parsetext(fp, "type_of_damping", 's', &type_of_damping) != 0) )
    {
      fprintf(stderr, "Error reading region origin from %s\n", physicsin);
      return -1;
    }


  if ((parsetext(fp, "lat_u", 'd', &theLatUMesh) != 0) ||
      (parsetext(fp, "lat_l", 'd', &theLatLMesh) != 0) ||
      (parsetext(fp, "lon_u", 'd', &theLonUMesh) != 0) ||
      (parsetext(fp, "lon_l", 'd', &theLonLMesh) != 0) ||
      (parsetext(fp, "depth_u", 'd', &theDepthUMesh) != 0) ||
      (parsetext(fp, "depth_l", 'd',  &theDepthLMesh) != 0))
    {
      fprintf(stderr, "Error reading mesh trim parameters %s\n", physicsin);
      return -1;
    }


  if ( strcasecmp(type_of_damping, "rayleigh") == 0)

    typeOfDamping = RAYLEIGH;

  else if (strcasecmp(type_of_damping, "mass") == 0)

    typeOfDamping = MASS;

  else if (strcasecmp(type_of_damping, "none") == 0)

    typeOfDamping = NONE;

  else {
    solver_abort( __FUNCTION_NAME, NULL, "Unknown damping type: %s\n",
		  type_of_damping );
  }

  fclose(fp); /* physics.in */

  if ((fp = fopen(numericalin, "r")) == NULL) {
    fprintf(stderr, "Error opening %s\n", numericalin);
    return -1;
  }

  if ( parsetext(fp, "monitor_file", 's', &theMonitorFileName) != 0 ) {
    fprintf( stderr, "Error reading monitor file name from %s\n",
	     numericalin );
    return -1;
  }

  /* Open the monitor file of the simulation in processor 0*/
  theMonitorFileFp = fopen( theMonitorFileName, "w" );
  if (theMonitorFileFp == NULL) {
    fprintf( stderr,"\n Err opening the monitor file" );
  } else {
    setlinebuf ( theMonitorFileFp );
  }
    
  /* numerical.in parse texts */
  if ((parsetext( fp, "simulation_wave_max_freq_hz", 'd', &freq)              != 0) ||
      (parsetext( fp, "simulation_node_per_wavelength", 'i', &samples)        != 0) ||
      (parsetext( fp, "simulation_shear_velocity_min", 'd', &vscut)           != 0) ||
      (parsetext( fp, "simulation_start_time_sec", 'd', &startT)              != 0) ||
      (parsetext( fp, "simulation_end_time_sec", 'd', &endT)                  != 0) ||
      (parsetext( fp, "simulation_delta_time_sec", 'd', &deltaT)              != 0) ||
      (parsetext( fp, "simulation_output_rate", 'i', &rate)                   != 0) ||
      (parsetext( fp, "number_output_planes", 'i', &number_output_planes)     != 0) ||
      (parsetext( fp, "number_output_stations", 'i', &number_output_stations) != 0) ||
      (parsetext( fp, "the_threshold_damping", 'd', &threshold_damping)       != 0) ||
      (parsetext( fp, "the_threshold_Vp_over_Vs", 'd', &threshold_VpVs)       != 0) ||
      (parsetext( fp, "do_damping_statistics", 'i', &damping_statistics)      != 0) ||
      (parsetext( fp, "use_checkpoint", 'i', &use_checkpoint)                 != 0) ||
      (parsetext( fp, "checkpointing_rate", 'i', &checkpointing_rate)         != 0) ||
      (parsetext( fp, "checkpoint_path", 's', &checkpoint_path)               != 0) ||
      (parsetext( fp, "database_path", 's', &database_path)               != 0) )
    {
      fprintf( stderr, "Error parsing simulation parameters from %s\n",
	       numericalin );
      return -1;
    }



  if(myID==0){
    double *auxiliar;
    int iCorner;
    static const char* fname = "output_stations_init()";

    auxiliar = (double *)malloc(sizeof(double)*8);
	
    if ( parsedarray( fp, "domain_surface_corners", 8 ,auxiliar) !=0 ) {
      solver_abort (fname, NULL, "Error parsing domain_surface_corners field from %s\n",
		    numericalin);
    }
	
    for ( iCorner = 0; iCorner < 4; iCorner++){
      theSurfaceCornersLong[ iCorner ] = auxiliar [ iCorner * 2 ];
      theSurfaceCornersLat [ iCorner ] = auxiliar [ iCorner * 2 +1 ];
    }
    free(auxiliar);

  }


  hu_config_get_int_opt( fp, "output_mesh", &theMeshOutFlag );
  hu_config_get_int_opt( fp, "schedule_print_error_check", &theSchedulePrintErrorCheckFlag);
  hu_config_get_int_opt( fp, "enable_timing_barriers", &theTimingBarriersFlag );
  hu_config_get_int_opt( fp, "forces_buffer_size", &theForcesBufferSize );

  fclose( fp );

  /* Sanity check */

  if (freq <= 0) {
    fprintf(stderr, "Illegal frequency value %f\n", freq);
    return -1;
  }

  if (samples <= 0) {
    fprintf(stderr, "Illegal samples value %d\n", samples);
    return -1;
  }

  if (vscut <= 0) {
    fprintf(stderr, "Illegal vscut value %f\n", vscut);
    return -1;
  }

  if ((startT < 0) || (endT < 0) || (startT > endT)) {
    fprintf(stderr, "Illegal startT %f or endT %f\n", startT, endT);
    return -1;
  }

  if (deltaT <= 0) {
    fprintf(stderr, "Illegal deltaT %f\n", deltaT);
    return -1;
  }

  if (rate <= 0) {
    fprintf(stderr, "Illegal output rate %d\n", rate);
    return -1;
  }

  if (number_output_planes < 0) {
    fprintf(stderr, "Illegal number of output planes %d\n",
	    number_output_planes);
    return -1;
  }

  if (number_output_stations < 0) {
    fprintf(stderr, "Illegal number of output stations %d\n",
	    number_output_planes);
    return -1;
  }


  if (threshold_damping < 0) {
    fprintf(stderr, "Illegal threshold damping %f\n",
	    threshold_damping);
    return -1;
  }

  if (threshold_VpVs < 0) {
    fprintf(stderr, "Illegal threshold Vp over Vs %f\n",
	    threshold_VpVs);
    return -1;
  }

  if ( (damping_statistics < 0) || (damping_statistics > 1) ) {
    fprintf(stderr, "Illegal do damping statistics flag %d\n",
	    damping_statistics);
    return -1;
  }

  if ( (use_checkpoint < 0) || (use_checkpoint > 1) ) {
    fprintf(stderr, "Illegal use checkpoint flag %d\n",
	    use_checkpoint);
    return -1;
  }

  if ( checkpointing_rate < 0 ) {
    fprintf(stderr, "Illegal checkpointing rate %d\n",
	    use_checkpoint);
    return -1;
  }

#ifdef USECVMDB
  /* Obtain the material database application control/meta data */
  /* Not necessary for geodata input : comment by Leo*/
  if(theGeodataSwitch == 0){
    if ((myctl = cvm_getdbctl(theCVMEp)) == NULL) {
      fprintf(stderr, "Error reading CVM etree control data\n");
      return -1;
    }

  }
  /* Check the ranges of the mesh and the scope of the CVM etree */
  /* if ((region_origin_latitude_deg < myctl->region_origin_latitude_deg) ||
     (region_origin_longitude_deg < myctl->region_origin_longitude_deg) ||
     (region_depth_shallow_m < myctl->region_depth_shallow_m) ||
     (region_depth_deep_m > myctl->region_depth_deep_m) ||
     (region_origin_latitude_deg + region_length_north_m / DIST1LAT
     > myctl->region_origin_latitude_deg
     + myctl->region_length_north_m / DIST1LAT) ||
     (region_origin_longitude_deg + region_length_east_m / DIST1LON
     > myctl->region_origin_longitude_deg +
     myctl->region_length_east_m / DIST1LON)) {
     fprintf(stderr, "Mesh area out of the CVM etree\n");
     return -1;
     }*/

  /* Compute the coordinates of the origin of the mesh coordinate
     system in the CVM etree domain coordinate system */
  if(theGeodataSwitch ==0 ){
    theXForMeshOrigin = (region_origin_latitude_deg
			 - myctl->region_origin_latitude_deg) * DIST1LAT;
    theYForMeshOrigin = (region_origin_longitude_deg
			 - myctl->region_origin_longitude_deg) * DIST1LON;
    theZForMeshOrigin = region_depth_shallow_m - myctl->region_depth_shallow_m;


    /* Free memory used by myctl */
    cvm_freedbctl(myctl);
  }else{

    theXForMeshOrigin = 0;
    theYForMeshOrigin = 0;
    theZForMeshOrigin = 0;
  }

#endif

  /* Init the static global variables */

  theRegionLat      = region_origin_latitude_deg;
  theRegionLong     = region_origin_longitude_deg ;
  theRegionDepth    = region_depth_shallow_m ;

  theVsCut          = vscut;
  theFactor         = freq * samples*1;
  theFreq           = freq;
  theDeltaT         = deltaT;
  theDeltaTSquared  = deltaT * deltaT;
  theStartT         = startT;
  theEndT           = endT;
  theTotalSteps     = (int)(((endT - startT) / deltaT));

  theDomainX        = region_length_north_m;
  theDomainY        = region_length_east_m;
  theDomainZ        = region_depth_deep_m - region_depth_shallow_m;
  theDomainAzimuth  = region_azimuth_leftface_deg;
  theTypeOfDamping  = typeOfDamping;

  theRate           = rate;

  theNumberOfPlanes        = number_output_planes;
  theNumberOfStations      = number_output_stations;

  theThresholdDamping      = threshold_damping;
  theThresholdVpVs         = threshold_VpVs;
  theDampingStatisticsFlag = damping_statistics;

  theCheckPointingRate     = checkpointing_rate;
  theUseCheckPoint         = use_checkpoint;

  strcpy( theCheckPointingDirOut, checkpoint_path );
  strcpy( theDatabasePath, database_path);


  return 0;

}



/*-----------Mesh generation related routines------------------------------*/


#ifdef USECVMDB

/**
 * replicateDB: Copy the material database to local disks.
 *
 */
static void
replicateDB(const char *dbname)
{
  char* destdir;
  char  curdir[256];

#ifndef SCEC
  char* srcpath;
  MPI_Comm replica_comm;
#endif /* SCEC */


  /* Change the working directory to $LOCAL */
#ifndef CVM_DESTDIR
  destdir = getenv( "CVM_DESTDIR" );
  if (destdir == NULL) { /* then use current directory */
    destdir = getcwd( curdir, 256 );
  }
#else
  destdir = CVM_DESTDIR;
#endif

  /* Clean the disks:
   * NOTE: Guys! cleanup the disk in your job script before launching
   * psolve, using rm -rf.
   * E.g., on Lemieux add the following line to your PBS job script,
   * before launching psolve.
   *   prun -m cyclic -n ${RMS_NODES} -N ${RMS_NODES} rm -rf <dirname>/
   * where dirname is the directory you want to wipe out.
   * This will take care of this issue.
   *
   * On BigBen it is even easier, it can be done from the front end
   * since everything is a shared parallel file system.
   *
   * Unfortunately the 'system' function is not supported on all platforms,
   * e.g., Cray's XT3 catamount platform (BigBen) does not have it.
   */
#ifndef SCEC
  if (chdir(destdir) != 0) {
    fprintf(stderr, "Thread %d: replicateDB: cannot chdir to %s\n",
	    myID, destdir);
    MPI_Abort(MPI_COMM_WORLD, ERROR);
    exit(1);
  }

  MPI_Barrier(MPI_COMM_WORLD);

  /* Replicate the material database among the processors */
  if (myID % PROCPERNODE != 0) {
    MPI_Comm_split(MPI_COMM_WORLD, MPI_UNDEFINED, myID, &replica_comm);

  } else {
    int replica_id;
    off_t filesize, remains, batchsize;
    void *filebuf;
    int src_fd = -1, dest_fd;

    MPI_Comm_split(MPI_COMM_WORLD, 0, myID, &replica_comm);
    MPI_Comm_rank(replica_comm, &replica_id);

    if (replica_id == 0) {
      struct stat statbuf;

#ifndef CVM_SRCPATH
      srcpath = getenv("CVM_SRCPATH");
#else
      srcpath = CVM_SRCPATH;
#endif

      if (stat(srcpath, &statbuf) != 0) {
	fprintf(stderr,
		"Thread 0: replicateDB: Cannot get stat of %s\n",
		srcpath);
	MPI_Abort(MPI_COMM_WORLD, ERROR);
	exit(1);
      }

      filesize = statbuf.st_size;
      src_fd = open(srcpath, O_RDONLY);
      if (src_fd == -1) {
	fprintf(stderr,
		"Thread 0: replicateDB: Cannot open cvm source db\n");
	MPI_Abort(MPI_COMM_WORLD, ERROR);
	exit(1);
      }
    }


    MPI_Bcast(&filesize, sizeof(off_t), MPI_CHAR, 0, replica_comm);
    theDBSize = filesize;

    if ((filebuf = malloc(FILEBUFSIZE)) == NULL) {
      fprintf(stderr, "Thread %d: replicateDB: ", myID);
      fprintf(stderr, "run out of memory while ");
      fprintf(stderr, "preparing to receive material database\n");
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }

    /* Everyone opens a replicate db */
    dest_fd = open(dbname, O_CREAT|O_TRUNC|O_WRONLY, S_IRUSR|S_IWUSR);
    if (dest_fd == -1) {
      fprintf(stderr, "Thread %d: replicateDB: ", myID);
      fprintf(stderr, "cannot create replica database\n");
      perror("open");
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }

    remains = filesize;
    while (remains > 0) {
      batchsize = (remains > FILEBUFSIZE) ? FILEBUFSIZE : remains;

      if (replica_id == 0) {
	if (read(src_fd, filebuf, batchsize) !=  batchsize) {
	  fprintf(stderr, "Thread 0: replicateDB: ");
	  fprintf(stderr, "Cannot read database\n");
	  perror("read");
	  MPI_Abort(MPI_COMM_WORLD, ERROR);
	  exit(1);
	}
      }

      MPI_Bcast(filebuf, batchsize, MPI_CHAR, 0, replica_comm);

      if (write(dest_fd, filebuf, batchsize) != batchsize) {
	fprintf(stderr, "Thread %d: replicateDB: ", myID);
	fprintf(stderr, "Cannot write replica database\n");
	MPI_Abort(MPI_COMM_WORLD, ERROR);
	exit(1);
      }

      remains -= batchsize;
    }

    free(filebuf);

    if (close(dest_fd) != 0) {
      fprintf(stderr, "Thread %d: replicateDB: ", myID);
      fprintf(stderr, "cannot close replica database\n");
      perror("close");
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }

    if (replica_id == 0) {
      close(src_fd);
    }

    MPI_Comm_free(&replica_comm);

  } /* processors participating in the replication */

#endif /* SCEC */

  return ;
}


/**
 * openDB: Open my local copy of the material database.
 *
 */
static void
openDB(const char *dbname)
{
  theCVMEp = etree_open(dbname, O_RDONLY, CVMBUFSIZE, 0, 0);
  if (theCVMEp == NULL) {
    fprintf(stderr, "Thread %d: openDB: error opening CVM etree %s\n",
	    myID, dbname);
    MPI_Abort(MPI_COMM_WORLD, ERROR);
    exit(1);
  }

  return;
}



/**
 * setrec: Assign values (material properties) to a leaf octant specified
 *         by octleaf.
 *
 */
static void
setrec(octant_t *leaf, double ticksize, void *data)
{
  edata_t *edata;
  double east_m, north_m, depth_m;
  double vp,vs,rho,qs,qp;
  double vpovervs,vpvsratioAddfactor;
  int layertype;
  double lon, lat,layerFactor,theTrueFactor;
  tick_t halfticks;
  int32_t res;
  int containingunit,iDepth,iRef;
  double signum[2];
  double lon0[4],lat0[4],lonTest,latTest,dist;

  struct timeval stime, etime;
  signum[0]=1;
  signum[1]=-1;

  edata = (edata_t *)data;


  halfticks = (tick_t)1 << (PIXELLEVEL - leaf->level - 1);

  edata->edgesize = ticksize * halfticks * 2;

  north_m = theXForMeshOrigin + (leaf->lx +theMeshCriteriaNorth* halfticks) * ticksize;    
  east_m  = theYForMeshOrigin + (leaf->ly +theMeshCriteriaEast* halfticks) * ticksize;    
     

  compute_lonlat_from_domain_coords_linearinterp( east_m , north_m , &lon, &lat, 
						  theSurfaceCornersLong,
						  theSurfaceCornersLat,
						  theDomainX, theDomainY); 
    
  gettimeofday(&stime, NULL);
  
  edata->Vp  = 0; 
  edata->Vs  = 0;
  edata->rho = 0;
  
  //  for (iDepth=0; iDepth<2;iDepth++){
  depth_m = theZForMeshOrigin + (leaf->lz +theMeshCriteriaZ*halfticks) * ticksize;//-(edata->edgesize);
      //  +(edata->edgesize)*.45*signum[iDepth];

  
    
    if(doIIncludeTopo==1){
      depth_m=database.datum-depth_m;
      res=Single_Search(lat, depth_m, lon,&vp, &vs, &rho,&containingunit,&qp,&qs,&database,0,1.);
    }else
      res=Single_Search(lat, depth_m+.001, lon,&vp, &vs, &rho,&containingunit,&qp,&qs,&database,1,1.);


    //    fprintf(stdout,"\n %lf %lf %lf %lf %d %lf, res=%d ",east_m , north_m,depth_m,ticksize,myID,database.depthmax,res);
    
    /* Sanity check*/

    if((vs<=0) ||(vp<=0) ||(rho<=0)){
      fprintf(stdout,"\n the vs or vp or rho is 0 or negative");
      fprintf(stdout,"\n lon=%lf lat=%lf depth=%lf",lon,lat,depth_m);
      fprintf(stdout,"\n vp=%lf vs=%lf rho=%lf",vp,vs,rho);
      fflush(stdout);
      if(doIIncludeTopo==1)
	Single_Search(lat, depth_m+.001, lon,&vp, &vs, &rho,&containingunit,&qp,&qs,&database,0,1.);


      exit(1);
    }


    layertype=(database.geologicunits[containingunit]).layertype;

    edata->Vp  = edata->Vp + vp; 
    edata->Vs  = edata->Vs + vs;
    edata->rho = edata->rho+ rho;
    
    
  if(vs==0)
    vpovervs=1;
  else
    vpovervs=vp/vs; 
  
  gettimeofday(&etime, NULL);

  /* Update the timing stats */
  if (theCVMQueryStage == 0) {
    theCVMQueryTime_Refinement += DIFFTIME(etime, stime);
    theCVMQueryCount_Refinement++;
  } else if (theCVMQueryStage == 1) {
    theCVMQueryTime_Balance += DIFFTIME(etime, stime);
    theCVMQueryCount_Balance++;
  }

    vpvsratioAddfactor=1;
    theTrueFactor=theFactor *vpvsratioAddfactor;

  if (res != 0) {
    /* Center point out the bound. Set Vs to force split */
    edata->Vs = theTrueFactor * edata->edgesize / 2;

  } else {
    /* Adjust the Vs */
    edata->Vs = (edata->Vs < theVsCut) ? theVsCut : edata->Vs;
  }

  return;
}


#else /* USECVMDB */

static int32_t
zsearch(void *base, int32_t count, int32_t recordsize,
        const point_t *searchpt)
{
  int32_t start, end, offset, found;

  start = 0;
  end = count - 1;
  offset = (start + end ) / 2;

  found = 0;
  do {
    if (end < start) {
      /* the two pointer crossed each other */
      offset = end;
      found = 1;
    } else {
      const void *pivot = (char *)base + offset * recordsize;

      switch (octor_zcompare(searchpt, pivot)) {
      case (0): /* equal */
	found = 1;
	break;
      case (1): /* searchpoint larger than the pivot */
	start = offset + 1;
	offset = (start + end) / 2;
	break;
      case (-1): /* searchpoint smaller than the pivot */
	end = offset - 1;
	offset = (start + end) / 2;
	break;
      }
    }
  } while (!found);

  return offset;
}


static cvmrecord_t *sliceCVM(const char *cvm_flatfile)
{
  cvmrecord_t *cvmrecord;
  int32_t bufferedbytes, bytecount, recordcount;
  if (myID == theGroupSize - 1) {
    /* the last processor reads data and
       distribute to other processors*/

    struct timeval starttime, endtime;
    float iotime = 0, memmovetime = 0;
    MPI_Request *isendreqs;
    MPI_Status *isendstats;
    FILE *fp;
    int fd, procid;
    struct stat statbuf;
    void *maxbuf;
    const point_t *intervaltable;
    off_t bytesent;
    int32_t offset;
    const int maxcount =  (1 << 29) / sizeof(cvmrecord_t);
    const int maxbufsize = maxcount * sizeof(cvmrecord_t);

    fp = fopen(cvm_flatfile, "r");
    if (fp == NULL) {
      fprintf(stderr, "Thread %d: Cannot open flat CVM file\n", myID);
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }

    fd = fileno(fp);
    if (fstat(fd, &statbuf) != 0) {
      fprintf(stderr, "Thread %d: Cannot get the status of CVM file\n",
	      myID);
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }

    intervaltable = octor_getintervaltable(myOctree);

    /*
      for (procid = 0; procid <= myID; procid++) {
      fprintf(stderr, "interval[%d] = {%d, %d, %d}\n", procid,
      intervaltable[procid].x << 1, intervaltable[procid].y << 1,
      intervaltable[procid].z << 1);
      }
    */

    bytesent = 0;
    maxbuf = malloc(maxbufsize) ;
    if (maxbuf == NULL) {
      fprintf(stderr, "Thread %d: Cannot allocate send buffer\n", myID);
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }

    isendreqs = (MPI_Request *)malloc(sizeof(MPI_Request) * theGroupSize);
    isendstats = (MPI_Status *)malloc(sizeof(MPI_Status) * theGroupSize);
    if ((isendreqs == NULL) || (isendstats == NULL)) {
      fprintf(stderr, "Thread %d: Cannot allocate isend controls\n",
	      myID);
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }

    /* Try to read max number of CVM records as allowed */
    gettimeofday(&starttime, NULL);
    recordcount = fread(maxbuf, sizeof(cvmrecord_t),
			maxbufsize / sizeof(cvmrecord_t), fp);
    gettimeofday(&endtime, NULL);

    iotime += (endtime.tv_sec - starttime.tv_sec) * 1000.0
      + (endtime.tv_usec - starttime.tv_usec) / 1000.0;

    if (recordcount != maxbufsize / sizeof(cvmrecord_t)) {
      fprintf(stderr, "Thread %d: Cannot read-init buffer\n", myID);
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }

    /* start with proc 0 */
    procid = 0;

    while (procid < myID) { /* repeatedly fill the buffer */
      point_t searchpoint, *point;
      int newreads;
      int isendcount = 0;

      /* we have recordcount to work with */
      cvmrecord = (cvmrecord_t *)maxbuf;

      while (procid < myID) { /* repeatedly send out data */

	searchpoint.x = intervaltable[procid + 1].x << 1;
	searchpoint.y = intervaltable[procid + 1].y << 1;
	searchpoint.z = intervaltable[procid + 1].z << 1;

	offset = zsearch(cvmrecord, recordcount, theCVMRecordSize,
			 &searchpoint);

	point = (point_t *)(cvmrecord + offset);

	if ((point->x != searchpoint.x) ||
	    (point->y != searchpoint.y) ||
	    (point->z != searchpoint.z)) {
	  break;
	} else {
	  bytecount = offset * sizeof(cvmrecord_t);
	  MPI_Isend(cvmrecord, bytecount, MPI_CHAR, procid,
		    CVMRECORD_MSG, MPI_COMM_WORLD,
		    &isendreqs[isendcount]);
	  isendcount++;

	  /*
	    fprintf(stderr,
	    "Procid = %d offset = %qd bytecount = %d\n",
	    procid, (int64_t)bytesent, bytecount);
	  */

	  bytesent += bytecount;

	  /* prepare for the next processor */
	  recordcount -= offset;
	  cvmrecord = (cvmrecord_t *)point;
	  procid++;
	}
      }

      /* Wait till the data in the buffer has been sent */
      MPI_Waitall(isendcount, isendreqs, isendstats);

      /* Move residual data to the beginning of the buffer
	 and try to fill the newly free space */
      bufferedbytes = sizeof(cvmrecord_t) * recordcount;

      gettimeofday(&starttime, NULL);
      memmove(maxbuf, cvmrecord, bufferedbytes);
      gettimeofday(&endtime, NULL);
      memmovetime += (endtime.tv_sec - starttime.tv_sec) * 1000.0
	+ (endtime.tv_usec - starttime.tv_usec) / 1000.0;

      gettimeofday(&starttime, NULL);
      newreads = fread((char *)maxbuf + bufferedbytes,
		       sizeof(cvmrecord_t), maxcount - recordcount, fp);
      gettimeofday(&endtime, NULL);
      iotime += (endtime.tv_sec - starttime.tv_sec) * 1000.0
	+ (endtime.tv_usec - starttime.tv_usec) / 1000.0;

      recordcount += newreads;

      if (newreads == 0)
	break;
    }

    free(maxbuf);
    free(isendreqs);
    free(isendstats);

    /* I am supposed to accomodate the remaining octants */
    bytecount = statbuf.st_size - bytesent;

    cvmrecord = (cvmrecord_t *)malloc(bytecount);
    if (cvmrecord == NULL) {
      fprintf(stderr, "Thread %d: out of memory\n", myID);
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }

    /* fseek exiting the for loop has file cursor propertly */
    if (fseeko(fp, bytesent, SEEK_SET) != 0) {
      fprintf(stderr, "Thread %d: fseeko failed\n", myID);
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }

    gettimeofday(&starttime, NULL);
    if (fread(cvmrecord, 1, bytecount, fp) != (size_t)bytecount) {
      fprintf(stderr, "Thread %d: fail to read the last chunk\n",
	      myID);
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }
    gettimeofday(&endtime, NULL);
    iotime += (endtime.tv_sec - starttime.tv_sec) * 1000.0
      + (endtime.tv_usec - starttime.tv_usec) / 1000.0;

    /*
      fprintf(stderr, "Procid = %d offset = %qd bytecount = %d\n",
      myID, (int64_t)bytesent, bytecount);
    */

    fclose(fp);

    fprintf(stdout, "Read %s (%.2fMB) in %.2f seconds (%.2fMB/sec)\n",
	    cvm_flatfile, (float)statbuf.st_size / (1 << 20),
	    iotime / 1000,
	    (float)statbuf.st_size / (1 << 20) / (iotime / 1000));

    fprintf(stdout, "Memmove takes %.2f seconds\n",
	    (float)memmovetime / 1000);

  } else {
    /* wait for my turn till PE(n - 1) tells me to go ahead */

    MPI_Status status;

    MPI_Probe(theGroupSize - 1, CVMRECORD_MSG, MPI_COMM_WORLD, &status);
    MPI_Get_count(&status, MPI_CHAR, &bytecount);

    cvmrecord = (cvmrecord_t *)malloc(bytecount);
    if (cvmrecord == NULL) {
      fprintf(stderr, "Thread %d: out of memory\n", myID);
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }

    MPI_Recv(cvmrecord, bytecount, MPI_CHAR, theGroupSize - 1,
	     CVMRECORD_MSG, MPI_COMM_WORLD,  &status);

  }

  /* Every processor should set these parameters correctly */
  theCVMRecordCount = bytecount / sizeof(cvmrecord_t);
  if (theCVMRecordCount * sizeof(cvmrecord_t) != (size_t)bytecount) {
    fprintf(stderr, "Thread %d: received corrupted CVM data\n",
	    myID);
    MPI_Abort(MPI_COMM_WORLD, ERROR);
    exit(1);
  }

  return cvmrecord;
}


static cvmrecord_t *sliceCVM_old(const char *cvm_flatfile)
{
  cvmrecord_t *cvmrecord;
  int32_t bufferedbytes, bytecount, recordcount;

  if (myID == theGroupSize - 1) {
    /* the last processor reads data and
       distribute to other processors*/

    FILE *fp;
    int fd, procid;
    struct stat statbuf;
    void *maxbuf;
    const point_t *intervaltable;
    off_t bytesent;
    int32_t offset;
    const int maxcount =  (1 << 29) / sizeof(cvmrecord_t);
    const int maxbufsize = maxcount * sizeof(cvmrecord_t);

    fp = fopen(cvm_flatfile, "r");
    if (fp == NULL) {
      fprintf(stderr, "Thread %d: Cannot open flat CVM file\n", myID);
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }

    fd = fileno(fp);
    if (fstat(fd, &statbuf) != 0) {
      fprintf(stderr, "Thread %d: Cannot get the status of CVM file\n",
	      myID);
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }

    intervaltable = octor_getintervaltable(myOctree);
    /*
      for (procid = 0; procid <= myID; procid++) {
      fprintf(stderr, "interval[%d] = {%d, %d, %d}\n", procid,
      intervaltable[procid].x << 1, intervaltable[procid].y << 1,
      intervaltable[procid].z << 1);
      }
    */

    bytesent = 0;
    maxbuf = malloc(maxbufsize) ;
    if (maxbuf == NULL) {
      fprintf(stderr, "Thread %d: Cannot allocate send buffer\n", myID);
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }

    /* Try to read max number of CVM records as allowed */
    recordcount = fread(maxbuf, sizeof(cvmrecord_t),
			maxbufsize / sizeof(cvmrecord_t), fp);

    if (recordcount != maxbufsize / sizeof(cvmrecord_t)) {
      fprintf(stderr, "Thread %d: Cannot read-init buffer\n", myID);
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }

    /* start with proc 0 */
    procid = 0;

    while (procid < myID) { /* repeatedly fill the buffer */
      point_t searchpoint, *point;
      int newreads;

      /* we have recordcount to work with */
      cvmrecord = (cvmrecord_t *)maxbuf;

      while (procid < myID) { /* repeatedly send out data */
	searchpoint.x = intervaltable[procid + 1].x << 1;
	searchpoint.y = intervaltable[procid + 1].y << 1;
	searchpoint.z = intervaltable[procid + 1].z << 1;

	offset = zsearch(cvmrecord, recordcount, theCVMRecordSize,
			 &searchpoint);

	point = (point_t *)(cvmrecord + offset);

	if ((point->x != searchpoint.x) ||
	    (point->y != searchpoint.y) ||
	    (point->z != searchpoint.z)) {
	  break;
	} else {
	  bytecount = offset * sizeof(cvmrecord_t);
	  MPI_Send(cvmrecord, bytecount, MPI_CHAR, procid,
		   CVMRECORD_MSG, MPI_COMM_WORLD);
	  /*
	    fprintf(stderr,
	    "Procid = %d offset = %qd bytecount = %d\n",
	    procid, (int64_t)bytesent, bytecount);
	  */

	  bytesent += bytecount;

	  /* prepare for the next processor */
	  recordcount -= offset;
	  cvmrecord = (cvmrecord_t *)point;
	  procid++;
	}
      }

      /* Move residual data to the beginning of the buffer
	 and try to fill the newly free space */
      bufferedbytes = sizeof(cvmrecord_t) * recordcount;
      memmove(maxbuf, cvmrecord, bufferedbytes);
      newreads = fread((char *)maxbuf + bufferedbytes,
		       sizeof(cvmrecord_t), maxcount - recordcount, fp);
      recordcount += newreads;

      if (newreads == 0)
	break;
    }

    free(maxbuf);

    /* I am supposed to accomodate the remaining octants */
    bytecount = statbuf.st_size - bytesent;

    cvmrecord = (cvmrecord_t *)malloc(bytecount);
    if (cvmrecord == NULL) {
      fprintf(stderr, "Thread %d: out of memory for %d bytes\n",
	      myID, bytecount);
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }

    /* fseek exiting the for loop has file cursor propertly */
    if (fseeko(fp, bytesent, SEEK_SET) != 0) {
      fprintf(stderr, "Thread %d: fseeko failed\n", myID);
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }

    if (fread(cvmrecord, 1, bytecount, fp) != (size_t)bytecount) {
      fprintf(stderr, "Thread %d: fail to read the last chunk\n",
	      myID);
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }

    /*
      fprintf(stderr, "Procid = %d offset = %qd bytecount = %d\n",
      myID, (int64_t)bytesent, bytecount);
    */

    fclose(fp);

  } else {
    /* wait for my turn till PE(n - 1) tells me to go ahead */

    MPI_Status status;

    MPI_Probe(theGroupSize - 1, CVMRECORD_MSG, MPI_COMM_WORLD, &status);
    MPI_Get_count(&status, MPI_CHAR, &bytecount);

    cvmrecord = (cvmrecord_t *)malloc(bytecount);
    if (cvmrecord == NULL) {
      fprintf(stderr, "Thread %d: out of memory\n", myID);
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }

    MPI_Recv(cvmrecord, bytecount, MPI_CHAR, theGroupSize - 1,
	     CVMRECORD_MSG, MPI_COMM_WORLD,  &status);

  }

  /* Every processor should set these parameters correctly */
  theCVMRecordCount = bytecount / sizeof(cvmrecord_t);
  if (theCVMRecordCount * sizeof(cvmrecord_t) != (size_t)bytecount) {
    fprintf(stderr, "Thread %d: received corrupted CVM data\n",
	    myID);
    MPI_Abort(MPI_COMM_WORLD, ERROR);
    exit(1);
  }

  return cvmrecord;
}



/**
 * setrec: Search the CVM record array to obtain the material property of
 *         a leaf octant.
 *
 */
void setrec(octant_t *leaf, double ticksize, void *data)
{
  cvmrecord_t *agghit;
  edata_t *edata;
  etree_tick_t x, y, z;
  etree_tick_t halfticks;
  point_t searchpoint;

  edata = (edata_t *)data;

  halfticks = (tick_t)1 << (PIXELLEVEL - leaf->level - 1);

  edata->edgesize = ticksize * halfticks * 2;

  searchpoint.x = x = leaf->lx + halfticks;
  searchpoint.y = y = leaf->ly + halfticks;
  searchpoint.z = z = leaf->lz + halfticks;

  if ((x * ticksize >= theDomainX) ||
      (y * ticksize >= theDomainY) ||
      (z * ticksize >= theDomainZ)) {
    /* Center point out the bound. Set Vs to force split */
    edata->Vs = theFactor * edata->edgesize / 2;
  } else {
    int offset;

    /* map the coordinate from the octor address space to the
       etree address space */
    searchpoint.x = x << 1;
    searchpoint.y = y << 1;
    searchpoint.z = z << 1;

    /* Inbound */
    offset = zsearch(theCVMRecord, theCVMRecordCount, theCVMRecordSize,
		     &searchpoint);
    if (offset < 0) {
      fprintf(stderr, "setrec: fatal error\n");
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }

    agghit = theCVMRecord + offset;
    edata->Vs = agghit->Vs;
    edata->Vp = agghit->Vp;
    edata->rho = agghit->density;

    /* Adjust the Vs */
    edata->Vs = (edata->Vs < theVsCut) ? theVsCut : edata->Vs;
  }

  return;
}
#endif  /* USECVMDB */


/**
 * mesh_generate: Generate and partition an unstructured octree mesh.
 *
 */
static void
mesh_generate()
{
  double elapsedtime;

  MPI_Barrier(MPI_COMM_WORLD);
  elapsedtime = -MPI_Wtime();

  /*----  Generate and partition an unstructured octree mesh ----*/

  if (myID == 0) {
    fprintf(stdout, "octor_newtree ... ");
  }

  myOctree = octor_newtree(theDomainX, theDomainY, theDomainZ,
			   sizeof(edata_t), myID, theGroupSize);

  if (myOctree == NULL) {
    fprintf(stderr, "Thread %d: mesh_generate: fail to create octree\n",
	    myID);
    MPI_Abort(MPI_COMM_WORLD, ERROR);
    exit(1);
  }
  MPI_Barrier(MPI_COMM_WORLD);
  elapsedtime += MPI_Wtime();
  if (myID == 0) {
    fprintf(stdout, "done.... %.2f seconds\n", elapsedtime);
  }
  theOctorNewTreeTime = elapsedtime;

#ifdef USECVMDB
  theCVMQueryStage = 0; /* Query CVM database to refine the mesh */
#else
  /* Use flat data record file and distibute the data in memories */
  elapsedtime = -MPI_Wtime();
  if (myID == 0) {
    fprintf(stdout, "slicing CVMDB ...");
  }

  theCVMRecord = sliceCVM(theCVMFlatFile);
  MPI_Barrier(MPI_COMM_WORLD);
  elapsedtime += MPI_Wtime();

  if (theCVMRecord == NULL) {
    fprintf(stderr, "Thread %d: Error obtaining the CVM records from %s\n",
	    myID, theCVMFlatFile);
    MPI_Abort(MPI_COMM_WORLD, ERROR);
    exit(1);
  };
  theSliceCVMTime = elapsedtime;

  if (myID == 0) {
    fprintf(stdout, "done.... %.2f seconds\n", elapsedtime);
  }

#endif

  elapsedtime = -MPI_Wtime();
  if (myID == 0) {
    fprintf(stdout, "octor_refinetree ...");
  }

  if (octor_refinetree(myOctree, toexpand, setrec) != 0) {
    fprintf(stderr, "Thread %d: mesh_generate: fail to refine octree\n",
	    myID);
    MPI_Abort(MPI_COMM_WORLD, ERROR);
    exit(1);
  }
  MPI_Barrier(MPI_COMM_WORLD);
  elapsedtime += MPI_Wtime();
  if (myID == 0) {
    fprintf(stdout, "done.... %.2f seconds\n", elapsedtime);
  }
  theOctorRefineTreeTime = elapsedtime;

#ifdef USECVMDB
  /* Get the max query time */
  MPI_Reduce(&theCVMQueryTime_Refinement, &theCVMQueryTime_Refinement_MAX,
	     1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
#endif

  elapsedtime = -MPI_Wtime();
  if (myID == 0) {
    fprintf(stdout, "octor_balancetree ... ");
  }

#ifdef USECVMDB
  theCVMQueryStage = 1; /* Query CVM database for balance operation */
#endif

  if (octor_balancetree(myOctree, setrec) != 0) {
    fprintf(stderr, "Thread %d: mesh_generate: fail to balance octree\n",
	    myID);
    MPI_Abort(MPI_COMM_WORLD, ERROR);
    exit(1);
  }
  MPI_Barrier(MPI_COMM_WORLD);
  elapsedtime += MPI_Wtime();
  if (myID == 0) {
    fprintf(stdout, "done.... %.2f seconds\n", elapsedtime);
  }
  theOctorBalanceTreeTime = elapsedtime;

#ifdef USECVMDB
  /* Get the timing stats */
  MPI_Reduce(&theCVMQueryTime_Balance, &theCVMQueryTime_Balance_MAX,
	     1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

  /* Close the material database */
  if(theGeodataSwitch == 0)
    etree_close(theCVMEp);
    
#else
  free(theCVMRecord);
#endif /* USECVMDB */

  elapsedtime = -MPI_Wtime();
  if (myID == 0) {
    fprintf(stdout, "octor_partitiontree ...");
  }
  if (octor_partitiontree(myOctree) != 0) {
    fprintf(stderr, "Thread %d: mesh_generate: fail to balance load\n",
	    myID);
    MPI_Abort(MPI_COMM_WORLD, ERROR);
    exit(1);
  }
  MPI_Barrier(MPI_COMM_WORLD);
  elapsedtime +=  MPI_Wtime();
  if (myID == 0) {
    fprintf(stdout, "done.... %.2f seconds\n", elapsedtime);
  }
  theOctorBalanceLoadTime = elapsedtime;

  elapsedtime = - MPI_Wtime();
  if (myID == 0) {
    fprintf(stdout, "octor_extractmesh ... ");
  }
  myMesh = octor_extractmesh(myOctree);
  if (myMesh == NULL) {
    fprintf(stderr, "Thread %d: mesh_generate: fail to extract mesh\n",
	    myID);
    MPI_Abort(MPI_COMM_WORLD, ERROR);
    exit(1);
  }

  MPI_Barrier(MPI_COMM_WORLD);
  elapsedtime += MPI_Wtime();
  if (myID == 0) {
    fprintf(stdout, "done.... %.2f seconds\n", elapsedtime);
  }
  theOctorExtractMeshTime = elapsedtime;

  /* Compute the total time spent by mesh generation */
  theOctorTime = theOctorNewTreeTime + theOctorRefineTreeTime +
    theOctorBalanceTreeTime + theOctorBalanceLoadTime +
    theOctorExtractMeshTime;

  return;
}


/**
 * toexpand: Instruct the Octor library whether a leaf octant needs to
 *           be expanded or not. Return 1 if true, 0 otherwise.
 *
 */
static int32_t
toexpand(octant_t *leaf, double ticksize, const void *data)
{


  if (data == NULL)
    return 1;
  else {
    const edata_t *edata;

    edata = (edata_t *)data;
    if (edata->edgesize <= edata->Vs / theFactor)
      return 0;
    else
      return 1;
  }
}


/**
 * bulkload: Append the data to the end of the mesh database. Return 0 if OK,
 *           -1 on error.
 *
 */
static int32_t
bulkload(etree_t *mep, mrecord_t *partTable, int32_t count)
{
  int index;

  for (index = 0; index < count; index++) {
    void *payload = &partTable[index].mdata;

    if (etree_append(mep, partTable[index].addr, payload) != 0) {
      /* Append error */
      return -1;
    }
  }

  return 0;
}


/**
 * mesh_printstat:  Obtain and print the statistics of the mesh.
 *
 */
static void
mesh_printstat()
{
  int32_t gmin, gmax;

  /* Collective function calls */
  gmin = octor_getminleaflevel(myOctree, GLOBAL);
  gmax = octor_getmaxleaflevel(myOctree, GLOBAL);

  if (myID == 0) {
    int64_t etotal, ntotal, dntotal;
    int32_t received, procid;
    int32_t *enumTable, *nnumTable, *dnnumTable, *harborednnumTable;
    int32_t rcvquad[4];

    /* Allocate the arrays to hold the statistics */
    enumTable = (int32_t *)malloc(sizeof(int32_t) * theGroupSize);
    nnumTable = (int32_t *)malloc(sizeof(int32_t) * theGroupSize);
    dnnumTable = (int32_t *)malloc(sizeof(int32_t) * theGroupSize);
    harborednnumTable = (int32_t *)malloc(sizeof(int32_t) * theGroupSize);

    if ((enumTable == NULL) ||
	(nnumTable == NULL) ||
	(dnnumTable == NULL) ||
	(harborednnumTable == NULL)) {
      fprintf(stderr, "Thread 0: mesh_printstat: out of memory\n");
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }

    /* Fill in my counts */
    enumTable[0] = myMesh->lenum;
    nnumTable[0] = myMesh->lnnum;
    dnnumTable[0] = myMesh->ldnnum;
    harborednnumTable[0] = myMesh->nharbored;

    /* Initialize sums */
    etotal = myMesh->lenum;
    ntotal = myMesh->lnnum;
    dntotal = myMesh->ldnnum;

    /* Fill in the rest of the tables */
    received = 0;
    while (received < theGroupSize - 1) {
      int32_t fromwhom;
      MPI_Status status;

      MPI_Probe(MPI_ANY_SOURCE, STAT_MSG, MPI_COMM_WORLD, &status);

      fromwhom = status.MPI_SOURCE;

      MPI_Recv(rcvquad, 4, MPI_INT, fromwhom, STAT_MSG, MPI_COMM_WORLD,
	       &status);

      enumTable[fromwhom] = rcvquad[0];
      nnumTable[fromwhom] = rcvquad[1];
      dnnumTable[fromwhom] = rcvquad[2];
      harborednnumTable[fromwhom] = rcvquad[3];

      etotal += rcvquad[0];
      ntotal += rcvquad[1];
      dntotal += rcvquad[2];

      received++;
    }

    fprintf(stdout, "\n\n");
    fprintf(stdout, "Mesh statistics:\n");
    fprintf(stdout, "            Elements   Nodes      D-nodes    ");
    fprintf(stdout, "H-nodes\n");

    /* Copy the totals to static globals */
    theETotal = etotal;
    theNTotal = ntotal;
    theDNTotal = dntotal;

    fprintf( stdout, "Total      :%-11"INT64_FMT"%-11ld%-11"INT64_FMT"\n\n",
	     etotal, ntotal, dntotal );
    for (procid = 0; procid < theGroupSize; procid++) {
      fprintf(stdout, "Thread  %5d:%-11d%-11d%-11d%-11d\n", procid,
	      enumTable[procid], nnumTable[procid], dnnumTable[procid],
	      harborednnumTable[procid]);
    }

    fprintf(stdout, "\n\n");

    free(enumTable);
    free(nnumTable);
    free(dnnumTable);
    free(harborednnumTable);

    printf("Maximum leaf level = %d\n", gmax);
    printf("Minimum leaf level = %d\n", gmin);

    fflush (stdout);

  } else {
    int32_t sndquad[4];

    sndquad[0] = myMesh->lenum;
    sndquad[1] = myMesh->lnnum;
    sndquad[2] = myMesh->ldnnum;
    sndquad[3] = myMesh->nharbored;

    MPI_Send(sndquad, 4, MPI_INT, 0, STAT_MSG, MPI_COMM_WORLD);
  }

  return;
}


/**
 * mesh_output: Join elements and nodes, and send to Thread 0 for output.
 *
 */
static void
mesh_output()
{
  int32_t eindex;
  int32_t remains, batch, batchlimit, idx;
  mrecord_t *partTable;

  theMeshOutTime = -MPI_Wtime();

  batchlimit = BATCH;

  /* Allocate a fixed size buffer space to store the join results */
  partTable = (mrecord_t *)calloc(batchlimit, sizeof(mrecord_t));
  if (partTable == NULL) {
    fprintf(stderr,  "Thread %d: mesh_output: out of memory\n", myID);
    MPI_Abort(MPI_COMM_WORLD, ERROR);
    exit(1);
  }

  if (myID == 0) {
    etree_t *mep;
    int32_t procid;

    printf("mesh_output ... ");

    mep = etree_open(theMEtree, O_CREAT|O_RDWR|O_TRUNC, 0,
		     sizeof(mdata_t),3);
    if (mep == NULL) {
      fprintf(stderr, "Thread 0: mesh_output: ");
      fprintf(stderr, "cannot create mesh etree\n");
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }

    /* Begin an appending operation */
    if (etree_beginappend(mep, 1) != 0) {
      fprintf(stderr, "Thread 0: mesh_output: \n");
      fprintf(stderr, "cannot begin an append operation\n");
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }

    eindex = 0;
    while (eindex < myMesh->lenum) {
      remains = myMesh->lenum - eindex;
      batch = (remains < batchlimit) ? remains : batchlimit;

      for (idx = 0; idx < batch; idx++) {
	mrecord_t *mrecord;
	int32_t whichnode;
	int32_t localnid0;

	mrecord = &partTable[idx];

	/* Fill the address field */
	localnid0 = myMesh->elemTable[eindex].lnid[0];

	mrecord->addr.x = myMesh->nodeTable[localnid0].x;
	mrecord->addr.y = myMesh->nodeTable[localnid0].y;
	mrecord->addr.z = myMesh->nodeTable[localnid0].z;
	mrecord->addr.level = myMesh->elemTable[eindex].level;
	mrecord->addr.type = ETREE_LEAF;

	/* Find the global node ids for the vertices */
	for (whichnode = 0; whichnode < 8; whichnode++) {
	  int32_t localnid;
	  int64_t globalnid;

	  localnid = myMesh->elemTable[eindex].lnid[whichnode];
	  globalnid = myMesh->nodeTable[localnid].gnid;

	  mrecord->mdata.nid[whichnode] = globalnid;
	}

	/* data points to mdata_t type */
	memcpy(&mrecord->mdata.edgesize,
	       myMesh->elemTable[eindex].data,
	       sizeof(edata_t));

	eindex++;
      } /* for a batch */

      if (bulkload(mep, partTable, batch) != 0) {
	fprintf(stderr, "Thread 0: mesh_output: ");
	fprintf(stderr, "error bulk-loading data\n");
	MPI_Abort(MPI_COMM_WORLD, ERROR);
	exit(1);
      }
    } /* for all the elements Thread 0 has */

    /* Receive data from other processors */
    for (procid = 1; procid < theGroupSize; procid++) {
      MPI_Status status;
      int32_t rcvbytecount;

      /* Signal the next processor to go ahead */
      MPI_Send(NULL, 0, MPI_CHAR, procid, GOAHEAD_MSG, MPI_COMM_WORLD);

      while (1) {
	MPI_Probe(procid, MESH_MSG, MPI_COMM_WORLD, &status);
	MPI_Get_count(&status, MPI_CHAR, &rcvbytecount);

	batch = rcvbytecount / sizeof(mrecord_t);

	MPI_Recv(partTable, rcvbytecount, MPI_CHAR, procid,
		 MESH_MSG, MPI_COMM_WORLD, &status);

	if (batch == 0) {
	  /* Done */
	  break;
	}

	if (bulkload(mep, partTable, batch) != 0) {
	  fprintf(stderr, "Thread 0: mesh_output: ");
	  fprintf(stderr, "cannot bulkloading data from ");
	  fprintf(stderr, "Thread %d\n", procid);
	  MPI_Abort(MPI_COMM_WORLD, ERROR);
	  exit(1);
	}
      } /* while there is more data to be received from procid */
    } /* for all the processors */

    /* End the appending operation */
    etree_endappend(mep);

    /* Close the mep to ensure the data is on disk */
    if (etree_close(mep) != 0) {
      fprintf(stderr, "Thread 0: mesh_output ");
      fprintf(stderr, "error closing the etree database\n");
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }

  } else {
    /* Processors other than 0 needs to send data to 0 */
    int32_t sndbytecount;
    MPI_Status status;

    /* Wait for me turn */
    MPI_Recv(NULL, 0, MPI_CHAR, 0, GOAHEAD_MSG, MPI_COMM_WORLD, &status);

    eindex = 0;
    while (eindex < myMesh->lenum) {
      remains = myMesh->lenum - eindex;
      batch = (remains < batchlimit) ? remains : batchlimit;

      for (idx = 0; idx < batch; idx++) {
	mrecord_t *mrecord;
	int32_t whichnode;
	int32_t localnid0;

	mrecord = &partTable[idx];

	/* Fill the address field */
	localnid0 = myMesh->elemTable[eindex].lnid[0];

	mrecord->addr.x = myMesh->nodeTable[localnid0].x;
	mrecord->addr.y = myMesh->nodeTable[localnid0].y;
	mrecord->addr.z = myMesh->nodeTable[localnid0].z;
	mrecord->addr.level = myMesh->elemTable[eindex].level;
	mrecord->addr.type = ETREE_LEAF;

	/* Find the global node ids for the vertices */
	for (whichnode = 0; whichnode < 8; whichnode++) {
	  int32_t localnid;
	  int64_t globalnid;

	  localnid = myMesh->elemTable[eindex].lnid[whichnode];
	  globalnid = myMesh->nodeTable[localnid].gnid;

	  mrecord->mdata.nid[whichnode] = globalnid;
	}

	memcpy(&mrecord->mdata.edgesize,
	       myMesh->elemTable[eindex].data,
	       sizeof(edata_t));

	eindex++;
      } /* for a batch */


      /* Send data to proc 0 */
      sndbytecount = batch * sizeof(mrecord_t);
      MPI_Send(partTable, sndbytecount, MPI_CHAR, 0, MESH_MSG,
	       MPI_COMM_WORLD);
    } /* While there is data left to be sent */

    /* Send an empty message to indicate the end of my transfer */
    MPI_Send(NULL, 0, MPI_CHAR, 0, MESH_MSG, MPI_COMM_WORLD);
  }

  /* Free the memory for the partial join results */
  free(partTable);

  theMeshOutTime += MPI_Wtime();

  if (myID == 0) {
    printf("done....%.2f seconds\n", theMeshOutTime);
  }

  return;
}


/*-----------Computation routines ------------------------------*/

/*
  Macros to facitilate computation

  INTEGRAL_1: Integral_Delfixl_Delfjxl()
  INTEGRAL_2: Integral_Delfixl_Delfjxm()
*/

#define INTEGRAL_1(xki, xkj, xli, xlj, xmi, xmj)			\
  (4.5 * xki * xkj * (1 + xli * xlj / 3) * (1 + xmi * xmj / 3) / 8)

#define INTEGRAL_2(xki, xlj, xmi, xmj)		\
  (4.5 * xki * xlj * (1 + xmi * xmj / 3) / 8)


/**
 * MultAddMatVec: Multiply a 3 x 3 Matrix (M) with a 3 x 1 vector (V1)
 *                and a constant (c). Then add the result to the same
 *                target vector (V2)
 *
 *  V2 = V2 + M * V1 * c
 *
 */
static void MultAddMatVec(fmatrix_t *M, fvector_t *V1, double c,
                          fvector_t *V2)
{
  int row, col;
  fvector_t tmpV;

  tmpV.f[0] = tmpV.f[1] = tmpV.f[2] = 0;

  for (row = 0; row < 3; row++)
    for (col = 0; col < 3; col++)
      tmpV.f[row] += M->f[row][col] * V1->f[col];

  for (row = 0; row < 3; row++)
    V2->f[row] += c * tmpV.f[row];

  return;
}


/**
 * Fast_MultAddMatVec: Multiply a 3 x 3 Matrix (M) with a 3 x 1 vector (V1)
 *                and a constant (c). Then add the result to the same
 *                target vector (V2)
 *
 *  V2 = V2 + M * V1 * c
 *
 */
static void Fast_MultAddMatVec(fmatrix_t *M, fvector_t *V1, double c,
			       fvector_t *V2)
{

  V2->f[0] += c *( M->f[0][0] * V1->f[0]+
		   M->f[0][1] * V1->f[1]+
		   M->f[0][2] * V1->f[2]);
  V2->f[1] += c *( M->f[1][0] * V1->f[0]+
		   M->f[1][1] * V1->f[1]+
		   M->f[1][2] * V1->f[2]);
  V2->f[2] += c * (M->f[2][0] * V1->f[0]+
		   M->f[2][1] * V1->f[1]+
		   M->f[2][2] * V1->f[2]);

  return;
}


/**
 * FastII_MultAddMatVec: Multiply a 3 x 3 Matrix (M) with a 3 x 1 vector (V1)
 *                and a constant (c). Then add the result to the same
 *                target vector (V2)
 *
 *  V2 = V2 +( c1*M1+ c2*M2) * V1 
 * 
 *  This little change improves 1.6 times the speed
 *
 */
static void FastII_MultAddMatVec(fmatrix_t *M1, fmatrix_t *M2, fvector_t *V1, double c1, double c2,
				 fvector_t *V2)
{
  double f0c1,f1c1,f2c1,f0c2,f1c2,f2c2;

  f0c1= V1->f[0]*c1;
  f1c1= V1->f[1]*c1;
  f2c1= V1->f[2]*c1;
  f0c2= V1->f[0]*c2;
  f1c2= V1->f[1]*c2;
  f2c2= V1->f[2]*c2;


  V2->f[0] += M1->f[0][0] * f0c1+ M1->f[0][1] * f1c1+ M1->f[0][2] * f2c1+
    M2->f[0][0] * f0c2+ M2->f[0][1] * f1c2+ M2->f[0][2] * f2c2;

  V2->f[1] += M1->f[1][0] * f0c1+ M1->f[1][1] * f1c1+ M1->f[1][2] * f2c1+
    M2->f[1][0] * f0c2+ M2->f[1][1] * f1c2+ M2->f[1][2] * f2c2;

  V2->f[2] += M1->f[2][0] * f0c1+ M1->f[2][1] * f1c1+ M1->f[2][2] * f2c1+
    M2->f[2][0] * f0c2+ M2->f[2][1] * f1c2+ M2->f[2][2] * f2c2;


  return;
}

static void FastIV_MultAddMatVec(fmatrix_t *M1, fmatrix_t *M2, fvector_t *V1,fvector_t *V2, double c1, double c2, double c3, double c4,
				 fvector_t *V3)
{
  double f0c1v1,f1c1v1,f2c1v1,f0c2v1,f1c2v1, f2c2v1,f0c3v2,f1c3v2,f2c3v2,f0c4v2,f1c4v2,f2c4v2;
    
  f0c1v1= V1->f[0]*c1;
  f1c1v1= V1->f[1]*c1;
  f2c1v1= V1->f[2]*c1;
  f0c2v1= V1->f[0]*c2;
  f1c2v1= V1->f[1]*c2;
  f2c2v1= V1->f[2]*c2;

  f0c3v2= V2->f[0]*c3;
  f1c3v2= V2->f[1]*c3;
  f2c3v2= V2->f[2]*c3;
  f0c4v2= V2->f[0]*c4;
  f1c4v2= V2->f[1]*c4;
  f2c4v2= V2->f[2]*c4;
      
    
  V3->f[0] += M1->f[0][0] * (f0c1v1+f0c3v2)+ M1->f[0][1] * (f1c1v1+f1c3v2) + M1->f[0][2] * (f2c1v1+f2c3v2)+
    M2->f[0][0] * (f0c2v1+f0c4v2)+ M2->f[0][1] * (f1c2v1+f1c4v2) + M2->f[0][2] * (f2c2v1+f2c4v2);
    
  V3->f[1] += M1->f[1][0] * (f0c1v1+f0c3v2)+ M1->f[1][1] * (f1c1v1+f1c3v2) + M1->f[1][2] * (f2c1v1+f2c3v2)+
    M2->f[1][0] * (f0c2v1+f0c4v2)+ M2->f[1][1] * (f1c2v1+f1c4v2) + M2->f[1][2] * (f2c2v1+f2c4v2);
    
  V3->f[2] += M1->f[2][0] * (f0c1v1+f0c3v2)+ M1->f[2][1] * (f1c1v1+f1c3v2) + M1->f[2][2] * (f2c1v1+f2c3v2)+
    M2->f[2][0] * (f0c2v1+f0c4v2)+ M2->f[2][1] * (f1c2v1+f1c4v2) + M2->f[2][2] * (f2c2v1+f2c4v2);  

  return;
}





#define DS_TOTAL_INTERVALS          40
#define DS_TOTAL_PARAMETERS          6

/**
 * Compute histograms for xi, zeta and associated values to understand
 * what is happenning with those parameters involved the damping and
 * delta T.
 */
static void
damping_statistics (
		    double min_xi,
		    double max_xi,
		    double min_zeta,
		    double max_zeta,
		    double min_VsVp,
		    double max_VsVp,
		    double min_VpVsZ,
		    double max_VpVsZ,
		    double min_Vs,
		    double max_Vs
		    )
{
  static const int totalintervals  = DS_TOTAL_INTERVALS;
  static const int totalparameters = DS_TOTAL_PARAMETERS;

  int interval, parameter, row, col, matrixelements;

  double  min_VpVs, max_VpVs;

  double  themins[DS_TOTAL_PARAMETERS],
    themaxs[DS_TOTAL_PARAMETERS],
    spacing[DS_TOTAL_PARAMETERS];

  int32_t counters[DS_TOTAL_PARAMETERS][DS_TOTAL_INTERVALS],
    global_counter[DS_TOTAL_PARAMETERS][DS_TOTAL_INTERVALS],
    global_total[DS_TOTAL_PARAMETERS],
    eindex;

  /* Initializing clue values and variables */

  min_VpVs = 1 / max_VsVp;
  max_VpVs = 1 / min_VsVp;

  themins[0] = min_zeta;
  themins[1] = min_xi;
  themins[2] = min_VsVp;
  themins[3] = min_VpVs;
  themins[4] = min_VpVsZ;
  themins[5] = min_Vs;

  themaxs[0] = max_zeta;
  themaxs[1] = max_xi;
  themaxs[2] = max_VsVp;
  themaxs[3] = max_VpVs;
  themaxs[4] = max_VpVsZ;
  themaxs[5] = max_Vs;

  for ( row = 0; row < totalparameters; row++ )
    {
      for ( col = 0; col < totalintervals; col++ )
	{
	  counters[row][col] = 0;
	}
      global_total[row] = 0;
    }

  for ( row = 0; row < totalparameters; row++ )
    {
      spacing[row] = ( themaxs[row] - themins[row] ) / totalintervals;
    }

  /* loop over the elements */
  for ( eindex = 0; eindex < myMesh->lenum; eindex++)
    {
      /* loop variables */
      elem_t  *elemp;
      edata_t *edata;
      double   a, b,
	omega,
	elementvalues[6];

      /* capturing the elements */
      elemp = &myMesh->elemTable[eindex];
      edata = (edata_t *)elemp->data;

      /* the parameteres */
      elementvalues[0] = 10 / edata->Vs;
      /* (edata->Vs < 1500) ? 25 / edata->Vs : 5 / edata->Vs; */
      /* zeta */
      omega = 3.46410161514 / ( edata->edgesize / edata->Vp );     /* freq in rad */
      a = elementvalues[0] * theABase;                            /* a     */
      b = elementvalues[0] * theBBase;                            /* b     */
      elementvalues[1] = ( a / (2 * omega)) + ( b * omega / 2 );  /* xi    */
      elementvalues[2] = ( edata->Vs / edata->Vp);                /* Vs/Vp */
      elementvalues[3] = edata->Vp / edata->Vs;                   /* Vp/Vs */
      elementvalues[4] = elementvalues[0] * ( edata->Vp / edata->Vs );
      /* Vp/Vs*zeta  */
      elementvalues[5] = edata->Vs;                               /* Vs    */

      /* loop over the parameters */
      for ( parameter = 0; parameter < totalparameters; parameter++ )
        {
	  /* loop over each interval */
	  for ( interval = 0; interval < totalintervals; interval++)
            {
	      /* loop variables */
	      double liminf, limsup;

	      /* histogram limits */
	      liminf = themins[parameter] + (interval * spacing[parameter]);
	      limsup = liminf + spacing[parameter];

	      /* for the last interval adjust to the max value */
	      if ( interval == totalintervals-1 )
		{
		  limsup = themaxs[parameter];
		}

	      /* counting elements within the interval */
	      if ( ( elementvalues[parameter] >  liminf ) &&
		   ( elementvalues[parameter] <= limsup ) )
		{
		  counters[parameter][interval]++;
		}
	    } /* ends loop on intervals */
	} /* ends loop on parameters */
    } /*ends loop on elements */

  /* add all counting results from each processor */
  matrixelements = totalparameters * totalintervals;
  MPI_Reduce (&counters[0][0], &global_counter[0][0], matrixelements,
	      MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Bcast (&global_counter[0][0], matrixelements, MPI_INT,0,MPI_COMM_WORLD);

  /* sums the total of elements for each histogram */
  if (myID == 0)
    {
      for ( parameter = 0; parameter < totalparameters; parameter++)
        {
	  global_counter[parameter][0]++;
	  for ( interval = 0; interval < totalintervals; interval++)
	    {
	      global_total[parameter] = global_total[parameter] + global_counter[parameter][interval];
	    }
        }
    }

  /* MPI Barrier */
  MPI_Barrier( MPI_COMM_WORLD );

  /* prints to the terminal the histograms */
  if (myID == 0)
    {
      /* header to identify each column */
      printf("\n\n\tThe histograms of the following parameters: \n\n");
      printf("\t 1. Zeta\n");
      printf("\t 2. Xi\n");
      printf("\t 3. Vs/Vp\n");
      printf("\t 4. Vp/Vs\n");
      printf("\t 5. Vp/Vs*zeta\n");
      printf("\t 6. Vs\n\n");
      printf("\tAre given in the following table\n");
      printf("\t(each column is one of the parameters)\n\n\t");

      /* printing the histograms */
      for ( interval = 0; interval < totalintervals; interval++)
	{
	  for ( parameter = 0; parameter < totalparameters; parameter++)
	    {
	      printf("%12d",global_counter[parameter][interval]);
	    }
	  printf("\n\t");
	}

      /* prints the total of elements for each column */
      printf("\n\tTotals:\n\n\t");
      for ( parameter = 0; parameter < totalparameters; parameter++)
	{
	  printf("%12d",global_total[parameter]);
	}

      /* prints the interval witdth */
      printf("\n\n\tAnd the intervals width is:");
      for ( parameter = 0; parameter < totalparameters; parameter++)
	{
	  printf("\n\t %2d. %.6f ",parameter+1,spacing[parameter]);
	}
      printf ("\n\n");
      fflush (stdout);
    }

  return;
} /* end damping_statistics */



/**
 * Determine the limit values associated with the damping and delta_t
 * problem
 */
static void solver_set_critical_T()
{

  /* Function local variables */

  int32_t eindex;                     /* element index         */

  double  min_h_over_Vp = 1e32;       /* the min h/Vp group    */
  double  min_h_over_Vp_global;
  int32_t min_h_over_Vp_elem_index = -1;

  double  min_dt_factor_X = 1e32;     /* the min delta_t group */
  double  min_dt_factor_Z = 1e32,
    min_dt_factor_X_global,
    min_dt_factor_Z_global;
  int32_t min_dt_factor_X_elem_index = -1,
    min_dt_factor_Z_elem_index = -1;

  double  min_zeta = 1e32;            /* the zeta group        */
  double  max_zeta = 0,
    min_zeta_global,
    max_zeta_global;
  int32_t min_zeta_elem_index = -1,
    max_zeta_elem_index = -1;

  double  min_xi = 1e32;              /* the xi group          */
  double  max_xi = 0,
    min_xi_global,
    max_xi_global;
  int32_t min_xi_elem_index = -1,
    max_xi_elem_index = -1;

  double  min_VsVp = 1e32;            /* the Vs/Vp group       */
  double  min_VsVp_global,
    max_VsVp = 0,
    max_VsVp_global;
  int32_t min_VsVp_elem_index = -1,
    max_VsVp_elem_index = -1;

  double  min_VpVsZ = 1e32;           /* the Vp/Vs group       */
  double  min_VpVsZ_global,
    max_VpVsZ = 0,
    max_VpVsZ_global;
  int32_t min_VpVsZ_elem_index = -1,
    max_VpVsZ_elem_index = -1;

  double  min_Vs = 1e32;              /* the Vs group          */
  double  min_Vs_global,
    max_Vs = 0,
    max_Vs_global;
  int32_t min_Vs_elem_index = -1,
    max_Vs_elem_index = -1;

  /* Find the minima and maxima for all needed coefficients */
  /* Start loop over the mesh elements */
  for (eindex = 0; eindex < myMesh->lenum; eindex++)
    {
      /* Loop local variables */

      elem_t  *elemp;       /* pointer to the mesh database                 */
      edata_t *edata;       /* pointer to the element data                  */

      double   ratio;       /* the h/Vp ratio                               */
      double   zeta;        /* the time domain zeta-damping                 */
      double   xi;          /* the freq domain xi-damping                   */
      double   omega;       /* the element associated freq from w=3.46*Vp/h */
      double   a, b;        /* the same constants we use for C = aM + bK    */
      double   dt_factor_X; /* the factor of 0.577(1-xi)*h/Vp               */
      double   dt_factor_Z; /* the factor of 0.577(1-zeta)*h/Vp             */
      double   VsVp;        /* the quotient Vs/Vp                           */
      double   VpVsZ;       /* the result of Vp / Vs * zeta                 */
      double   Vs;          /* the Vs                                       */
      double vsInKm,vsInM,qs;

      /* Captures the element */

      elemp = &myMesh->elemTable[eindex];
      edata = (edata_t *)elemp->data;

      /* Calculate the clue quantities */

      ratio       = edata->edgesize / edata->Vp;

      /* Compute damping */
      vsInM=(edata->Vs);
      qs=qualityfactor(vsInM);
      zeta = 1/(2*qs);

      omega       = 3.46410161514 / ratio;
      a           = zeta * theABase;
      b           = zeta * theBBase;
      xi          = ( a / (2 * omega)) + ( b * omega / 2 );
      dt_factor_X = 0.57735026919 * ( 1 - xi ) * ratio;
      dt_factor_Z = 0.57735026919 * ( 1 - zeta ) * ratio;
      VsVp        = edata->Vs / edata->Vp;
      VpVsZ       = zeta * ( edata->Vp / edata->Vs );
      Vs          = edata->Vs;

      /* Updating for extreme values */

      /* ratio */
      if ( ratio < min_h_over_Vp )
	{
	  min_h_over_Vp = ratio;
	  min_h_over_Vp_elem_index = eindex;
	}

      /* dt_factors */
      if ( dt_factor_X < min_dt_factor_X )
	{
	  min_dt_factor_X = dt_factor_X;
	  min_dt_factor_X_elem_index = eindex;
	}
      if ( dt_factor_Z < min_dt_factor_Z )
	{
	  min_dt_factor_Z = dt_factor_Z;
	  min_dt_factor_Z_elem_index = eindex;
	}

      /* min_zeta and max_zeta */
      if ( zeta < min_zeta )
	{
	  min_zeta = zeta;
	  min_zeta_elem_index = eindex;
	}
      if ( zeta > max_zeta )
	{
	  max_zeta = zeta;
	  max_zeta_elem_index = eindex;
	}

      /* min_xi and max_xi */
      if ( xi < min_xi )
	{
	  min_xi = xi;
	  min_xi_elem_index = eindex;
	}
      if ( xi > max_xi )
	{
	  max_xi = xi;
	  max_xi_elem_index = eindex;
	}

      /* min Vs/Vp */
      if ( VsVp < min_VsVp )
	{
	  min_VsVp = VsVp;
	  min_VsVp_elem_index = eindex;
	}
      if ( VsVp > max_VsVp )
	{
	  max_VsVp = VsVp;
	  max_VsVp_elem_index = eindex;
	}

      /* min and max VpVsZ */
      if ( VpVsZ < min_VpVsZ )
	{
	  min_VpVsZ = VpVsZ;
	  min_VpVsZ_elem_index = eindex;
	}
      if ( VpVsZ > max_VpVsZ )
	{
	  max_VpVsZ = VpVsZ;
	  max_VpVsZ_elem_index = eindex;
	}

      /* min Vs */
      if ( Vs < min_Vs )
	{
	  min_Vs = Vs;
	  min_Vs_elem_index = eindex;
	}
      if ( Vs > max_Vs )
	{
	  max_Vs = Vs;
	  max_Vs_elem_index = eindex;
	}

    } /* End of the loop over the mesh elements */

  /* Reducing to global values */
  MPI_Reduce(&min_h_over_Vp,   &min_h_over_Vp_global,   1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
  if ( theDampingStatisticsFlag == 1 )
    {
      MPI_Reduce(&min_dt_factor_X, &min_dt_factor_X_global, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
      MPI_Reduce(&min_dt_factor_Z, &min_dt_factor_Z_global, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
      MPI_Reduce(&min_zeta,        &min_zeta_global,        1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
      MPI_Reduce(&max_zeta,        &max_zeta_global,        1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
      MPI_Reduce(&min_xi,          &min_xi_global,          1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
      MPI_Reduce(&max_xi,          &max_xi_global,          1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
      MPI_Reduce(&min_VsVp,        &min_VsVp_global,        1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
      MPI_Reduce(&max_VsVp,        &max_VsVp_global,        1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
      MPI_Reduce(&min_VpVsZ,       &min_VpVsZ_global,       1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
      MPI_Reduce(&max_VpVsZ,       &max_VpVsZ_global,       1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
      MPI_Reduce(&min_Vs,          &min_Vs_global,          1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
      MPI_Reduce(&max_Vs,          &max_Vs_global,          1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    }

  /* Inform everyone about the global values */
  MPI_Bcast(&min_h_over_Vp_global,   1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
  if ( theDampingStatisticsFlag == 1 )
    {
      MPI_Bcast(&min_dt_factor_X_global, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      MPI_Bcast(&min_dt_factor_Z_global, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      MPI_Bcast(&min_zeta_global,        1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      MPI_Bcast(&max_zeta_global,        1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      MPI_Bcast(&min_xi_global,          1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      MPI_Bcast(&max_xi_global,          1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      MPI_Bcast(&min_VsVp_global,        1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      MPI_Bcast(&max_VsVp_global,        1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      MPI_Bcast(&min_VpVsZ_global,       1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      MPI_Bcast(&max_VpVsZ_global,       1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      MPI_Bcast(&min_Vs_global,          1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      MPI_Bcast(&max_Vs_global,          1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    }

  /* go for damping statistics */
  if ( theDampingStatisticsFlag == 1 )
    {
      damping_statistics(min_xi_global,   max_xi_global,   min_zeta_global,  max_zeta_global,
			 min_VsVp_global, max_VsVp_global, min_VpVsZ_global, max_VpVsZ_global,
			 min_Vs_global,   max_Vs_global);
    }

  /* Static global variable for the critical delta t */
  theCriticalT = min_h_over_Vp_global;

  /* Printing of information */
  MPI_Barrier( MPI_COMM_WORLD );
  if (myID == 0)
    {
      if ( theDampingStatisticsFlag == 1 )
	{
	  printf("\n\n Critical delta t related information: \n\n");
	  printf("\t 1. The minimum h/Vp         = %.6f \n", min_h_over_Vp_global);
	  printf("\t 2. The minimum dt X         = %.6f \n", min_dt_factor_X_global);
	  printf("\t 3. The minimum dt Z         = %.6f \n", min_dt_factor_Z_global);
	  printf("\t 4. The minimum zeta         = %.6f \n", min_zeta_global);
	  printf("\t 5. The maximum zeta         = %.6f \n", max_zeta_global);
	  printf("\t 6. The minimum xi           = %.6f \n", min_xi_global);
	  printf("\t 7. The maximum xi           = %.6f \n", max_xi_global);
	  printf("\t 8. The minimum Vs/Vp        = %.6f \n", min_VsVp_global);
	  printf("\t 9. The maximum Vs/Vp        = %.6f \n", max_VsVp_global);
	  printf("\t10. The minimum (Vp/Vs)*zeta = %.6f \n", min_VpVsZ_global);
	  printf("\t11. The maximum (Vp/Vs)*zeta = %.6f \n", max_VpVsZ_global);
	  printf("\t12. The minimum Vs           = %.6f \n", min_Vs_global);
	  printf("\t13. The maximum Vs           = %.6f \n", max_Vs_global);
	}
      else
        {
	  printf("\n\n Critical delta t related information: \n\n");
	  printf("\t The minimum h/Vp = %.6f \n\n", min_h_over_Vp_global);
	}
    }

#ifdef AUTO_DELTA_T
  /* Set the critical delta T */
  theDeltaT        = theCriticalT;
  theDeltaTSquared = theDeltaT * theDeltaT;
  theGlobalDeltaT  = theDeltaT;

  /* Set the total steps */
  theTotalSteps    = (int)(((theEndT - theStartT) / theDeltaT));
#endif /* AUTO_DELTA_T */

  /* Printing location and element properties of the maximum values */
  if ( theDampingStatisticsFlag == 1 )
    {
      /* Local variables */

      double  local_extremes[13],
	global_extremes[13];
      int32_t element_indices[13];
      int32_t extreme_index;

      local_extremes[0]  = min_h_over_Vp;
      local_extremes[1]  = min_dt_factor_X;
      local_extremes[2]  = min_dt_factor_Z;
      local_extremes[3]  = min_zeta;
      local_extremes[4]  = max_zeta;
      local_extremes[5]  = min_xi;
      local_extremes[6]  = max_xi;
      local_extremes[7]  = min_VsVp;
      local_extremes[8]  = max_VsVp;
      local_extremes[9]  = min_VpVsZ;
      local_extremes[10] = max_VpVsZ;
      local_extremes[11] = min_Vs;
      local_extremes[12] = max_Vs;

      global_extremes[0]  = min_h_over_Vp_global;
      global_extremes[1]  = min_dt_factor_X_global;
      global_extremes[2]  = min_dt_factor_Z_global;
      global_extremes[3]  = min_zeta_global;
      global_extremes[4]  = max_zeta_global;
      global_extremes[5]  = min_xi_global;
      global_extremes[6]  = max_xi_global;
      global_extremes[7]  = min_VsVp_global;
      global_extremes[8]  = max_VsVp_global;
      global_extremes[9]  = min_VpVsZ_global;
      global_extremes[10] = max_VpVsZ_global;
      global_extremes[11] = min_Vs_global;
      global_extremes[12] = max_Vs_global;

      element_indices[0]  = min_h_over_Vp_elem_index;
      element_indices[1]  = min_dt_factor_X_elem_index;
      element_indices[2]  = min_dt_factor_Z_elem_index;
      element_indices[3]  = min_zeta_elem_index;
      element_indices[4]  = max_zeta_elem_index;
      element_indices[5]  = min_xi_elem_index;
      element_indices[6]  = max_xi_elem_index;
      element_indices[7]  = min_VsVp_elem_index;
      element_indices[8]  = max_VsVp_elem_index;
      element_indices[9]  = min_VpVsZ_elem_index;
      element_indices[10] = max_VpVsZ_elem_index;
      element_indices[11] = min_Vs_elem_index;
      element_indices[12] = max_Vs_elem_index;

      /* Printing section title */
      MPI_Barrier( MPI_COMM_WORLD );
      if (myID == 0)
        {
	  printf("\n\t Their corresponding element properties and coordinates are: \n\n");
        }

      /* Loop over the six extreme values */
      MPI_Barrier( MPI_COMM_WORLD );
      for ( extreme_index = 0; extreme_index < 13; extreme_index++ )
        {
	  MPI_Barrier( MPI_COMM_WORLD );
	  if ( local_extremes[extreme_index] == global_extremes[extreme_index] )
	    {
	      tick_t   ldb[3];
	      elem_t  *elemp;
	      edata_t *edata;
	      int      lnid0 = myMesh->elemTable[element_indices[extreme_index]].lnid[0];

	      ldb[0] = myMesh->nodeTable[lnid0].x;
	      ldb[1] = myMesh->nodeTable[lnid0].y;
	      ldb[2] = myMesh->nodeTable[lnid0].z;

	      elemp  = &myMesh->elemTable[element_indices[extreme_index]];
	      edata  = (edata_t *)elemp->data;

	      printf("\t For extreme value No. %d:", extreme_index + 1);
	      printf("\n\t\t h = %.6f, Vp = %.6f Vs = %.6f rho = %.6f",
		     edata->edgesize, edata->Vp , edata->Vs, edata->rho);
	      printf("\n\t\t by thread %d, element_coord = (%.6f, %.6f, %.6f)\n\n",
		     myID, ldb[0] * myMesh->ticksize, ldb[1] * myMesh->ticksize,
		     ldb[2] * myMesh->ticksize);
  	    }
	  MPI_Barrier( MPI_COMM_WORLD );
        } /* End of loop over the extreme values */

      if (myID == 0) {
	fflush (stdout);
      }
    } /* end if damping statistics */

  return;
} /* End solver_set_critical_T */


/*
 *
 * get_minimum_edge: goes through all processor to obtain the minimum
 *                   edgesize of the mesh.
 *
 */

static void get_minimum_edge()
{
  int32_t eindex;
  double min_h = 1e32, min_h_global;
  int32_t min_h_elem_index;

  /* Find the minimal h/Vp in the domain */
  for (eindex = 0; eindex < myMesh->lenum; eindex++) {
    elem_t *elemp;   /* pointer to the mesh database */
    edata_t *edata;
    double h;

    elemp = &myMesh->elemTable[eindex];
    edata = (edata_t *)elemp->data;

    /* Update the min_h value. (h = edgesize)  */
    h = edata->edgesize;
    if (h < min_h) {
      min_h = h;
      min_h_elem_index = eindex;
    }
  }

  MPI_Reduce(&min_h, &min_h_global, 1, MPI_DOUBLE,
	     MPI_MIN, 0, MPI_COMM_WORLD);
  /* Inform everyone of this value */
  MPI_Bcast(&min_h_global, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);

  if (myID == 0) {
    printf("\nThe minimal h  = %.6f\n\n\n", min_h_global);
  }

  theNumericsInformation.minimumh=min_h_global;

  return;
}

/*
 *  print_K_stdoutput(): prints the Stiffness matrix K1, K2 and K3 to the
 *                       standard output
 */
void print_K_stdoutput(){

  int i,j,iloc,jloc;

  for ( i = 0 ; i < 8 ; i++)

    for ( iloc = 0; iloc < 3; iloc++){

      for ( j = 0; j < 8; j++)

	for (jloc = 0; jloc < 3; jloc++)
	  fprintf(stdout, " %e ", theK3[i][j].f[iloc][jloc]);

      fprintf(stdout, "\n");
    }

  exit(1);

}


/**
 * solver_init(): Init matrices and constants, build comm schedule,
 *                allocate/init space for the solver.
 *
 *
 */
static void solver_init()
{
  /* local variables */
  int32_t eindex;
  int32_t c_outsize, c_insize, s_outsize, s_insize;

  /* compute the damping parameters a/zeta and b/zeta */
  compute_setab(theFreq, &theABase, &theBBase);

  /* find out the critical delta T of the current simulation */
  /* and goes for the damping statistics if falg is == 1     */
  MPI_Barrier( MPI_COMM_WORLD );
  solver_set_critical_T();

  /* find the minimum edge size */

  get_minimum_edge();

  /* Init stiffness matrices and other constants */
  compute_K();

  /* to debug */
  /*    print_K_stdoutput();*/

  compute_setab(theFreq, &theABase, &theBBase);

  /* allocation of memory */
  mySolver = (mysolver_t *)malloc(sizeof(mysolver_t));
  if (mySolver == NULL) {
    fprintf(stderr, "Thread %d: solver_init: out of memory\n", myID);
    MPI_Abort(MPI_COMM_WORLD, ERROR);
    exit(1);
  }

  /* Allocate memory */
  mySolver->eTable = (e_t *)calloc(myMesh->lenum, sizeof(e_t));
  mySolver->nTable = (n_t *)calloc(myMesh->nharbored, sizeof(n_t));
  mySolver->tm1 = (fvector_t *)calloc(myMesh->nharbored, sizeof(fvector_t));
  mySolver->tm2 = (fvector_t *)calloc(myMesh->nharbored, sizeof(fvector_t));
  mySolver->force = (fvector_t *)
    calloc(myMesh->nharbored, sizeof(fvector_t));

  mySolver->dn_sched = schedule_new();
  mySolver->an_sched = schedule_new();

  if ((mySolver->eTable == NULL) ||
      (mySolver->nTable == NULL) ||
      (mySolver->tm1 == NULL) ||
      (mySolver->tm2 == NULL) ||
      (mySolver->force == NULL)) {

    fprintf(stderr, "Thread %d: solver_init: out of memory\n", myID);
    MPI_Abort(MPI_COMM_WORLD, ERROR);
    exit(1);
  }

  /* Initialize the data structures. tm1, tm2 and force have
     already been initialized to 0 by calloc().  */
  for (eindex = 0; eindex < myMesh->lenum; eindex++)
    {
      elem_t *elemp;   /* pointer to the mesh database */
      edata_t *edata;
      e_t *ep;         /* pointer to the element constant table */
      double mass, M, mu, lambda;
      int j;

#ifdef BOUNDARY
      tick_t edgeticks;
      tick_t ldb[3], ruf[3];
      int32_t lnid0;
      char flag;
      double dashpot[8][3];
#endif

      double zeta, a, b;

      double qs,vsInKm,vsInM;

      /* Note the difference between the two tables */
      elemp = &myMesh->elemTable[eindex];
      edata = (edata_t *)elemp->data;
      ep    = &mySolver->eTable[eindex];

      /* Calculate the constants */
      mu = edata->rho * edata->Vs * edata->Vs;

      if ( edata->Vp > (edata->Vs * theThresholdVpVs) )
	{
	  lambda = edata->rho * edata->Vs * edata->Vs * theThresholdVpVs
	    * theThresholdVpVs - 2 * mu;
	}

      else
	{
	  lambda = edata->rho * edata->Vp * edata->Vp -  2 * mu;
	}

      /* Adjust Vs, Vp to fix Poisson ratio problem, formula provided by Jacobo */
      if (lambda < 0) {
	if (edata->Vs < 500)
	  edata->Vp = 2.45 * edata->Vs;
	else if (edata->Vs < 1200)
	  edata->Vp = 2 * edata->Vs;
	else
	  edata->Vp = 1.87 * edata->Vs;

	lambda = edata->rho * edata->Vp * edata->Vp;
      }

      if (lambda < 0) {
	fprintf(stderr, "Thread %d: %d element produces negative lambda ",
		myID, eindex);
	fprintf(stderr, "= %.6f \n", lambda);
	MPI_Abort(MPI_COMM_WORLD, ERROR);
      }

      /* coefficients for term (deltaT_squared * Ke * Ut) */
      ep->c1 = theDeltaTSquared * edata->edgesize * mu / 9;
      ep->c2 = theDeltaTSquared * edata->edgesize * lambda / 9;

      /* coefficients for term (b * deltaT * Ke_off * (Ut-1 - Ut)) */
      /* Anelastic attenuation (material damping) */

      /* Compute damping */
      vsInM=(edata->Vs);
      qs=qualityfactor(vsInM);
      zeta = 1/(2*qs);

      if ( zeta > theThresholdDamping )
	{
	  zeta = theThresholdDamping;
	}

      /* the a,b coefficients */
      a = zeta * theABase;
      b = zeta * theBBase;

      /* coefficients for term (b * deltaT * Ke_off * (Ut-1 - Ut)) */
      ep->c3 = b * theDeltaT * edata->edgesize * mu / 9;
      ep->c4 = b * theDeltaT * edata->edgesize * lambda / 9;

#ifdef BOUNDARY

      /* Set the flag for the element */
      lnid0 = elemp->lnid[0];

      ldb[0] = myMesh->nodeTable[lnid0].x;
      ldb[1] = myMesh->nodeTable[lnid0].y;
      ldb[2] = myMesh->nodeTable[lnid0].z;

      edgeticks = (tick_t)1 << (PIXELLEVEL - elemp->level);
      ruf[0] = ldb[0] + edgeticks;
      ruf[1] = ldb[1] + edgeticks;
      ruf[2] = ldb[2] + edgeticks;

      flag = compute_setflag(ldb, ruf, myOctree->nearendp,
			     myOctree->farendp);
      if (flag != 13) {
	compute_setboundary(edata->edgesize, edata->Vp, edata->Vs,
			    edata->rho, flag, dashpot);
      }
#endif /* BOUNDARY */

      /* Assign the element mass to its vertices */
      /* mass is the total mass of the element   */
      /* and M is the mass assigned to each node */
      mass = edata->rho * edata->edgesize * edata->edgesize * edata->edgesize;
      M    = mass / 8;

      /* For each node */
      for (j = 0; j < 8; j++)
        {
	  int32_t lnid;
	  int axis;
	  n_t *np;

	  lnid = elemp->lnid[j];
	  np = &mySolver->nTable[lnid];
	  np->mass_simple += M;

	  /* loop for each axis */
	  for (axis = 0; axis < 3; axis++ )
	    {

	      np->mass_minusaM[axis]  -= (theDeltaT * a * M);
	      np->mass2_minusaM[axis] -= (theDeltaT * a * M);

#ifdef BOUNDARY
	      if (flag != 13)
                {
		  /* boundary impact */

		  /* WARNING: On the new code for damping this hasn't        */
		  /*          been checked by Leo or Jacobo.                 */
		  /*          Ricardo's call.                                */

		  /* Old code for damping                                    */
		  /* np->mplus[axis] += (theDeltaT / 2 * dashpot[j][axis]);  */
		  /* np->mminus[axis] -= (theDeltaT / 2 * dashpot[j][axis]); */

		  /* New code:                                               */
		  /* RICARDO May 2006                                        */
		  np->mass_minusaM[axis]  -= (theDeltaT * dashpot[j][axis]);
		  np->mass2_minusaM[axis] -= (theDeltaT * dashpot[j][axis]);
                }
#endif /* BOUNDARY */

	      /* Old code for damping             */
	      /* np->mplus[axis]  += M;           */
	      /* np->mminus[axis] += M;           */

	      /* New code:                        */
	      /* RICARDO May 2006                 */
	      np->mass_minusaM[axis]  += M;
	      np->mass2_minusaM[axis] += (M * 2);

            } /* end loop for each axis */

        } /* end loop for each node */

    } /* eindex for elements */

  /* Build the communication schedules */
  schedule_build(myMesh, mySolver->dn_sched, mySolver->an_sched);

#ifdef DEBUG
  /* For debug purpose, add gnid into the data field. */
  c_outsize = sizeof(n_t) + sizeof(int64_t);
  c_insize = sizeof(n_t) + sizeof(int64_t);
  s_outsize = sizeof(n_t) + sizeof(int64_t);
  s_insize = sizeof(n_t) + sizeof(int64_t);
#else
  c_outsize = sizeof(n_t);
  c_insize = sizeof(n_t);
  s_outsize = sizeof(n_t);
  s_insize = sizeof(n_t);
#endif /* DEBUG */

  schedule_prepare(mySolver->dn_sched, c_outsize, c_insize,
		   s_outsize, s_insize);

  schedule_prepare(mySolver->an_sched, c_outsize, c_insize,
		   s_outsize, s_insize);

  /* Send mass information of dangling nodes to their owners */
  schedule_senddata(mySolver->dn_sched, mySolver->nTable,
		    sizeof(n_t) / sizeof(double), CONTRIBUTION, DN_MASS_MSG);

  /* Distribute the mass from dangling nodes to anchored nodes. (local) */
  compute_adjust(mySolver->nTable, sizeof(n_t) / sizeof(double),
		 DISTRIBUTION);

  /* Send mass information of anchored nodes to their owner processors*/
  schedule_senddata(mySolver->an_sched, mySolver->nTable,
		    sizeof(n_t) / sizeof(double), CONTRIBUTION, AN_MASS_MSG);

  return;
}



/**
 * solver_printstat(): Print the communication schedule of each processor.
 */
static void solver_printstat()
{
  int32_t dn_c_nodecount, dn_s_nodecount;
  int32_t an_c_nodecount, an_s_nodecount;
  int32_t comm_total;

  /* Get my count */
  dn_c_nodecount = messenger_countnodes(mySolver->dn_sched->first_c);
  dn_s_nodecount = messenger_countnodes(mySolver->dn_sched->first_s);

  an_c_nodecount = messenger_countnodes(mySolver->an_sched->first_c);
  an_s_nodecount = messenger_countnodes(mySolver->an_sched->first_s);

  comm_total = dn_c_nodecount + dn_s_nodecount +
    an_c_nodecount + an_s_nodecount;

  if (myID == 0) {
    int32_t received, procid;

    int32_t *dc_p; /* dangling node contribution processor count */
    int32_t *dc_n; /* dangling node contribution node count */
    int32_t *ds_p; /* dangling node sharing processor count */
    int32_t *ds_n; /* dangling node sharing node count */

    int32_t *ac_p, *ac_n, *as_p, *as_n;
    int32_t *total_n;

    dc_p = (int32_t *)malloc(sizeof(int32_t) * theGroupSize);
    dc_n = (int32_t *)malloc(sizeof(int32_t) * theGroupSize);
    ds_p = (int32_t *)malloc(sizeof(int32_t) * theGroupSize);
    ds_n = (int32_t *)malloc(sizeof(int32_t) * theGroupSize);
    ac_p = (int32_t *)malloc(sizeof(int32_t) * theGroupSize);
    ac_n = (int32_t *)malloc(sizeof(int32_t) * theGroupSize);
    as_p = (int32_t *)malloc(sizeof(int32_t) * theGroupSize);
    as_n = (int32_t *)malloc(sizeof(int32_t) * theGroupSize);

    total_n = (int32_t *)malloc(sizeof(int32_t) * theGroupSize);

    if ((dc_p == NULL) || (dc_n == NULL) ||
	(ds_p == NULL) || (ds_n == NULL) ||
	(ac_p == NULL) || (ac_n == NULL) ||
	(as_p == NULL) || (as_n == NULL) ||
	(total_n == NULL)) {
      fprintf(stderr, "Thread 0: solver_printstat: out of memory\n");
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }

    /* Fill my field */
    dc_p[0] = mySolver->dn_sched->c_count;
    dc_n[0] = dn_c_nodecount;

    ds_p[0] = mySolver->dn_sched->s_count;
    ds_n[0] = dn_s_nodecount;

    ac_p[0] = mySolver->an_sched->c_count;
    ac_n[0] = an_c_nodecount;

    as_p[0] = mySolver->an_sched->s_count;
    as_n[0] = an_s_nodecount;

    total_n[0] = comm_total;

    /* Fill the rest of the tables */
    received = 0;
    while (received < theGroupSize - 1) {
      int32_t fromwhom;
      MPI_Status status;
      int32_t rcvocta[9];

      MPI_Probe(MPI_ANY_SOURCE, STAT_MSG, MPI_COMM_WORLD, &status);

      fromwhom = status.MPI_SOURCE;

      MPI_Recv(rcvocta, 9, MPI_INT, fromwhom, STAT_MSG, MPI_COMM_WORLD,
	       &status);

      dc_p[fromwhom] = rcvocta[0];
      dc_n[fromwhom] = rcvocta[1];

      ds_p[fromwhom] = rcvocta[2];
      ds_n[fromwhom] = rcvocta[3];

      ac_p[fromwhom] = rcvocta[4];
      ac_n[fromwhom] = rcvocta[5];

      as_p[fromwhom] = rcvocta[6];
      as_n[fromwhom] = rcvocta[7];

      total_n[fromwhom] = rcvocta[8];

      received++;
    }

    printf("Solver communication schedule summary:\n");
    printf("            ");
    printf("dc_p dc_n     ds_p ds_n     ac_p ac_n     as_p as_n     ");
    printf("total_ncnt\n");


    for (procid = 0; procid < theGroupSize; procid++) {
      printf("Thread%5d:%-4d %-8d %-4d %-8d %-4d %-8d %-4d %-8d %-8d\n",
	     procid, dc_p[procid], dc_n[procid], ds_p[procid],
	     ds_n[procid], ac_p[procid], ac_n[procid], as_p[procid],
	     as_n[procid], total_n[procid]);
    }

    printf("\n\n");

    fflush (stdout);

    free(dc_p);
    free(dc_n);
    free(ds_p);
    free(ds_n);
    free(ac_p);
    free(ac_n);
    free(as_p);
    free(as_n);
    free(total_n);

  } else {
    int32_t sndocta[9];

    sndocta[0] = mySolver->dn_sched->c_count;
    sndocta[1] = dn_c_nodecount;

    sndocta[2] = mySolver->dn_sched->s_count;
    sndocta[3] = dn_s_nodecount;

    sndocta[4] = mySolver->an_sched->c_count;
    sndocta[5] = an_c_nodecount;

    sndocta[6] = mySolver->an_sched->s_count;
    sndocta[7] = an_s_nodecount;

    sndocta[8] = comm_total;

    MPI_Send(sndocta, 9, MPI_INT, 0, STAT_MSG, MPI_COMM_WORLD);
  }

  return;
}




/**
 * solver_delete: Release all the memory associate with mySolver.
 *
 */
static void solver_delete()
{
  if (mySolver == NULL)
    return;

  free(mySolver->eTable);
  free(mySolver->nTable);

  free(mySolver->tm1);
  free(mySolver->tm2);
  free(mySolver->force);

  schedule_delete(mySolver->dn_sched);
  schedule_delete(mySolver->an_sched);

  free(mySolver);
}


/*
 * interpolate the displacenents and communicate strips to printing PE
 * Bigben version has no control flow, just chugs through planes
 */
static int planes_interpolate_communicate_and_print(){

  int iPlane, iPhi;
  int stripLength;
  int iStrip, elemnum, rStrip;
  /* Auxiliar array to handle shapefunctions in a loop */
  double  xi[3][8]={ {-1,  1, -1,  1, -1,  1, -1, 1} ,
		     {-1, -1,  1,  1, -1, -1,  1, 1} ,
		     {-1, -1, -1, -1,  1,  1,  1, 1} };

  double phi[8];
  double displacementsX, displacementsY, displacementsZ;
  double dphidx,dphidy,dphidz;
  double x,y,z,h,hcube;

  int32_t howManyDisplacements, iDisplacement;
  vector3D_t *localCoords; /* convinient renaming */
  int32_t *nodesToInterpolate[8];
  MPI_Status status;
  MPI_Request sendstat;
  int recvStripCount, StartLocation;
  
  for (iPlane = 0; iPlane < theNumberOfPlanes ;iPlane++){
    for (iStrip = 0; iStrip < thePlanes[iPlane].numberofstripsthisplane; iStrip++){

      /*Interpolate points directly into send buffer for this strip*/
      for (elemnum = 0; elemnum <(thePlanes[iPlane].stripend[iStrip]-thePlanes[iPlane].stripstart[iStrip]+1); elemnum++){
	displacementsX = 0;displacementsY = 0;displacementsZ = 0;

	if(thePlanes[iPlane].fieldtooutput==0){
	  for (iPhi = 0; iPhi < 8; iPhi++){
	    phi[iPhi] =  ( 1 + (xi[0][iPhi]) * (thePlanes[iPlane].strip[iStrip][elemnum].localcoords.x[0]) ) *
	      ( 1 + (xi[1][iPhi]) * (thePlanes[iPlane].strip[iStrip][elemnum].localcoords.x[1]) ) *
	      ( 1 + (xi[2][iPhi]) * (thePlanes[iPlane].strip[iStrip][elemnum].localcoords.x[2]) ) /8;
	    /*!!!! This is caused by bad nodes and coords data in strip routine  */
	    displacementsX += phi[iPhi]*
	      (mySolver->tm1[ thePlanes[iPlane].strip[iStrip][elemnum].nodestointerpolate[iPhi] ].f[0]);
	    displacementsY += phi[iPhi]*
	      (mySolver->tm1[ thePlanes[iPlane].strip[iStrip][elemnum].nodestointerpolate[iPhi] ].f[1]);
	    displacementsZ += phi[iPhi]*
	      (mySolver->tm1[ thePlanes[iPlane].strip[iStrip][elemnum].nodestointerpolate[iPhi] ].f[2]);
	  }
	}else{

	h=thePlanes[iPlane].strip[iStrip][elemnum].h;
	hcube=h*h*h;
	x=thePlanes[iPlane].strip[iStrip][elemnum].localcoords.x[0];
	y=thePlanes[iPlane].strip[iStrip][elemnum].localcoords.x[1];
	z=thePlanes[iPlane].strip[iStrip][elemnum].localcoords.x[2];
	
	for (iPhi = 0; iPhi < 8; iPhi++){
	  dphidx= (2*xi[0][iPhi])*(h+2*xi[1][iPhi]*y)*(h+2*xi[2][iPhi]*z)/(8*hcube);
	  dphidy= (2*xi[1][iPhi])*(h+2*xi[2][iPhi]*z)*(h+2*xi[0][iPhi]*x)/(8*hcube);     
	  dphidz= (2*xi[2][iPhi])*(h+2*xi[0][iPhi]*x)*(h+2*xi[1][iPhi]*y)/(8*hcube);
	  
	  displacementsX+=
	    dphidx*mySolver->tm1[thePlanes[iPlane].strip[iStrip][elemnum].nodestointerpolate[iPhi]].f[thePlanes[iPlane].fieldtooutput-1];
	  displacementsY+=
	    dphidy*mySolver->tm1[thePlanes[iPlane].strip[iStrip][elemnum].nodestointerpolate[iPhi]].f[thePlanes[iPlane].fieldtooutput-1];
	  displacementsZ+=
	    dphidz*mySolver->tm1[thePlanes[iPlane].strip[iStrip][elemnum].nodestointerpolate[iPhi]].f[thePlanes[iPlane].fieldtooutput-1];      
	}
	
	}
	
	planes_stripMPISendBuffer[elemnum*3]     = (double) (displacementsX);
	planes_stripMPISendBuffer[elemnum*3 + 1] = (double) (displacementsY);
	planes_stripMPISendBuffer[elemnum*3 + 2] = (double) (displacementsZ);

      }


      /*add start location as last element, add .1 to insure int-float conversion (yes, bad form)*/
      stripLength = thePlanes[iPlane].stripend[iStrip]-thePlanes[iPlane].stripstart[iStrip]+1;
      planes_stripMPISendBuffer[stripLength*3] = (double) (thePlanes[iPlane].stripstart[iStrip] + 0.1);

      if(myID==0){ /*Don't try to send to same PE, just memcopy*/
	memcpy( &(planes_output_buffer[ thePlanes[iPlane].stripstart[iStrip]*3 ] ), planes_stripMPISendBuffer,
		stripLength*3*sizeof(double) );
      }
      else{
	MPI_Send(planes_stripMPISendBuffer, (stripLength*3)+1, MPI_DOUBLE, 0 , iPlane, MPI_COMM_WORLD);
      }
    } /*for iStrips*/
    
    /* IO PE recieves for this plane */
    if(myID == 0){
      /* Recv all strips not directly memory copied above */
      for (rStrip = 0; rStrip < (thePlanes[iPlane].globalnumberofstripsthisplane - thePlanes[iPlane].numberofstripsthisplane);
	   rStrip++){
	MPI_Recv(planes_stripMPIRecvBuffer, planes_GlobalLargestStripCount*3+1, MPI_DOUBLE, MPI_ANY_SOURCE,
		 iPlane, MPI_COMM_WORLD, &status);
	MPI_Get_count(&status, MPI_DOUBLE, &recvStripCount);
	StartLocation = (int) planes_stripMPIRecvBuffer[recvStripCount-1];
	memcpy( &(planes_output_buffer[StartLocation * 3]), planes_stripMPIRecvBuffer, (recvStripCount-1)*sizeof(double) );
      }
    }

    /* do print for just this plane */
    print_plane_displacements(iPlane);

    /*May not be required*/
    MPI_Barrier( MPI_COMM_WORLD );
  } /*for iPlane*/

  return 1;
}

/*
 *  print_plane_displacements ():
 */
static int
print_plane_displacements(int ThisPlane)
{
  int ret;

  if  (myID == 0){
    ret = fwrite( planes_output_buffer, sizeof(double),
		  3 * thePlanes[ThisPlane].numberofstepsalongstrike * thePlanes[ThisPlane].numberofstepsdowndip,
		  thePlanes[ThisPlane].fpoutputfile );
    if (ret != (3*thePlanes[ThisPlane].numberofstepsalongstrike * thePlanes[ThisPlane].numberofstepsdowndip) ){
      fprintf(stderr, "Error writing all values in planes file %d.\n", ThisPlane);
      exit(1);
    }
  }
 
  return 1;
}


static int
read_myForces( int32_t timestep )
{
  off_t   whereToRead;
  size_t  to_read, read_count;

  whereToRead = ((off_t)sizeof(int32_t))
    + theNodesLoaded * sizeof(int32_t)
    + theNodesLoaded * timestep * sizeof(double) * 3;

  hu_fseeko( fpsource, whereToRead, SEEK_SET );

  to_read    = theNodesLoaded * 3;
  read_count = hu_fread( myForces, sizeof(double), to_read, fpsource );

  return 0;	/* if we got here everything went OK */
}


static inline off_t
checkpoint_header_size( void )
{
  return 3 * sizeof(int32_t);
}


/**
 * Compute the offset for the first displacement vector (\c tm1)
 */
static off_t
checkpoint_offset_tm1( int id, int32_t nharboredmax )
{
  return checkpoint_header_size()
    + 2 * myID * nharboredmax * sizeof(fvector_t);
}


/**
 * Compute the offset for the first displacement vector (\c tm2)
 */
static off_t
checkpoint_offset_tm2( int id, int32_t nharboredmax )
{
  return checkpoint_header_size()
    + 2 * myID * nharboredmax * sizeof(fvector_t)
    + myMesh->nharbored * sizeof(fvector_t);
}


/**
 * Writes to the check point file, the tm1 and tm2 arrays and in the
 * header includes the number of processors, the last good time step and
 * the nharboredmax.
 */
static void
write_checkpoint( int32_t step )
{
  char       filename[256];
  FILE*      fp;
  int32_t    nharboredmax, localnharbored;
  off_t      startTofwrite;

  htimerv_start( &timer_checkpoint_write );

  /* compute max nharbored to print the chunks */
  localnharbored = myMesh->nharbored;

  MPI_Reduce( &localnharbored, &nharboredmax, 1, MPI_INT, MPI_MAX, 0,
	      MPI_COMM_WORLD );
  MPI_Bcast( &nharboredmax, 1, MPI_INT, 0, MPI_COMM_WORLD );

  /* Paranoic check over the nharbored */
  if ((myMesh->nharbored > nharboredmax) || (localnharbored > nharboredmax)) {
    solver_abort( __FUNCTION_NAME, NULL,
		  "nhabored greater than nharboredmax: "
		  "nh %d lnh %d nhmax %d\n",
		  myMesh->nharbored, localnharbored, nharboredmax );
  }


  /* generate file name */
  sprintf( filename, "%s%s%d", theCheckPointingDirOut, "/checkpoint.out",
	   currentCheckPointFile );

  /* all open the file name to avoid overwriting */
  fp = hu_fopen( filename, "wb" );

  MPI_Barrier( MPI_COMM_WORLD );	/* make sure everyone opened the file */

  if (myID == 0) {		/* only PE 0 prints the header */
    hu_fwrite( &theGroupSize, sizeof(int32_t), 1, fp );
    hu_fwrite( &step,         sizeof(int32_t), 1, fp );
    hu_fwrite( &nharboredmax, sizeof(int32_t), 1 ,fp );
  }

  /* find where to write the first vector tm1 */
  startTofwrite = checkpoint_offset_tm1( myID, nharboredmax );

  hu_fseeko( fp, startTofwrite, SEEK_SET );

  /* write the first vector tm1 */
  hu_fwrite( mySolver->tm1, sizeof(fvector_t), myMesh->nharbored, fp );
    
  /* find where to write the second vector tm2 */
  startTofwrite = checkpoint_offset_tm2( myID, nharboredmax );

  hu_fseeko( fp, startTofwrite, SEEK_SET );

  /* write second vector, tm2 */
  hu_fwrite( mySolver->tm2, sizeof(fvector_t), myMesh->nharbored, fp );
  hu_fclose( fp );

  /* flip checkpointing file */
  if ( currentCheckPointFile == 0 ) {
    currentCheckPointFile = 1;
  } else {
    currentCheckPointFile = 0;
  }

  htimerv_stop( &timer_checkpoint_write );
}


/**
 * Reads from the check point file, the tm1 and tm2 arrays and the number
 * of processors, the last time step and the nharbored max.
 */
static int32_t
read_checkpoint( void )
{
  char     filename[256];
  FILE*    fp;
  int32_t  nharboredmax, PESize, step;
  off_t    startToRead;


  /* generate checkpointing file name */
  sprintf( filename, "%s%s", theCheckPointingDirOut, "/checkpoint.in" );

  /* PE 0 reads the header for all */
  if (myID == 0) {
    fp = hu_fopen( filename, "rb" );
    hu_fread( &PESize,       sizeof(int32_t), 1 , fp );
    hu_fread( &step,         sizeof(int32_t), 1 , fp );
    hu_fread( &nharboredmax, sizeof(int32_t), 1 , fp );
    hu_fclose( fp );

    /* check number of processors is right */
    if (PESize != theGroupSize) {
      solver_abort( __FUNCTION_NAME, NULL,
		    "The number of processors for current job does not "
		    "match the number of processor in the checkpoint file"
		    ": %d %d\n", PESize, theGroupSize );
    }
  }

  /* pass header info to other PEs */
  MPI_Bcast( &PESize,       1, MPI_INT, 0, MPI_COMM_WORLD );
  MPI_Bcast( &step,         1, MPI_INT, 0, MPI_COMM_WORLD );
  MPI_Bcast( &nharboredmax, 1, MPI_INT, 0, MPI_COMM_WORLD );

  /* check nhabored */
  if (myMesh->nharbored > nharboredmax) {
    solver_abort( __FUNCTION_NAME, NULL,
		  "nhabored greater than nharboredmax: %d %d\n",
		  myMesh->nharbored, nharboredmax);
  }

  /* open checkpoint file */
  fp = hu_fopen( filename, "rb" );

  /* find right place to read the first vector tm1 */
  startToRead = checkpoint_offset_tm1( myID, nharboredmax );
  hu_fseeko( fp, startToRead, SEEK_SET );

  /* read first vector tm1 */
  hu_fread( mySolver->tm1, sizeof(fvector_t), myMesh->nharbored, fp );
   
  /* find right place to read second vector tm2 */
  startToRead = checkpoint_offset_tm2( myID, nharboredmax );
  hu_fseeko( fp, startToRead, SEEK_SET );

  /* read second vector tm2 */
  hu_fread( mySolver->tm2, sizeof(fvector_t), myMesh->nharbored, fp );
  hu_fclose( fp );

  return step;
}


/**
 * check the max and min value of displacement.
 */
static void
solver_debug_overflow( mysolver_t* solver, mesh_t *mesh, int step )
{
  int nindex;
  double max_disp, min_disp, global_max_disp, global_min_disp;

  max_disp = DBL_MIN;
  min_disp = DBL_MAX;

  /* find the min and max X displacement components */
  for (nindex = 0; nindex < mesh->nharbored; nindex++) {
    fvector_t *tm2Disp;
    n_t *np;

    np      = &solver->nTable[nindex];
    tm2Disp = solver->tm2 + nindex;

    max_disp = (max_disp > tm2Disp->f[0]) ? max_disp : tm2Disp->f[0];
    min_disp = (min_disp < tm2Disp->f[0]) ? min_disp : tm2Disp->f[0];
  }

  /* get the global */
  MPI_Reduce(&max_disp, &global_max_disp, 1, MPI_DOUBLE,
	     MPI_MAX, 0, MPI_COMM_WORLD);
  MPI_Reduce(&min_disp, &global_min_disp, 1, MPI_DOUBLE,
	     MPI_MIN, 0, MPI_COMM_WORLD);


  if (myID == 0) {
    printf("Timestep %d: max_dx = %.6f min_dx = %.6f\n",
	   step, global_max_disp, global_min_disp);
  }
}



/**
 * solver_run: March forward in time and output the result whenever
 *             necessary.
 *
 */
static void
solver_run()
{
  int32_t step, startingStep;
  int32_t c_outsize, c_insize, s_outsize, s_insize;
  int32_t nindex;

  /* Setup the communication schedule properly */

#ifdef DEBUG
  /* The int64_t (global node id) is for debug purpose */
  c_outsize = sizeof(fvector_t) + sizeof(int64_t); /* force */
  c_insize  = sizeof(fvector_t) + sizeof(int64_t); /* displacement */
  s_outsize = sizeof(fvector_t) + sizeof(int64_t); /* displacement */
  s_insize  = sizeof(fvector_t) + sizeof(int64_t); /* force */
#else
  c_outsize = sizeof(fvector_t); /* force */
  c_insize  = sizeof(fvector_t); /* displacement */
  s_outsize = sizeof(fvector_t); /* displacement */
  s_insize  = sizeof(fvector_t); /* force */
#endif /* DEBUG */

  schedule_prepare(mySolver->dn_sched, c_outsize,c_insize,s_outsize,s_insize);
  schedule_prepare(mySolver->an_sched, c_outsize,c_insize,s_outsize,s_insize);

  solver_print_schedules( mySolver );

  /* open a monitor file in a specified directory */
  if (myID == 0) {
    monitor_print( "solver_run() start\n\n"
		   "   Message Conventions\n"
		   "     ST   = Simulation time (s)\n"
		   "     APWT = Acumulated Planes Writting time (s)\n"
		   "     ACT  = Acumulated Plane Comm time (s)\n\n" );
  }

  /* defines starting step depending on checkpointing */
  if ( theUseCheckPoint == 1 ) {
    startingStep = read_checkpoint();
  } else {
    startingStep = 0;
  }

  if (myID == 0) {
    printf( "Starting time step = %d\n\n", startingStep );
    fflush( stdout );
  }

  MPI_Barrier( MPI_COMM_WORLD );

  /* march forward in time */
  for (step = startingStep; step < theTotalSteps; step++) {
    fvector_t *tmpvector;

    if ( (theCheckPointingRate != 0) && (step != startingStep)
	 && ((step % theCheckPointingRate) == 0) )
      {
	write_checkpoint( step );
      }


    /* Show a progress bar to make the process less anxious! */
    /* Add to progress_file relevant data */
    if (myID == 0)  {
      if ( step % 50 == 0) { /* break the line */
	monitor_print( " ST=% 4.1f APWT=% 6.2f ACT=% 6.2f"
		       " BT=% 6.2f CT=% 6.2f DT=% 6.2f ET=% 6.2f"
		       " E1T=% 6.2f E2T=% 6.2f E3T=% 6.2f"
		       " FT=% 6.2f GT=% 6.2f HT=% 6.2f IT=% 6.2f"
		       " JT=% 6.2f KT=% 6.2f LT=% 6.2f sT=% 6.2f"
		       " Wall Time=% 6.2f\n",
		       step * theDeltaT,
		       thePrintPlaneTime, theCollectPlaneTime,
		       the_B_timer,  the_C_timer, the_D_timer,
		       the_E_timer,  the_E1_timer, the_E2_timer,
		       the_E3_timer, the_F_timer, the_G_timer,
		       the_H_timer,  the_I_timer, the_J_timer,
		       the_K_timer,  the_L_timer, the_s_timer,
		       MPI_Wtime() + theSolverTime );
      }

      monitor_print( "*" );
    }

    /* Prepare for a new iteration*/
    tmpvector     = mySolver->tm2;
    mySolver->tm2 = mySolver->tm1;
    mySolver->tm1 = tmpvector;

    /* TIMER A */
    /* I am not going to put a timer here as this flas is not ON */
    if (DO_OUTPUT && (step % theRate == 0)) {
      /* Output the current timestep */
      solver_output();
    }

    HU_COND_GLOBAL_BARRIER( theTimingBarriersFlag );

    /* TIMER B */
    the_B_timer -= MPI_Wtime();
    if (theNumberOfPlanes != 0)
      {
	if ( step % thePlanePrintRate == 0)
	  {
	    thePrintPlaneTime -= MPI_Wtime();
	    planes_interpolate_communicate_and_print();
	    thePrintPlaneTime += MPI_Wtime();
	  }
      }
    the_B_timer += MPI_Wtime();
	
    HU_COND_GLOBAL_BARRIER( theTimingBarriersFlag );

    /* TIMER s for stations */
    the_s_timer -= MPI_Wtime();
    if(theNumberOfStations !=0){
      interpolate_station_displacements(step);
    }
    the_s_timer += MPI_Wtime();

    /* Calculate the force due to the earthquake souce and element
       stiffness matrices */
    HU_COND_GLOBAL_BARRIER( theTimingBarriersFlag );


    the_C_timer -= MPI_Wtime();
    if ( theNodesLoaded > 0 ) {
      read_myForces(step);  /* TIMER C */
    }
    the_C_timer += MPI_Wtime();

    HU_COND_GLOBAL_BARRIER( theTimingBarriersFlag );

    the_D_timer -= MPI_Wtime();
    compute_addforce_s(step); /* TIMER D */
    the_D_timer += MPI_Wtime();

    HU_COND_GLOBAL_BARRIER( theTimingBarriersFlag );

    the_E_timer -= MPI_Wtime();
    compute_addforce_e();     /* TIMER E */
    the_E_timer += MPI_Wtime();

    HU_COND_GLOBAL_BARRIER( theTimingBarriersFlag );

    /* TIMER F */
    the_F_timer -= MPI_Wtime();
    /* Send the forces on dangling nodes to their owner processors */
    schedule_senddata(mySolver->dn_sched, mySolver->force,
		      sizeof(fvector_t) / sizeof(double), CONTRIBUTION,
		      DN_FORCE_MSG);
    the_F_timer += MPI_Wtime();

    HU_COND_GLOBAL_BARRIER( theTimingBarriersFlag );

    /* TIMER G */
    the_G_timer -= MPI_Wtime();
    /* Distribute the forces to LOCAL anchored nodes */
    compute_adjust(mySolver->force, sizeof(fvector_t) / sizeof(double),
		   DISTRIBUTION);
    the_G_timer += MPI_Wtime();

    HU_COND_GLOBAL_BARRIER( theTimingBarriersFlag );

    /* TIMER H */
    the_H_timer -= MPI_Wtime();
    /* Send the forces on anchored nodes to their owner processors */
    schedule_senddata(mySolver->an_sched, mySolver->force,
		      sizeof(fvector_t) / sizeof(double), CONTRIBUTION,
		      AN_FORCE_MSG);
    the_H_timer += MPI_Wtime();

    HU_COND_GLOBAL_BARRIER( theTimingBarriersFlag );

    /* TIMER I */
    the_I_timer -= MPI_Wtime();

    /* Compute new displacements of my harbored nodes */
    for (nindex = 0; nindex < myMesh->nharbored; nindex++)
      {
	fvector_t *nodalForce, *tm1Disp, *tm2Disp;
	n_t *np;

	np = &mySolver->nTable[nindex];
	nodalForce = mySolver->force + nindex;
	tm1Disp = mySolver->tm1 + nindex;
	tm2Disp = mySolver->tm2 + nindex;

	/* Old code for damping                                            */

	/* nodalForce->f[0] +=                                             */
	/*     np->mass2x * tm1Disp->f[0] - np->mminus[0] * tm2Disp->f[0]; */
	/* nodalForce->f[1] +=                                             */
	/*     np->mass2x * tm1Disp->f[1] - np->mminus[1] * tm2Disp->f[1]; */
	/* nodalForce->f[2] +=                                             */
	/*     np->mass2x * tm1Disp->f[2] - np->mminus[2] * tm2Disp->f[2]; */

	/* Overwrite tm2                                                   */
	/* tm2Disp->f[0] = nodalForce->f[0] / np->mplus[0];                */
	/* tm2Disp->f[1] = nodalForce->f[1] / np->mplus[1];                */
	/* tm2Disp->f[2] = nodalForce->f[2] / np->mplus[2];                */

	/* New code for damping                                            */
	/* RICARDO May 2006                                                */

	/* total nodal forces */
	nodalForce->f[0] +=  np->mass2_minusaM[0] * tm1Disp->f[0]
	  - np->mass_minusaM[0] * tm2Disp->f[0];
	nodalForce->f[1] +=  np->mass2_minusaM[1] * tm1Disp->f[1]
	  - np->mass_minusaM[1] * tm2Disp->f[1];
	nodalForce->f[2] +=  np->mass2_minusaM[2] * tm1Disp->f[2]
	  - np->mass_minusaM[2] * tm2Disp->f[2];
	/* Overwrite tm2 */
	tm2Disp->f[0] = nodalForce->f[0] / np->mass_simple;
	tm2Disp->f[1] = nodalForce->f[1] / np->mass_simple;
	tm2Disp->f[2] = nodalForce->f[2] / np->mass_simple;

	/* zero out the force now */
	memset(nodalForce, 0, sizeof(fvector_t));

      } /* For all my harbored nodes */
    the_I_timer += MPI_Wtime();

    HU_COND_GLOBAL_BARRIER( theTimingBarriersFlag );

    /* TIMER J */
    the_J_timer -= MPI_Wtime();
    /* Share the displacement of anchored nodes with other processors */
    schedule_senddata(mySolver->an_sched, mySolver->tm2,
		      sizeof(fvector_t) / sizeof(double), SHARING,
		      AN_DISP_MSG);
    the_J_timer += MPI_Wtime();

    HU_COND_GLOBAL_BARRIER( theTimingBarriersFlag );

    /* TIMER K */
    the_K_timer -= MPI_Wtime();
    /* Adjust the displaceements of my LOCAL dangling nodes */
    compute_adjust(mySolver->tm2, sizeof(fvector_t) / sizeof(double),
		   ASSIGNMENT);
    the_K_timer += MPI_Wtime();

    HU_COND_GLOBAL_BARRIER( theTimingBarriersFlag );

    /* TIMER L */
    the_L_timer -= MPI_Wtime();
    /* Share the displacment of dangling nodes with other processors */
    schedule_senddata(mySolver->dn_sched, mySolver->tm2,
		      sizeof(fvector_t) / sizeof(double), SHARING,
		      DN_DISP_MSG);
    the_L_timer += MPI_Wtime();

    HU_COND_GLOBAL_BARRIER( theTimingBarriersFlag );

    if (0) {
      solver_debug_overflow( mySolver, myMesh, step );
    }

  }

  if (DO_OUTPUT && (theOutFp != NULL)) {
    fclose( theOutFp );		    /* close the output file */
  }


  /* collect times in read_forces,compute_addforce_s  & compute_addforce_e */
  /*
   * MPI_Reduce (&theReadSourceForceTime,&theAbsReadSrcFrcT, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
   * MPI_Reduce (&theAddForceSTime      ,&theAbsFrcST,       1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
   * MPI_Reduce (&theAddForceETime      ,&theAbsFrcET,       1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
   */
    
  MPI_Reduce (&the_B_timer, &theMin_B_timer, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
  MPI_Reduce (&the_C_timer, &theMin_C_timer, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
  MPI_Reduce (&the_D_timer, &theMin_D_timer, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
  MPI_Reduce (&the_E_timer, &theMin_E_timer, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
  MPI_Reduce (&the_F_timer, &theMin_F_timer, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
  MPI_Reduce (&the_G_timer, &theMin_G_timer, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
  MPI_Reduce (&the_H_timer, &theMin_H_timer, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
  MPI_Reduce (&the_I_timer, &theMin_I_timer, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
  MPI_Reduce (&the_J_timer, &theMin_J_timer, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
  MPI_Reduce (&the_K_timer, &theMin_K_timer, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
  MPI_Reduce (&the_L_timer, &theMin_L_timer, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
  MPI_Reduce (&the_s_timer, &theMin_s_timer, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
  MPI_Reduce (&the_E1_timer, &theMin_E1_timer, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
  MPI_Reduce (&the_E2_timer, &theMin_E2_timer, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
  MPI_Reduce (&the_E3_timer, &theMin_E3_timer, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);

  MPI_Reduce (&the_B_timer, &theMax_B_timer, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
  MPI_Reduce (&the_C_timer, &theMax_C_timer, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
  MPI_Reduce (&the_D_timer, &theMax_D_timer, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
  MPI_Reduce (&the_E_timer, &theMax_E_timer, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
  MPI_Reduce (&the_F_timer, &theMax_F_timer, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
  MPI_Reduce (&the_G_timer, &theMax_G_timer, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
  MPI_Reduce (&the_H_timer, &theMax_H_timer, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
  MPI_Reduce (&the_I_timer, &theMax_I_timer, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
  MPI_Reduce (&the_J_timer, &theMax_J_timer, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
  MPI_Reduce (&the_K_timer, &theMax_K_timer, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
  MPI_Reduce (&the_L_timer, &theMax_L_timer, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
  MPI_Reduce (&the_s_timer, &theMax_s_timer, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
  MPI_Reduce (&the_E1_timer, &theMax_E1_timer, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
  MPI_Reduce (&the_E2_timer, &theMax_E2_timer, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
  MPI_Reduce (&the_E3_timer, &theMax_E3_timer, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

  return;
}



/**
 * solver_output_seq: Output the velocity of the mesh nodes for the current
 *                timestep. Send the data to Thread 0 who is responsible
 *                for dumping the data to disk.
 */
void
solver_output_seq()
{
  int32_t nindex;
  int32_t batchlimit, idx;

#ifdef DEBUG
  int64_t gnid_prev, gnid_current;
  int32_t first_counted;
#endif /* DEBUG */

  batchlimit = BATCH * 10;

  /* Allocate a fixed size buffer space if not initiazlied */
  if (myVelocityTable == NULL) {
    myVelocityTable = (fvector_t *)calloc(batchlimit, sizeof(fvector_t));
    if (myVelocityTable == NULL) {
      fprintf(stderr,  "Thread %d: solver_output_seq: out of memory\n",
	      myID);
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }
  }

  if (myID == 0) {
    int32_t procid;
#ifdef DEBUG
    first_counted = 0;
#endif

    if (theOutFp == NULL) {
      out_hdr_t out_hdr;

      /* First output, create the output file */
      theOutFp = fopen(theOutFile, "w+");
      if (theOutFp == NULL) {
	fprintf(stderr, "Thread 0: solver_output_seq: ");
	fprintf(stderr, "cannot create %s\n", theOutFile);
	perror("fopen");
	MPI_Abort(MPI_COMM_WORLD, ERROR);
	exit(1);
      }

      /* Write the header that contains the metadata */
      out_hdr.domain_x = theDomainX;
      out_hdr.domain_y = theDomainY;
      out_hdr.domain_z = theDomainZ;
      out_hdr.total_nodes = theNTotal;
      out_hdr.total_elements = theETotal;
      out_hdr.mesh_ticksize = myMesh->ticksize;
      out_hdr.output_steps = (theTotalSteps - 1) / theRate + 1;

      if (fwrite(&out_hdr, sizeof(out_hdr_t), 1, theOutFp) != 1){
	fprintf(stderr, "Thread 0: solver_output_seq: ");
	fprintf(stderr, "fail to write 4D-out header info\n");
	perror("fwrite");
	MPI_Abort(MPI_COMM_WORLD, ERROR);
	exit(1);
      }
    }

    the4DOutTime -= MPI_Wtime();

    /* Output my owned nodes' velocities */
    nindex = 0;
    while (nindex < myMesh->nharbored) {
      fvector_t vel;

      if (myMesh->nodeTable[nindex].ismine) {
	vel.f[0] =
	  (mySolver->tm1[nindex].f[0]
	   - mySolver->tm2[nindex].f[0])  / theDeltaT;
	vel.f[1] =
	  (mySolver->tm1[nindex].f[1]
	   - mySolver->tm2[nindex].f[1])  / theDeltaT;
	vel.f[2] =
	  (mySolver->tm1[nindex].f[2]
	   - mySolver->tm2[nindex].f[2])  / theDeltaT;


	if (fwrite(&vel, sizeof(fvector_t), 1, theOutFp) != 1) {
	  fprintf(stderr, "Thread 0: solver_output_seq: error\n");
	  MPI_Abort(MPI_COMM_WORLD, ERROR);
	  exit(1);
	}

	the4DOutSize += sizeof(fvector_t);

#ifdef DEBUG
	gnid_current = myMesh->nodeTable[nindex].gnid;

	if (first_counted) {
	  if (gnid_prev != (gnid_current - 1)) {
	    fprintf( stderr, "PE 0: uncontinuous gnid\n"
		     "   gnid_prev = %" INT64_FMT
		     ", gnid_current = %" INT64_FMT "\n",
		     gnid_prev, gnid_current);
	  }
	} else {
	  first_counted = 1;
	}

	gnid_prev = gnid_current;

	if ((vel.f[0] != 0) ||
	    (vel.f[1] != 0) ||
	    (vel.f[2] != 0)) {
	  /*
	    fprintf(stderr, "Thread 0: Node %ld  non-zero values\n",
	    gnid_current);
	  */
	}
#endif /* DEBUG */

      }

      nindex++;
    }

    /* Receive data from other processors */
    for (procid = 1; procid < theGroupSize; procid++) {
      MPI_Status status;
      int32_t rcvbytecount;

      /* Signal the next processor to go ahead */
      MPI_Send(NULL, 0, MPI_CHAR, procid, GOAHEAD_MSG, MPI_COMM_WORLD);

      while (1) {
	MPI_Probe(procid, OUT4D_MSG, MPI_COMM_WORLD, &status);
	MPI_Get_count(&status, MPI_CHAR, &rcvbytecount);

	/* Receive the data even if rcvbytecount == 0. Otherwise
	   the 0-byte message would get stuck in the message queue */
	MPI_Recv(myVelocityTable, rcvbytecount, MPI_CHAR, procid,
		 OUT4D_MSG, MPI_COMM_WORLD, &status);

	if (rcvbytecount == 0) {
	  /* Done */
	  break;
	}

	if (fwrite(myVelocityTable, rcvbytecount, 1, theOutFp) != 1) {
	  fprintf(stderr, "Thread 0: solver_output_seq: error\n");
	  MPI_Abort(MPI_COMM_WORLD, ERROR);
	  exit(1);
	}

	the4DOutSize += rcvbytecount;

      } /* while there is more data to be received from procid */
    } /* for all the processors */

    the4DOutTime += MPI_Wtime();

  } else {
    /* Processors other than 0 needs to send data to 0 */
    int32_t sndbytecount;
    MPI_Status status;

    /* Wait for me turn */
    MPI_Recv(NULL, 0, MPI_CHAR, 0, GOAHEAD_MSG, MPI_COMM_WORLD, &status);

#ifdef DEBUG
    first_counted = 0;
#endif


    nindex = 0;
    while (nindex < myMesh->nharbored) {
      fvector_t *velp;

      idx = 0;
      while ((idx < batchlimit) &&
	     (nindex < myMesh->nharbored)) {

	if (myMesh->nodeTable[nindex].ismine) {

	  velp = &myVelocityTable[idx];

	  velp->f[0] =
	    (mySolver->tm1[nindex].f[0]
	     - mySolver->tm2[nindex].f[0])  / theDeltaT;
	  velp->f[1] =
	    (mySolver->tm1[nindex].f[1]
	     - mySolver->tm2[nindex].f[1])  / theDeltaT;
	  velp->f[2] =
	    (mySolver->tm1[nindex].f[2]
	     - mySolver->tm2[nindex].f[2])  / theDeltaT;


	  idx++;

#ifdef DEBUG
	  gnid_current = myMesh->nodeTable[nindex].gnid;

	  if (first_counted) {
	    if (gnid_prev != (gnid_current - 1)) {
	      fprintf( stderr, "PE %d uncontinuous gnid\n"
		       "  gnid_prev = %" INT64_FMT
		       ", gnid_current = %" INT64_FMT "\n",
		       myID, gnid_prev, gnid_current );
	    }
	  } else {
	    first_counted = 1;
	  }

	  gnid_prev = gnid_current;

	  /* debug */
	  /*
	    if ((velp->f[0] != 0) ||
	    (velp->f[1] != 0) ||
	    (velp->f[2] != 0)) {
	    fprintf(stderr,
	    "Thread %d: there are non-zero values\n",
	    myID);
	    }
	  */
#endif /* DEBUG */

	}

	nindex++;
      }

      /* Send data to proc 0 */

      if (idx > 0) {
	/* I have some real data to send */
	sndbytecount = idx * sizeof(fvector_t);
	MPI_Send(myVelocityTable, sndbytecount, MPI_CHAR, 0, OUT4D_MSG,
		 MPI_COMM_WORLD);
      }
    } /* While there is data left to be sent */

    /* Send an empty message to indicate the end of my transfer */
    MPI_Send(NULL, 0, MPI_CHAR, 0, OUT4D_MSG, MPI_COMM_WORLD);
  }

  return;
}


/**
 * schedule_new: Allocate and init a scheduler
 *
 */
static schedule_t *schedule_new()
{
  schedule_t *sched;

  sched = (schedule_t *)malloc(sizeof(schedule_t));
  if (sched == NULL)
    return NULL;

  sched->c_count = 0;
  sched->first_c = NULL;
  sched->messenger_c = (messenger_t **)
    calloc(theGroupSize, sizeof(messenger_t *));
  if (sched->messenger_c == NULL)
    return NULL;

  sched->s_count = 0;
  sched->first_s = NULL;
  sched->messenger_s = (messenger_t **)
    calloc(theGroupSize, sizeof(messenger_t *));
  if (sched->messenger_s == NULL)
    return NULL;

  return sched;
}



/**
 * schedule_allocmapping: Allocate the mapping table for each of my
 *                        messenger.
 *
 */
static void
schedule_allocmapping(schedule_t *sched)
{
  messenger_t *messenger;
  int32_t nodecount;

  messenger = sched->first_c;
  while (messenger != NULL) {
    nodecount = messenger->nodecount;

    messenger->mapping =
      (int32_t *)calloc(nodecount, sizeof(int32_t));

    if (messenger->mapping == NULL) {
      fprintf(stderr, "Thread %d: schedule_allocamapping: ", myID);
      fprintf(stderr, " out of memory\n");
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }

    messenger = messenger->next;
  }

  messenger = sched->first_s;
  while (messenger != NULL) {
    nodecount = messenger->nodecount;

    messenger->mapping =
      (int32_t *)calloc(nodecount, sizeof(int32_t));

    if (messenger->mapping == NULL) {
      fprintf(stderr, "Thread %d: schedule_allocamapping: ", myID);
      fprintf(stderr, " out of memory\n");
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }

    messenger = messenger->next;
  }

  return;
}



/**
 * schudule_allocMPIctl: Allocate MPI controls for non-blocing receives
 *
 */
static void
schedule_allocMPIctl(schedule_t *sched)
{
  if (sched->c_count != 0) {
    sched->irecvreqs_c =
      (MPI_Request *)malloc(sizeof(MPI_Request) * sched->c_count);
    sched->irecvstats_c =
      (MPI_Status *)malloc(sizeof(MPI_Status) * sched->c_count);

    if ((sched->irecvreqs_c == NULL) ||
	(sched->irecvstats_c == NULL)) {
      fprintf(stderr, "Thread %d: schedule_allocMPIctl: ", myID);
      fprintf(stderr, "out of memory\n");
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }
  } else {
    sched->irecvreqs_c = NULL;
    sched->irecvstats_c = NULL;
  }

  if (sched->s_count != 0) {
    sched->irecvreqs_s =
      (MPI_Request *)malloc(sizeof(MPI_Request) * sched->s_count);
    sched->irecvstats_s =
      (MPI_Status *)malloc(sizeof(MPI_Status) * sched->s_count);

    if ((sched->irecvreqs_s == NULL) ||
	(sched->irecvstats_s == NULL)) {
      fprintf(stderr, "Thread %d: schedule_allocMPIctl: ", myID);
      fprintf(stderr, "out of memory\n");
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }
  } else {
    sched->irecvreqs_s = NULL;
    sched->irecvstats_s = NULL;
  }

  return;
}


/**
 * schedule_build: Build a communication schedule using local information.
 *
 */
static void
schedule_build(mesh_t *mesh, schedule_t *dnsched, schedule_t *ansched)
{
  int32_t nindex;
  node_t *nodep;
  messenger_t * messenger;

  for (nindex = 0; nindex < mesh->nharbored; nindex++) {
    nodep = &mesh->nodeTable[nindex];
    int32_t owner, sharer;

    if (!nodep->ismine) {
      /* I do not own this node. Add its owner processor to my c-list */

      owner = nodep->proc.ownerid;

      if (nodep->isanchored) {
	messenger = ansched->messenger_c[owner];
      } else {
	messenger = dnsched->messenger_c[owner];
      }

      if (messenger == NULL) {
	messenger = messenger_new(owner);
	if (messenger == NULL) {
	  fprintf(stderr, "Thread %d: schedule_build: ", myID);
	  fprintf(stderr, "out of memory.\n");
	  MPI_Abort(MPI_COMM_WORLD, ERROR);
	  exit(1);
	}

	if (nodep->isanchored) {
	  ansched->c_count++;
	  ansched->messenger_c[owner] = messenger;
	  messenger->next = ansched->first_c;
	  ansched->first_c = messenger;
	} else {
	  dnsched->c_count++;
	  dnsched->messenger_c[owner] = messenger;
	  messenger->next = dnsched->first_c;
	  dnsched->first_c = messenger;
	}
      }

      /* Update the number of nodecount for the messenger */
      messenger->nodecount++;

    } else {
      /* I own this node. Add any sharing processor to my s-list */

      int32link_t *int32link;

      int32link = nodep->proc.share;
      while (int32link != NULL) {
	sharer = int32link->id;

	if (nodep->isanchored) {
	  messenger = ansched->messenger_s[sharer];
	} else {
	  messenger = dnsched->messenger_s[sharer];
	}

	if (messenger == NULL) {
	  messenger = messenger_new(sharer);
	  if (messenger == NULL) {
	    fprintf(stderr, "Thread %d: schedule_build: ", myID);
	    fprintf(stderr, "out of memory.\n");
	    MPI_Abort(MPI_COMM_WORLD, ERROR);
	    exit(1);
	  }

	  if (nodep->isanchored) {
	    ansched->s_count++;
	    ansched->messenger_s[sharer] = messenger;
	    messenger->next = ansched->first_s;
	    ansched->first_s = messenger;
	  } else {
	    dnsched->s_count++;
	    dnsched->messenger_s[sharer] = messenger;
	    messenger->next = dnsched->first_s;
	    dnsched->first_s = messenger;
	  }
	}

	/* Update the nodecount */
	messenger->nodecount++;

	/* Move to the next sharing processor */
	int32link = int32link->next;
      }
    }
  }

  /* Allocate MPI controls */
  schedule_allocMPIctl(ansched);
  schedule_allocMPIctl(dnsched);

  /* Allocate localnode table for each of the messegners I have */
  schedule_allocmapping(ansched);
  schedule_allocmapping(dnsched);

  /* Go through the nodes again and fill out the mapping table */
  for (nindex = 0; nindex < mesh->nharbored; nindex++) {
    nodep = &mesh->nodeTable[nindex];
    int32_t owner, sharer;

    if (!nodep->ismine) {
      /* I do not own this node. Add its owner processor to my c-list */

      owner = nodep->proc.ownerid;

      if (nodep->isanchored) {
	messenger = ansched->messenger_c[owner];
      } else {
	messenger = dnsched->messenger_c[owner];
      }

      if (messenger == NULL) {
	fprintf(stderr, "Thread %d: schedule_build: ", myID);
	fprintf(stderr, "encounter NULL messenger.\n");
	MPI_Abort(MPI_COMM_WORLD, ERROR);
	exit(1);
      }

      /* Fill in the mapping */
      messenger->mapping[messenger->nidx] = nindex;
      messenger->nidx++;

    } else {
      /* I own this node. Add any sharing processor to my s-list */

      int32link_t *int32link;

      int32link = nodep->proc.share;
      while (int32link != NULL) {
	sharer = int32link->id;

	if (nodep->isanchored) {
	  messenger = ansched->messenger_s[sharer];
	} else {
	  messenger = dnsched->messenger_s[sharer];
	}

	if (messenger == NULL) {
	  fprintf(stderr, "Thread %d: schedule_build: ", myID);
	  fprintf(stderr, "encounter NULL messenger.\n");
	  MPI_Abort(MPI_COMM_WORLD, ERROR);
	  exit(1);
	}

	messenger->mapping[messenger->nidx] = nindex;
	messenger->nidx++;

	/* Move to the next sharing processor */
	int32link = int32link->next;
      }
    }
  }

  return ;
}



/**
 * schedule_delete: Release memory used by a scheduler.
 *
 */
static void schedule_delete(schedule_t *sched)
{
  messenger_t *current, *next;

  /* Release messengers overseeing my contributions */
  current = sched->first_c;
  while (current != NULL) {
    next = current->next;

    messenger_delete(current);
    current = next;
  }

  /* Release messengers overseeing shareing with others */
  current = sched->first_s;
  while (current != NULL) {
    next = current->next;

    messenger_delete(current);
    current = next;
  }

  if (sched->irecvreqs_c != NULL)
    free(sched->irecvreqs_c);
  if (sched->irecvstats_c != NULL)
    free(sched->irecvstats_c);
  free(sched->messenger_c);

  if (sched->irecvreqs_s != NULL)
    free(sched->irecvreqs_s);
  if (sched->irecvstats_s != NULL)
    free(sched->irecvstats_s);
  free(sched->messenger_s);

  free(sched);

  return;
}



/**
 * schedule_prepare: Allocate the memory for data exchange.
 *
 */
static void
schedule_prepare(schedule_t *sched, int32_t c_outsize, int32_t c_insize,
                 int32_t s_outsize, int32_t s_insize)
{
  messenger_t *messenger;

  messenger = sched->first_c;
  while (messenger != NULL) {
    messenger_set(messenger, c_outsize, c_insize);
    messenger = messenger->next;
  }

  messenger = sched->first_s;
  while (messenger != NULL) {
    messenger_set(messenger, s_outsize, s_insize);
    messenger = messenger->next;
  }

  return;

}



/**
 * schedule_senddata: Assemble the proper information for the group
 *                    of messengers managed by a scheduler and send
 *                    the data.
 *
 * direction: CONTRIBUTION or SHARING
 *
 */
static void
schedule_senddata(schedule_t *sched, void *valuetable, int32_t doublesperentry,
                  int32_t direction, int32_t msgtag)
{
  messenger_t *send_messenger, *recv_messenger;
  int32_t irecvcount, irecvnum, bytesize;
  MPI_Request *irecvreqs;
  MPI_Status *irecvstats;

#ifdef DEBUG
  int64_t *gnidp;
#endif /* DEBUG */

  if (direction == CONTRIBUTION) {
    send_messenger = sched->first_c;
    recv_messenger = sched->first_s;
    irecvcount = sched->s_count;
    irecvreqs = sched->irecvreqs_s;
    irecvstats = sched->irecvstats_s;
  } else {
    send_messenger = sched->first_s;
    recv_messenger = sched->first_c;
    irecvcount = sched->c_count;
    irecvreqs = sched->irecvreqs_c;
    irecvstats = sched->irecvstats_c;
  }

  /* Post receives */
  irecvnum = 0;
  while (recv_messenger != NULL) {
    bytesize = recv_messenger->nodecount * recv_messenger->insize;
    MPI_Irecv(recv_messenger->indata, bytesize, MPI_CHAR,
	      recv_messenger->procid, msgtag, MPI_COMM_WORLD,
	      &irecvreqs[irecvnum]);

    irecvnum++;
    recv_messenger = recv_messenger->next;
  }

  /* Asssemble outgoing messages */
  while (send_messenger != NULL) {
    int32_t lnid, idx, entry;
    double *dvalue;
    double *out;

    for (idx = 0; idx < send_messenger->nidx; idx++) {

      lnid = send_messenger->mapping[idx];

      out = (double *)((char *)send_messenger->outdata +
		       send_messenger->outsize * idx);

      dvalue = (double *)valuetable + doublesperentry * lnid;

      for (entry = 0; entry < doublesperentry; entry++)
	*(out + entry) = *(dvalue + entry);

#ifdef DEBUG
      /* For debug, carry the global node id */
      gnidp = (int64_t *)
	((char *)out + doublesperentry * sizeof(double));
      *gnidp = myMesh->nodeTable[lnid].gnid;
#endif /* DEBUG */
    }

    send_messenger = send_messenger->next;
  }

  /* Revisit messengers */
  if (direction == CONTRIBUTION) {
    send_messenger = sched->first_c;
    recv_messenger = sched->first_s;
  } else {
    send_messenger = sched->first_s;
    recv_messenger = sched->first_c;
  }

  /* Send the data */
  while (send_messenger != NULL) {
    bytesize = send_messenger->nodecount * send_messenger->outsize;
    MPI_Send(send_messenger->outdata, bytesize, MPI_CHAR,
	     send_messenger->procid, msgtag, MPI_COMM_WORLD);
    send_messenger = send_messenger->next;
  }

  /* Wait till I receive all the data I want */
  if (irecvcount != 0) {
    MPI_Waitall(irecvcount, irecvreqs, irecvstats);
  }

  while (recv_messenger != NULL) {
    int32_t lnid, idx, entry;
    double *dvalue;
    double *in;

    for (idx = 0; idx < recv_messenger->nidx; idx++) {

      lnid = recv_messenger->mapping[idx];

      in = (double *)((char *)recv_messenger->indata +
		      recv_messenger->insize * idx);

      dvalue = (double *)valuetable + doublesperentry * lnid;

      for (entry = 0; entry < doublesperentry; entry++) {
	if (direction == CONTRIBUTION) {
	  *(dvalue + entry) += *(in + entry);
	} else {
	  /* SHARING, overwrite my local value */
	  *(dvalue + entry) = *(in + entry);
	}
      }

#ifdef DEBUG
      /* For debug, check the global node id */
      gnidp = (int64_t *)
	((char *)in + doublesperentry * sizeof(double));

      if (*gnidp != myMesh->nodeTable[lnid].gnid) {
	fprintf(stderr, "Thread %d: solver_init: gnids do not match\n",
		myID);
	MPI_Abort(MPI_COMM_WORLD, ERROR);
	exit(1);
      }
#endif /* DEBUG */
    } /* for all the incoming data */

    recv_messenger = recv_messenger->next;
  }


  /* MPI_Barrier(MPI_COMM_WORLD);  */

  return;
}


/**
 * Print messenger entry data to the given output stream.
 * The output line contains the following information:
 * "pe_id type c/s rpe nodecount outsize insize"
 *
 * \note Printing is not synchronized accross PEs, so lines can be out of
 * order / interleaved
 */
static int
messenger_print( messenger_t* msg, char type, char cs, FILE* out )
{
  int ret;

  assert( NULL != msg );

  ret = fprintf( out, "msg_info: %d %c %c %d %d %d %d %d\n", myID, type, cs,
		 msg->procid, msg->nodecount, msg->outsize, msg->insize,
		 msg->nodecount * msg->outsize );

  if (ret > 0 || !theSchedulePrintErrorCheckFlag) {
    ret = 0;
  } else {  /* ret <= 0 && theSchedulePrintErrorCheckFlag */
    perror( "Warning! fprintf(...) failed" );
    fprintf( stderr, "PE id = %d, ret = %d, out = 0x%p\n", myID, ret, out );
    fflush( stderr );
  }

  return ret;
}


/**
 * Print the messenger list for a scheduler
 *
 * \note Printing is not synchronized accross PEs, so lines can be out of
 * order / interleaved
 */
static int
schedule_print_messenger_list( schedule_t* sched, messenger_t* msg, int count,
			       char type, char cs, FILE* out )
{
  messenger_t* m_p;
  int my_count;
  int ret = 0;

  assert( NULL != sched );
    
  m_p = msg;


  for (my_count = 0; m_p != NULL && ret >= 0; my_count++, m_p = m_p->next) {
    ret += messenger_print( msg, type, cs, out );
  }


  if (ret < 0) {
    return -1;
  }

  if (my_count != count) {
    fprintf( stderr, "Warning! schedule list count does not match: "
	     "%u %c %c %d %d\n", myID, type, cs, count, my_count );
  }

  return ret;
}


/**
 * Print scheduler communication to a given output stream.
 *
 * Print a line per entry in the schedule_t structure, i.e., an line
 * per messenger_t entry in the list.
 *
 * Each line has the following format:
 * "pe_id type c/s rpe nodecount outsize insize"
 */
static int
schedule_print_detail( schedule_t* sched, char type, FILE* out )
{
  int ret = 0;


  ret += schedule_print_messenger_list( sched, sched->first_c, sched->c_count,
					type, 'c', out );
  ret += schedule_print_messenger_list( sched, sched->first_s, sched->s_count,
					type, 's', out );


  return ret;
}


/**
 * Print scheduler communication to a given output stream.
 *
 * Print a line per entry in the schedule_t structure, i.e., an line
 * per messenger_t entry in the list.
 *
 * Each line has the following format:
 * "pe_id type s_count c_count"
 *
 * \note Printing is not synchronized accross PEs, so lines can be out of
 * order / interleaved
 */
static int
schedule_print( schedule_t* sched, char type, FILE* out )
{
  int ret;

  assert( NULL != sched );
  assert( NULL != out );

  ret = fprintf( out, "sch_info: %u %c %d %d\n", myID, type, sched->c_count,
		 sched->s_count );


  if (ret > 0 || !theSchedulePrintErrorCheckFlag) {
    ret = 0;
  }

  return ret;
}


/**
 * \note Printing is not synchronized accross PEs, so lines can be out of
 * order / interleaved
 */
static int
solver_print_schedules_imp( mysolver_t* solver, FILE* out )
{
  int ret = 0;

  assert( solver != NULL );
  assert( out != NULL );

  MPI_Barrier(MPI_COMM_WORLD);

  /* print the high-level schedule per PE */
  if (myID == 0) { /* print some header information */
    fputs( "# ----------------------------------------------------------\n"
	   "# Content: Solver schedule information\n"
	   "# pe_id d/a s_count c_count\n", out );
  }
  fflush( out );
  fdatasync( fileno( out ) );

  MPI_Barrier( MPI_COMM_WORLD );

  ret += schedule_print( solver->an_sched, 'a', out );
  ret += schedule_print( solver->dn_sched, 'd', out );

  fflush( out );
  fdatasync( fileno( out ) );
  MPI_Barrier( MPI_COMM_WORLD );

  /* print the schedule detail */
  if (myID == 0) { /* print some header information */
    fputs( "\n\n"
	   "# ----------------------------------------------------------\n"
	   "# Content: Solver schedule detail\n"
	   "# pe_id d/a c/s rpe nodecount outsize insize msgsize\n", out );
    fflush( out );
    fdatasync( fileno( out ) );
  }

  MPI_Barrier( MPI_COMM_WORLD );

  ret += schedule_print_detail( solver->an_sched, 'a', out );
  ret += schedule_print_detail( solver->dn_sched, 'd', out );

  fflush( out );
  fdatasync( fileno( out ) );
  MPI_Barrier( MPI_COMM_WORLD );

  if (myID == 0) { /* this is not synchronized, so it might be out of order */
    fputs( "# ----------------------------------------------------------\n"
	   "\n", out );
    fflush( out );
    fdatasync( fileno( out ) );
  }

  MPI_Barrier( MPI_COMM_WORLD );

  return ret;
}


/**
 * Wrapper to print the solver schedules to stdout and a file with
 * the given name.
 *
 * \note Printing is not synchronized accross PEs, so lines can be out of
 * order / interleaved
 */
static int
solver_print_schedules( mysolver_t* solver )
{
  FILE* out;
  int ret = 0;

  if (theSchedulePrintToStdout) {
    /* print schedules to standard output */
    ret += solver_print_schedules_imp( solver, stdout );

    if (ret < 0) {
      fprintf( stderr, "Warning! printing schedules to standard output "
	       "failed for PE %d\n", myID );
    }
  }

  if (theSchedulePrintToFile && (theSchedulePrintFilename != NULL)) {
    /* print schedules to the given file */
    out = fopen( theSchedulePrintFilename, "a" );

    if (NULL == out) { /* this is not fatal */
      fprintf( stderr, "Warning!, PE# %d failed to open output file for "
	       "printing the communication schedule\n", myID );
      return -1;
    }

    ret = solver_print_schedules_imp( solver, out );

    if (ret < 0) {
      fprintf( stderr, "Warning! PE %d could not print schedules "
	       "to file\n", myID );
    }

    fclose( out );
  }

  return ret;
}

/**
 * messenger_new: Allocate and initialize a messenger.
 *
 */
static messenger_t *messenger_new(int32_t procid)
{
  messenger_t *messenger;

  messenger = (messenger_t *)calloc(1, sizeof(messenger_t));

  if (messenger == NULL)
    return NULL;

  messenger->procid = procid;

  return messenger;
}



/**
 * messenger_delete: Release memory used by a messenger.
 *
 */
static void messenger_delete(messenger_t *messenger)
{
  if (messenger == NULL)
    return;

  if (messenger->outdata != NULL)
    free(messenger->outdata);

  if (messenger->indata != NULL)
    free(messenger->indata);

  free(messenger->mapping);

  free(messenger);

  return;
}



/**
 * messenger_set: Free any data memory used by the messenger for
 *                previous communication. And allocate new memory
 *                for the new round of communication.
 *
 */
static void
messenger_set(messenger_t *messenger, int32_t outsize, int32_t insize)
{
  if (messenger->outdata != NULL) {
    free(messenger->outdata);
    messenger->outdata = NULL;
  }

  if (messenger->indata != NULL) {
    free(messenger->indata);
    messenger->indata = NULL;
  }

  messenger->outsize = outsize;
  messenger->insize = insize;

  if (outsize != 0) {
    messenger->outdata = calloc(messenger->nodecount, outsize);
    if (messenger->outdata == NULL) {
      fprintf(stderr, "Thread %d: messenger_set: out of memory\n",
	      myID);
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }
  }

  if (insize != 0) {
    messenger->indata = calloc(messenger->nodecount, insize);
    if (messenger->indata == NULL) {
      fprintf(stderr, "Thread %d: messenger_set: out of memory\n",
	      myID);
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }
  }

  return;
}



/**
 * messenger_countnodes: Count the total number of nodes (that need to
 *                       be communicated) on the messenger link list.
 *
 */
static int32_t
messenger_countnodes(messenger_t *first)
{
  messenger_t *messenger;
  int32_t total;

  total = 0;
  messenger = first;
  while (messenger != NULL) {
    total += messenger->nodecount;
    messenger = messenger->next;
  }

  return total;
}



/**
 * compute_K: compute K1, K2 and K3 for a  linear  elastic,  homogeneous
 *            and isotropic cube; and their off-diagonal matrices.
 *
 *            K1 = Integral(   Del(Phi(j))  * T(Del(Phi(i)))     )
 *            K2 = Integral(   Del(Phi(i))  * T(Del(Phi(j)))     )
 *            K3 = Integral( T(Del(Phi(i))) *   Del(Phi(j))  * I )
 *
 *            where:
 *
 *            T(W) = transpose of W.
 *
 * Note: K offdiagonal has been commented out because is no longer needed
 *       after the changes made on the solution algorith due to the new
 *       form to handle the damping involved terms.
 *       RICARDO June 2006
 *
 */
static void compute_K()
{
  int i, j; /* indices of the block matrices, i for rows, j for columns */
  int k, l; /* indices of 3 x 3 matrices, k for rows, l for columns     */

  double x[3][8]={ {-1,  1, -1,  1, -1,  1, -1, 1} ,
		   {-1, -1,  1,  1, -1, -1,  1, 1} ,
		   {-1, -1, -1, -1,  1,  1,  1, 1} };

  /* Compute K3 first */
  memset(theK3, 0, 8 * 8 * sizeof(fmatrix_t));
  for (i = 0 ;  i < 8; i++) {
    for (j = 0; j <8; j++){
      /* for  each 3 x 3 matrix representing (node i, node j),
	 each of these matrices is diagonal, off-diagonal elements
	 have been set to 0 in memset() */

      fmatrix_t *matPtr;
      matPtr = &theK3[i][j];

      for (k = 0; k < 3; k++) {
	/* set the diagonal values for the 3 x 3 matrix */
	/* Note the rotation of the index */
	double I1, I2, I3;

	I1 = INTEGRAL_1
	  (x[(k + 0) % 3][i], x[(k + 0) % 3][j],
	   x[(k + 1) % 3][i], x[(k + 1) % 3][j],
	   x[(k + 2) % 3][i], x[(k + 2) % 3][j]);

	I2 = INTEGRAL_1
	  (x[(k + 1) % 3][i], x[(k + 1) % 3][j],
	   x[(k + 2) % 3][i], x[(k + 2) % 3][j],
	   x[(k + 0) % 3][i], x[(k + 0) % 3][j]);


	I3 = INTEGRAL_1
	  (x[(k + 2) % 3][i], x[(k + 2) % 3][j],
	   x[(k + 0) % 3][i], x[(k + 0) % 3][j],
	   x[(k + 1) % 3][i], x[(k + 1) % 3][j]);

	matPtr->f[k][k] = I1 + I2 + I3;
      } /* k*/
    } /* j*/
  } /* i */

    /* Compute K1 and K2. They are not diagonal either,
       but they are indeed symmetric globablly */
  for (i = 0; i < 8; i++) {
    for (j = 0; j < 8; j++) {
      /* for  each 3 x 3 matrix representing (node i, node j)*/

      fmatrix_t *matPtr0, *matPtr1;
      matPtr0 = &theK1[i][j];
      matPtr1 = &theK2[i][j];

      for (k = 0; k <  3; k++)
	for (l = 0; l < 3; l++) {
	  if (k == l) {
	    /* Initialize the diagnoal elements */

	    matPtr0->f[k][k] =
	      INTEGRAL_1
	      (x[(k + 0) % 3][i], x[(k + 0) % 3][j],
	       x[(k + 1) % 3][i], x[(k + 1) % 3][j],
	       x[(k + 2) % 3][i], x[(k + 2) % 3][j]);


	    matPtr1->f[k][k] =
	      INTEGRAL_1
	      (x[(k + 0) % 3][j], x[(k + 0) % 3][i],
	       x[(k + 1) % 3][j], x[(k + 1) % 3][i],
	       x[(k + 2) % 3][j], x[(k + 2) % 3][i]);

	  } else {
	    /* Initialize off-diagonal elements */

	    matPtr0->f[k][l] =
	      INTEGRAL_2
	      (x[k][j], x[l][i],
	       x[3 - (k + l)][j], x[3 - (k + l)][i]);

	    matPtr1->f[k][l] =
	      INTEGRAL_2
	      (x[k][i], x[l][j],
	       x[3 - (k + l)][i], x[3 - (k + l)][j]);


	  } /* else */
	} /* for l */
    } /* for j */
  } /* i */

    /* Old code for damping                                    */
    /* Create the off-diagonal matrices                        */
    /* memcpy(theK1_offdiag, theK1, sizeof(double) * 24 * 24); */
    /* memcpy(theK2_offdiag, theK2, sizeof(double) * 24 * 24); */
    /* memcpy(theK3_offdiag, theK3, sizeof(double) * 24 * 24); */
    /* for (i = 0; i <8; i++) {                                */
    /*     theK1_offdiag[i][i].f[0][0] = 0;                    */
    /*     theK1_offdiag[i][i].f[1][1] = 0;                    */
    /*     theK1_offdiag[i][i].f[2][2] = 0;                    */
    /*     theK2_offdiag[i][i].f[0][0] = 0;                    */
    /*     theK2_offdiag[i][i].f[1][1] = 0;                    */
    /*     theK2_offdiag[i][i].f[2][2] = 0;                    */
    /*     theK3_offdiag[i][i].f[0][0] = 0;                    */
    /*     theK3_offdiag[i][i].f[1][1] = 0;                    */
    /*     theK3_offdiag[i][i].f[2][2] = 0;                    */
    /* }                                                       */

    /* New code                                                */
    /* First option to solve double K1 K3 computation in the   */
    /* time steps is to merge both of them in K1 only          */
    /* RICARDO June 2006                                       */
  for ( i = 0; i < 8; i++ )
    {
      for ( j = 0; j < 8; j++ )
	{
	  for ( k = 0; k < 3; k++ )
	    {
	      for ( l = 0; l < 3; l++ )
		{
		  theK1[i][j].f[k][l] += theK3[i][j].f[k][l];
	        }
	    }
	}
    }

  return;
}



/**
 * compute_setflag:
 *
 * - results from the discussion with Leo
 * - set flags as if in the full space.
 * - the main() routine will set the flag properly in case half-space
 *   is desired.
 *
 */
#ifdef BOUNDARY
static char
compute_setflag(tick_t ldb[3], tick_t ruf[3], tick_t p1[3], tick_t p2[3])
{
  char flag;

  flag = 13; /* indicate internal element */

  if (ldb[0] == p1[0])
    flag = 12;
  if (ldb[1] == p1[1])
    flag = 10;
  if (ldb[2] == p1[2])
    flag = 4;

  if (ruf[0] == p2[0])
    flag = 14;
  if (ruf[1] == p2[1])
    flag = 16;
  if (ruf[2] == p2[2])
    flag = 22;


  if(ldb[0] == p1[0] && ldb[1] == p1[1])
    flag = 9;

  if(ruf[0] == p2[0] && ldb[1] == p1[1])
    flag = 11;

  if(ldb[0] == p1[0] && ruf[1] == p2[1])
    flag = 15;

  if(ruf[0] == p2[0] &&   ruf[1] == p2[1])
    flag = 17;


  if (ldb[0] == p1[0] && ldb[2] == p1[2])
    flag = 3;

  if (ruf[0] == p2[0] && ldb[2] == p1[2])
    flag = 5;

  if (ldb[0] == p1[0] && ruf[2] == p2[2])
    flag = 21;

  if (ruf[0] == p2[0] && ruf[2] == p2[2])
    flag = 23;


  if (ldb[1] == p1[1] && ldb[2] == p1[2])
    flag = 1;

  if (ruf[1] == p2[1] && ldb[2] == p1[2])
    flag = 7;

  if (ldb[1] == p1[1] && ruf[2] == p2[2])
    flag = 19;

  if (ruf[1] == p2[1] && ruf[2] == p2[2])
    flag = 25;

  if (ldb[0] == p1[0] && ldb[1] == p1[1] && ldb[2] == p1[2])
    flag = 0;

  if (ruf[0] == p2[0] && (ldb[1] == p1[1]) && ldb[2] == p1[2])
    flag = 2;

  if (ldb[0] == p1[0] && ruf[1] == p2[1] && ldb[2] == p1[2])
    flag = 6;

  if (ruf[0] == p2[0] && ruf[1] == p2[1] && ldb[2] == p1[2])
    flag = 8;

  if (ldb[0] == p1[0] && ldb[1] == p1[1] && ruf[2] == p2[2])
    flag = 18;

  if (ruf[0] == p2[0] && ldb[1] == p1[1] && ruf[2] == p2[2])
    flag = 20;

  if (ldb[0] == p1[0] && ruf[1] == p2[1] && ruf[2] == p2[2])
    flag = 24;

  if (ruf[0] == p2[0] && ruf[1] == p2[1] && ruf[2] == p2[2])
    flag = 26;

  return flag;
}



static const int theIDBoundaryMatrix[27][8] = {
  { 7, 6, 5, 4, 3, 2, 1, 0},
  { 6, 6, 4, 4, 2, 2, 0, 0},
  { 6, 7, 4, 5, 2, 3, 0, 1},
  { 5, 4, 5, 4, 1, 0, 1, 0},
  { 4, 4, 4, 4, 0, 0, 0, 0},
  { 4, 5, 4, 5, 0, 1, 0, 1},
  { 5, 4, 7, 6, 1, 0, 3, 2},
  { 4, 4, 6, 6, 0, 0, 2, 2},
  { 4, 5, 6, 7, 0, 1, 2, 3},
  { 3, 2, 1, 0, 3, 2, 1, 0},
  { 2, 2, 0, 0, 2, 2, 0, 0},
  { 2, 3, 0, 1, 2, 3, 0, 1},
  { 1, 0, 1, 0, 1, 0, 1, 0},
  { 0, 0, 0, 0, 0, 0, 0, 0}, /* 13: internal elements */
  { 0, 1, 0, 1, 0, 1, 0, 1},
  { 1, 0, 3, 2, 1, 0, 3, 2},
  { 0, 0, 2, 2, 0, 0, 2, 2},
  { 0, 1, 2, 3, 0, 1, 2, 3},
  { 3, 2, 1, 0, 7, 6, 5, 4},
  { 2, 2, 0, 0, 6, 6, 4, 4},
  { 2, 3, 0, 1, 6, 7, 4, 5},
  { 1, 0, 1, 0, 5, 4, 5, 4},
  { 0, 0, 0, 0, 4, 4, 4, 4},
  { 0, 1, 0, 1, 4, 5, 4, 5},
  { 1, 0, 3, 2, 5, 4, 7, 6},
  { 0, 0, 2, 2, 4, 4, 6, 6},
  { 0, 1, 2, 3, 4, 5, 6, 7},
};





static void
compute_setboundary(float size, float Vp, float Vs, float rho, int flag,
                    double dashpot[8][3])
{
  int whichNode;
  double scale;

  /* init the damping vector to all zeroes */
  memset(dashpot, 0, sizeof(double) * 8 * 3);

#ifdef HALFSPACE
  flag = (flag < 9) ? flag + 9 : flag;
#endif /* HALFSPACE */

  scale = rho * (size / 2) * (size / 2);

  for (whichNode = 0 ; whichNode < 8; whichNode++) {
    int bitmark, component;

    bitmark = theIDBoundaryMatrix[flag][whichNode];

    switch (bitmark) {
    case 0:
      break;
    case 7:
      /* Three contributing faces */
      dashpot[whichNode][0] = dashpot[whichNode][1]
	= dashpot[whichNode][2] = (Vp + 2 * Vs) * scale;
      break;
    case 3:
    case 5:
    case 6:
      /* Two contributing faces */
      for (component = 0; component < 3; component++)
	dashpot[whichNode][component] =
	  (Vs + ((bitmark & (1<< component)) ? Vp : Vs)) * scale;
      break;
    case 1:
    case 2:
    case 4:
      /* One contributing face */
      for (component = 0; component < 3; component++)
	dashpot[whichNode][component] =
	  ((bitmark & (1<<component)) ? Vp : Vs) * scale;
      break;
    default:
      fprintf(stderr, "SetBoundary: Unknown bitmark. Panic!\n");
      exit(1);
    }
  }

  return;
}
#endif /* BOUNDARY */



/**
 * compute_setab: the base a and b values will be scaled by zeta
 *                specific to each element.
 */
static void compute_setab(double freq, double *aBasePtr, double *bBasePtr)
{

  /* old version which caused overflow  because of the aproximation
     in the derivative */

  double w1, w2, lw1, lw2, sw1, sw2, cw1, cw2;
  double numer, denom;

  if(theTypeOfDamping == RAYLEIGH)
    {

      /* the factors 0.2 and 1 were calibrated heuristically by LEO */
      w1 = 2 * PI * freq *.2;
      w2 = 2 * PI * freq * 1;

      /* logs */
      lw1 = log(w1);
      lw2 = log(w2);

      /* squares */
      sw1 = w1 * w1;
      sw2 = w2 * w2;

      /* cubes */
      cw1 = w1 * w1 * w1;
      cw2 = w2 * w2 * w2;

      /* numerator */
      numer = w1 * w2 *
	( -2 * sw1 * lw2 + 2 * sw1 * lw1 - 2 * w1 * w2 * lw2
	  + 2 * w1 * w2 * lw1 + 3 * sw2 - 3 * sw1
	  - 2 * sw2 * lw2 + 2 * sw2 * lw1);

      /* denominator */
      denom = (cw1 - cw2 + 3 * sw2 * w1 - 3 * sw1 * w2);

      /* the a over zeta target is... */
      *aBasePtr = numer / denom;

      /* new numerator */
      numer = 3 * (2 * w1 * w2 * lw2 - 2 * w1 * w2 * lw1 + sw1 - sw2);

      /* the b over zeta target is... */
      *bBasePtr = numer / denom;

    }
  else if ( theTypeOfDamping == MASS )
    {
      w1 = 2 * PI * freq * .1;  /* these .1 and 8 heuristics */
      w2 = 2 * PI * freq * 8;

      numer = 2 * w2 * w1 * log(w2 / w1);
      denom = w2 - w1;

      *aBasePtr = 1.3*numer / denom;  /* this 1.3 comes out from heuristics */
      *bBasePtr = 0;
    }
  else if ( theTypeOfDamping == NONE )
    {
      *aBasePtr = 0;
      *bBasePtr = 0;
    }

  return;
}



int is_nodeloaded(int32_t iNode, char *onoff){

  int32_t whichByte, whichBit;

  char mask,test;

  whichByte = iNode/8;
  whichBit = 7 - iNode % 8;


  mask = ( char )pow(2,whichBit);

  test = onoff[whichByte] & mask;

  if ( test == mask )

    return 1;

  else

    return 0;

}


/**
 * compute_addforce_s: Add the force due to earthquake source.
 */
static void
compute_addforce_s (int32_t timestep)
{
  int32_t  i, lnid;

  fvector_t* nodalForce;

  /* theAddForceSTime -= MPI_Wtime(); */

  for( i = 0; i <  theNodesLoaded; i++) {
    lnid=theNodesLoadedList[i];
    nodalForce = mySolver->force + lnid;

    nodalForce->f[0] = ( myForces [ i ].x [0] ) * theDeltaTSquared;
    nodalForce->f[1] = ( myForces [ i ].x [1] ) * theDeltaTSquared;
    nodalForce->f[2] = ( myForces [ i ].x [2] ) * theDeltaTSquared;
  }

  /* theAddForceSTime += MPI_Wtime(); */
}

/**
 * isThisVectorZero: determines if all elements in a vector are zero
 *                   returns 1 if one element is not zero,
 *                   and 0 if all are zero
 *
 */
int isThisVectorZero(fvector_t *thisVector)
{
  int i;
	
  for (i = 0; i < 3; i++) {
    /* if ( thisVector->f[i] != 0 ) { */
    if ( fabs(thisVector->f[i]) > 1e-20 ) {
      return 1;
    }
  }

  return 0;
}


/**
 * compute_addforce_e: Compute and add the force due to the element
 *                     stiffness matrices.
 */
static void compute_addforce_e()
{
    fvector_t localForce[8];
    int       i, j;
    int isMyDisp0=1, isMyDeltaDisp0=1;
    int32_t   eindex;

    fvector_t deltaDisp[8];
    edata_t *edata;
    /* theAddForceETime -= MPI_Wtime(); */

    /* loop on the number of elements */
    for (eindex = 0; eindex < myMesh->lenum; eindex++)
    {
        elem_t *elemp;
        e_t    *ep;

        elemp = &myMesh->elemTable[eindex];
        ep = &mySolver->eTable[eindex];

	edata = (edata_t *)elemp->data;

	if( edata->typeofelement >-1){
        
	/* Step 1. calcuate the force due to the elment stiffness */
        memset(localForce, 0, 8 * sizeof(fvector_t));

        /* compute the diff between disp(tm1) and disp(tm2) */

	the_E1_timer -= MPI_Wtime();
        for (i = 0; i < 8; i++) {
	    fvector_t *tm1Disp, *tm2Disp;
            int32_t    lnid;

            lnid = elemp->lnid[i];

            tm1Disp = mySolver->tm1 + lnid;
            tm2Disp = mySolver->tm2 + lnid;

            deltaDisp[i].f[0] = tm1Disp->f[0] - tm2Disp->f[0];
            deltaDisp[i].f[1] = tm1Disp->f[1] - tm2Disp->f[1];
            deltaDisp[i].f[2] = tm1Disp->f[2] - tm2Disp->f[2];
        }
	the_E1_timer += MPI_Wtime();

        /* contribution by node j to node i*/
	the_E2_timer -= MPI_Wtime();
        for (i = 0; i < 8; i++)
        {
            fvector_t *toForce;

            toForce = &localForce[i];

            for (j = 0; j < 8; j++)
            {
                fvector_t *myDisp;
                int32_t    nodeJ;
                fvector_t *myDeltaDisp;

                nodeJ = elemp->lnid[j];
                myDisp = mySolver->tm1 + nodeJ;

		/* contributions by the stiffnes/damping matrix               */

		/* contribution by ( - deltaT_square * Ke * Ut )              */
		/* But if myDisp is zero avoids multiplications               */
		myDeltaDisp = &deltaDisp[j];
		isMyDisp0=isThisVectorZero(myDisp);
		isMyDeltaDisp0=isThisVectorZero( myDeltaDisp);
		
		if( ( isMyDisp0 != 0 ) && (isMyDeltaDisp0 != 0)){
		  FastIV_MultAddMatVec(&theK1[i][j],&theK2[i][j], myDisp, myDeltaDisp,
				       -ep->c1,-ep->c2,-ep->c3,-ep->c4, toForce);
		}
		else if ( isMyDisp0 != 0 ){
		  FastII_MultAddMatVec(&theK1[i][j],&theK2[i][j], myDisp,-ep->c1,-ep->c2, toForce);
		}
		else if ( isMyDeltaDisp0 != 0 ){
		  FastII_MultAddMatVec(&theK1[i][j],&theK2[i][j], myDeltaDisp,-ep->c3,-ep->c4, toForce);
		}		
            }
        }
	the_E2_timer += MPI_Wtime();

        /* Step 2. sum up my contribution to my vertex nodes */

	the_E3_timer -= MPI_Wtime();
        for (i = 0; i < 8; i++) {
	    int32_t lnid;
	    fvector_t *nodalForce;

            lnid = elemp->lnid[i];

            nodalForce = mySolver->force + lnid;
            nodalForce->f[0] += localForce[i].f[0];
            nodalForce->f[1] += localForce[i].f[1];
            nodalForce->f[2] += localForce[i].f[2];
        }
	the_E3_timer += MPI_Wtime();
	};

    } /* for all the elements */

    /* theAddForceETime += MPI_Wtime(); */
 
    return;

}






/**
 * compute_adjust: Either distribute the values from LOCAL dangling nodes
 *                 to LOCAL anchored nodes, or assign values from LOCAL
 *                 anchored nodes to LOCAL dangling nodes.
 *
 */
static void
compute_adjust(void *valuetable, int32_t doublesperentry, int32_t how)
{
  double *vtable = (double *)valuetable;
  int32_t dnindex;

  if (how == DISTRIBUTION) {
    for (dnindex = 0; dnindex < myMesh->ldnnum; dnindex++) {
      dnode_t *dnode;
      double *myvalue, *parentvalue;
      double darray[7]; /* A hack to avoid memory allocation */
      int32link_t *int32link;
      int32_t idx, parentlnid;
#ifdef DEBUG
      int32_t deps = 0;
#endif /* DEBUG */

      dnode = &myMesh->dnodeTable[dnindex];
      myvalue = vtable + dnode->ldnid * doublesperentry;

      for (idx = 0; idx < doublesperentry; idx++) {
	darray[idx] = (*(myvalue + idx)) / dnode->deps;
      }

      /* Distribute my darray value to my anchors */
      int32link = dnode->lanid;
      while (int32link != NULL) {

#ifdef DEBUG
	deps++;
#endif

	parentlnid = int32link->id;
	parentvalue = vtable + parentlnid * doublesperentry;

	for (idx = 0; idx < doublesperentry; idx++) {
	  /* Accumulation the distributed values */
	  *(parentvalue + idx) += darray[idx];
	}

	int32link = int32link->next;
      }

#ifdef DEBUG
      if (deps != (int)dnode->deps) {
	fprintf(stderr, "Thread %d: compute_adjust distri: ", myID);
	fprintf(stderr, "deps don't match\n");
	MPI_Abort(MPI_COMM_WORLD, ERROR);
	exit(1);
      }
#endif /* DEBUG */
    } /* for all my LOCAL dangling nodes */

  } else {
    /* Assign the value of the anchored parents to the dangling nodes*/

    for (dnindex = 0; dnindex < myMesh->ldnnum; dnindex++) {
      dnode_t *dnode;
      double *myvalue, *parentvalue;
      int32link_t *int32link;
      int32_t idx, parentlnid;
#ifdef DEBUG
      int32_t deps = 0;
#endif /* DEBUG */

      dnode = &myMesh->dnodeTable[dnindex];
      myvalue = vtable + dnode->ldnid * doublesperentry;

      /* Zero out the residual values the dangling node might
	 still hold */
      memset(myvalue, 0, sizeof(double) * doublesperentry);

      /* Assign prorated anchored values to a dangling node */
      int32link = dnode->lanid;
      while (int32link != NULL) {

#ifdef DEBUG
	deps++;
#endif

	parentlnid = int32link->id;
	parentvalue = vtable + parentlnid * doublesperentry;

	for (idx = 0; idx < doublesperentry; idx++) {
	  *(myvalue + idx) += (*(parentvalue + idx) / dnode->deps);
	}

	int32link = int32link->next;
      }

#ifdef DEBUG
      if (deps != (int)dnode->deps) {
	fprintf(stderr, "Thread %d: compute_adjust assign: ", myID);
	fprintf(stderr, "deps don't match\n");
	MPI_Abort(MPI_COMM_WORLD, ERROR);
	exit(1);
      }
#endif /* DEBUG */

    } /* for all my LOCAL dangling nodes */

  }

  return;
}





static void print_timing_stat()
{
#ifndef NO_OUTPUT
  double meshoutsize;
#endif /* NO_OUTPUT */

  printf("\n");
  printf("-------------- Running time breakdown ----------------------\n");
  printf("\n");
  printf("Total elements      : %" INT64_FMT "\n", theETotal);
  printf("Simulation duration : %.2f seconds\n", theEndT - theStartT);
  printf("Total steps         : %d\n", theTotalSteps);
  printf("DeltaT used         : %.6f seconds\n", theDeltaT);
  printf("Critical deltaT     : %.6f seconds\n", theCriticalT);
  printf("\n");

  printf("Total running time  : %.2f seconds\n", theE2ETime);

#ifdef USECVMDB
  printf("  Replicating time  : %.2f seconds\n", theDBReplicateTime);
#else
  printf("  Slicing time     : %.2f seconds\n", theSliceCVMTime);
#endif

  printf("  Meshing time      : %.2f seconds\n", theOctorTime);
  printf("  Solving time      : %.2f seconds\n", theSolverTime);

  printf("Time/step           : %.2f second\n", theE2ETime / theTotalSteps);
  printf("Time/step/(elem/PE) : %.6f millisec\n",
	 theE2ETime * 1000.0 / theTotalSteps /
	 (theETotal * 1.0 / theGroupSize));

#ifdef USECVMDB
  printf("\n");
  printf("------------------------------------------------------------\n");
  printf("\n");
  printf("Replicating time    : %.6f seconds\n", theDBReplicateTime);
  printf("Database size       : %"INT64_FMT" bytes\n", (int64_t)theDBSize);
  printf("Transfer rate       : %.2f MB/s\n",
	 theDBSize * 1.0 / (1 << 20) / theDBReplicateTime);
  printf("\n");
#endif /* USECVMDB */

  printf("------------------------------------------------------------\n");
  printf("\n");
  printf("Meshing time        : %.2f seconds\n", theOctorTime);
  printf("  newtree           : %.2f seconds\n", theOctorNewTreeTime);
  printf("  refinetree        : %.2f seconds\n", theOctorRefineTreeTime);
  printf("  balancetree       : %.2f seconds\n", theOctorBalanceTreeTime);
  printf("  balanceload       : %.2f seconds\n", theOctorBalanceLoadTime);
  printf("  extractmesh       : %.2f seconds\n", theOctorExtractMeshTime);
  printf("Time/(elem/PE)      : %.3f millisec\n",
	 theOctorTime * 1000.0 / (theETotal * 1.0 / theGroupSize));

#ifdef USECVMDB
  printf("\n");
  printf("Max CVM qtime (ref) : %.2f seconds\n",
	 theCVMQueryTime_Refinement_MAX);
  printf("Max CVM qtime (bal) : %.2f seconds\n",
	 theCVMQueryTime_Balance_MAX);
  printf("\n");
#endif /* USECVMDB */

  printf("------------------------------------------------------------\n");
  printf("\n");
  printf("Solving time        : %.2f seconds\n", theSolverTime);

  printf("   The minima in solving time:\n");
  printf("       Section B min: %.2f seconds :: planes loop\n",                           theMin_B_timer);
  printf("       Section C min: %.2f seconds :: read my forces\n",                        theMin_C_timer);
  printf("       Section D min: %.2f seconds :: compute addforces s\n",                   theMin_D_timer);
  printf("       Section E min: %.2f seconds :: compute addforces e\n",                   theMin_E_timer);
  printf("           Section E1 min: %.2f seconds :: compute addforces e\n",              theMin_E1_timer);
  printf("           Section E2 min: %.2f seconds :: compute addforces e\n",              theMin_E2_timer);
  printf("           Section E3 min: %.2f seconds :: compute addforces e\n",              theMin_E3_timer);
  printf("       Section F min: %.2f seconds :: 1st schedule send data (contribution)\n", theMin_F_timer);
  printf("       Section G min: %.2f seconds :: 1st compute adjust (distribution)\n",     theMin_G_timer);
  printf("       Section H min: %.2f seconds :: 2nd schedule send data (contribution)\n", theMin_H_timer);
  printf("       Section I min: %.2f seconds :: compute new displacement\n",              theMin_I_timer);
  printf("       Section J min: %.2f seconds :: 3rd schedule send data (sharing)\n",      theMin_J_timer);
  printf("       Section K min: %.2f seconds :: 2nd compute adjust (assignment)\n",       theMin_K_timer);
  printf("       Section L min: %.2f seconds :: 4th schadule send data (sharing)\n",      theMin_L_timer);
  printf("       Section s min: %.2f seconds :: Stations\n\n",                            theMin_s_timer);

  printf("   The maxima in solving time:\n");
  printf("       Section B max: %.2f seconds :: planes loop\n",                           theMax_B_timer);
  printf("       Section C max: %.2f seconds :: read my forces\n",                        theMax_C_timer);
  printf("       Section D max: %.2f seconds :: compute addforces s\n",                   theMax_D_timer);
  printf("       Section E max: %.2f seconds :: compute addforces e\n",                   theMax_E_timer);
  printf("           Section E1 max: %.2f seconds :: compute addforces e\n",              theMax_E1_timer);
  printf("           Section E2 max: %.2f seconds :: compute addforces e\n",              theMax_E2_timer);
  printf("           Section E3 max: %.2f seconds :: compute addforces e\n",              theMax_E3_timer);
  printf("       Section F max: %.2f seconds :: 1st schedule send data (contribution)\n", theMax_F_timer);
  printf("       Section G max: %.2f seconds :: 1st compute adjust (distribution)\n",     theMax_G_timer);
  printf("       Section H max: %.2f seconds :: 2nd schedule send data (contribution)\n", theMax_H_timer);
  printf("       Section I max: %.2f seconds :: compute new displacement\n",              theMax_I_timer);
  printf("       Section J max: %.2f seconds :: 3rd schedule send data (sharing)\n",      theMax_J_timer);
  printf("       Section K max: %.2f seconds :: 2nd compute adjust (assignment)\n",       theMax_K_timer);
  printf("       Section L max: %.2f seconds :: 4th schadule send data (sharing)\n",      theMax_L_timer);
  printf("       Section s max: %.2f seconds :: Stations\n\n",                            theMax_s_timer);

  /*     printf("  Reading source    : %.2f seconds\n", theAbsReadSrcFrcT); */
  /*     printf("  Adding force S    : %.2f seconds\n", theAbsFrcST); */
  /*     printf("  Adding force E    : %.2f seconds\n", theAbsFrcET); */

  printf("Time/step           : %.6f seconds\n", theSolverTime/theTotalSteps);
  printf("Time/step/(elem/PE) : %.6f millisec\n",
	 theSolverTime * 1000.0 / theTotalSteps /
	 (theETotal * 1.0 / theGroupSize));
  printf("\n");
    
    
  printf("------------------------------------------------------------\n");
  printf("\n");
  printf("Planes I_O time     : %.2f seconds\n", thePrintPlaneTime );
  printf("Communication time  : %.2f seconds\n", theCollectPlaneTime );
  printf("\n");
    
#ifndef NO_OUTPUT
  meshoutsize = (theETotal * (sizeof(mdata_t) + 13));

  printf("------------------------------------------------------------\n");
  printf("\n");
  printf("Mesh output time    : %.2f seconds\n", theMeshOutTime);
  printf("Mesh output size    : %.2f MB\n",
	 meshoutsize * 1.0 / (1 << 20));
  printf("Mesh output rate    : %.2f MB/sec\n",
	 meshoutsize * 1.0 / (theMeshOutTime * (1 << 20)));
  printf("4D output time      : %.2f seconds\n", the4DOutTime);
  printf("4D output size      : %.2f MB\n", the4DOutSize / (1 << 20));
  printf("4D output rate      : %.2f MB/sec\n",
	 the4DOutSize / the4DOutTime / (1 << 20));
  printf("\n");
#endif /* NO_OUTPUT */

  printf("------------------------------------------------------------\n");
  printf( "Monitor output time  : % 6.2f sec\n",
	  htimerv_get_total_sec( &timer_monitor ) );
  printf( "Checkpoint write time: % 6.2f sec\n",
	  htimerv_get_total_sec( &timer_checkpoint_write ) );
  printf("------------------------------------------------------------\n");

  fflush (stdout);

  return;
}




/**
 * Prepare data to compute the myForce vector.  It calls compute_force.
 */
static void
source_init( const char *physicsin )
{
  double source_init_time = 0;
  char temporalSource[256], sourceDirectoryOutput[256];

  if (myID == 0) {
    source_init_time = -MPI_Wtime();
  }

  /* Load to theMPIInformation */
  theMPIInformation.myid      = myID;
  theMPIInformation.groupsize = theGroupSize;

  /* Load to theNumericsInforamation */
  theNumericsInformation.numberoftimesteps = theTotalSteps;
  theNumericsInformation.deltat            = theDeltaT;
  theNumericsInformation.validfrequency    = theFreq;

  theNumericsInformation.xlength = theDomainX;
  theNumericsInformation.ylength = theDomainY;
  theNumericsInformation.zlength = theDomainZ;

  /* it will create the files to be read each time step to
     load (force) the mesh */
  if (compute_print_source( physicsin,myOctree,myMesh, theNumericsInformation,
			    theMPIInformation ))
    {
      fprintf(stdout,"Err:cannot create source forces");
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }


  if (myID == 0) {
    FILE *fpphysics;

    if ((fpphysics = fopen(physicsin, "r")) == NULL) {
      fprintf(stderr, "Error opening %s\n", physicsin);
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }

    if ( ( parsetext( fpphysics, "source_directory_output", 's',
		      &sourceDirectoryOutput ) ) ){
      fprintf(stderr, "Err prs domain || src dir:  %s\n ", physicsin);
      MPI_Abort(MPI_COMM_WORLD, ERROR);
      exit(1);
    }

    fclose( fpphysics );
  }

  MPI_Bcast( sourceDirectoryOutput, 256, MPI_CHAR, 0, MPI_COMM_WORLD );
  sprintf(temporalSource, "%s/force_process.%d", sourceDirectoryOutput, myID);
  fpsource = fopen( temporalSource, "r" );

  if (fpsource == NULL) {
    fprintf(stderr, "Error opening %s\n",temporalSource );
    MPI_Abort(MPI_COMM_WORLD, ERROR);
    exit(1);
  }

  hu_fread( &theNodesLoaded, sizeof(int32_t), 1, fpsource );

  MPI_Barrier( MPI_COMM_WORLD );

  if (theNodesLoaded != 0) {
    size_t ret;

    theNodesLoadedList = malloc( sizeof(int32_t) * theNodesLoaded );
    myForces           = calloc( theNodesLoaded, sizeof(vector3D_t) );

    if (myForces == NULL || theNodesLoadedList == NULL) {
      solver_abort( "source_init", "memory allocation failed",
		    "Cannot allocate memory for myForces or "
		    "loaded nodes list arrays\n" );
    }

    ret = fread( theNodesLoadedList, sizeof(int32_t), theNodesLoaded,
		 fpsource );

    if (ret != theNodesLoaded) {
      solver_abort( "source_init(", "fread failed",
		    "Could not read nodal force file");
    }
  }

  if (myID == 0) {
    source_init_time += MPI_Wtime();
    printf( "\n\nTotal Source Init Time: %.2fs\n", source_init_time );
    fflush( stdout );
  }
}



/**
 * Search a point in the domain of the local mesh.
 *
 *   input: coordinates
 *  output: 0 fail 1 success
 */
static int32_t
search_point( vector3D_t point, octant_t **octant )
{
  tick_t  xTick, yTick, zTick;

  xTick = point.x[0] / myMesh->ticksize;
  yTick = point.x[1] / myMesh->ticksize;
  zTick = point.x[2] / myMesh->ticksize;

  *octant = octor_searchoctant( myOctree, xTick, yTick, zTick,
				PIXELLEVEL, AGGREGATE_SEARCH );

  if ( (*octant == NULL) || ((*octant)->where == REMOTE) ) {
    return 0;
  }

  return 1;
}

/**
 *  compute_eta_csi_dzeta;
 *   input: octant where the point is located;
 *          coords of the point
 *  output: the displacment
 *
 */
static double
compute_csi_eta_dzeta( octant_t *octant, vector3D_t pointcoords,
		       vector3D_t *localcoords, int32_t *localNodeID )
{
  tick_t  edgeticks;
  int32_t eindex;
  double  center_x, center_y, center_z;

  /* various convienient variables */
  double xGlobal = pointcoords.x[0];
  double yGlobal = pointcoords.x[1];
  double zGlobal = pointcoords.x[2];
  double h;

  edgeticks = (tick_t)1 << (PIXELLEVEL - octant->level);
  h =  myMesh->ticksize * edgeticks;

  /* Calculate the center coordinate of the element */
  center_x = myMesh->ticksize * (octant->lx + edgeticks / 2);
  center_y = myMesh->ticksize * (octant->ly + edgeticks / 2);
  center_z = myMesh->ticksize * (octant->lz + edgeticks / 2);



  /* Go through my local elements to find which one matches the
     containing octant. I should have a better solution than this.*/

  for (eindex = 0; eindex < myMesh->lenum; eindex++) {
    int32_t lnid0;

    lnid0 = myMesh->elemTable[eindex].lnid[0];

    if ((myMesh->nodeTable[lnid0].x == octant->lx) &&
	(myMesh->nodeTable[lnid0].y == octant->ly) &&
	(myMesh->nodeTable[lnid0].z == octant->lz)) {

      /* Sanity check */
      if (myMesh->elemTable[eindex].level != octant->level) {
	fprintf(stderr, "Thread %d: source_init: internal error\n",
		myID);
	MPI_Abort(MPI_COMM_WORLD, ERROR);
	exit(1);
      }

      /* Fill in the local node ids of the containing element */
      memcpy(localNodeID, myMesh->elemTable[eindex].lnid,
	     sizeof(int32_t) * 8);

      break;
    }
  }  /* for all the local elements */


  if (eindex == myMesh->lenum) {
    fprintf(stderr, "Thread %d: source_init: ", myID);
    fprintf(stderr, "No element matches the containing octant.\n");
    MPI_Abort(MPI_COMM_WORLD, ERROR);
    exit(1);
  }


  /* Derive the local coordinate of the source inside the element */

  localcoords->x[0] =  2*(xGlobal- center_x)/h;
  localcoords->x[1] =  2*(yGlobal- center_y)/h;
  localcoords->x[2] =  2*(zGlobal- center_z)/h;

  return h;

}
    
 
void open_write_header_planedisplacements(int typeofheader,int iplane){

  int i,isascii;
  char planedisplacementsout[1024];
  FILE *fp;
 
  isascii=0;
  /* open displacement output files and write header */
  sprintf (planedisplacementsout, "%s/planedisplacements.%d",thePlaneDirOut, iplane);
  fp = fopen (planedisplacementsout, "w");
  if(fp==NULL)
    solver_abort("open_write_header_planedisplacements()",
		 planedisplacementsout,"ErrOp planedisplacement F:\'%s\'", 
		 planedisplacementsout);
  thePlanes[iplane].fpoutputfile = fp;
	    
  /* Write the domain coordinate and lengths of the simulation */
  for (i=0; i<4; i++){
    fwrite(&(theSurfaceCornersLong[i]),sizeof(double),1,fp);
    fwrite(&(theSurfaceCornersLat[i] ),sizeof(double),1,fp);
  }		    
  
  /*theDomainY,theDomainX)*/
  fwrite(&(theDomainX),sizeof(double),1,fp);
  fwrite(&(theDomainY),sizeof(double),1,fp);
  
 if(typeofheader==0){
   /*origin of the plane */
   fwrite(&(thePlanes[iplane].originlon) ,sizeof(double),1,fp);
   fwrite(&(thePlanes[iplane].originlat) ,sizeof(double),1,fp);
   
   fwrite(&(thePlanes[iplane].stepalongstrike)         ,sizeof(double),1,fp);
   fwrite(&(thePlanes[iplane].numberofstepsalongstrike),sizeof(int   ),1,fp);
   fwrite(&(thePlanes[iplane].stepdowndip)             ,sizeof(double),1,fp);
   fwrite(&(thePlanes[iplane].numberofstepsdowndip)    ,sizeof(int   ),1,fp);
   fwrite(&theDeltaT,sizeof(double),1,fp);
   fwrite(&thePlanePrintRate,sizeof(int),1,fp);
 }    
 if(typeofheader==1){
   if(isascii==0){
    fwrite(&(thePlanes[iplane].numberofstepsalongstrike),sizeof(int   ),1,fp);
    fwrite(&(thePlanes[iplane].numberofstepsdowndip)    ,sizeof(int   ),1,fp);
    fwrite(&theDeltaT,sizeof(double),1,fp);
    fwrite(&thePlanePrintRate,sizeof(int),1,fp);
   }else{
     fprintf(fp,"\n %d %d %lf %d", thePlanes[iplane].numberofstepsalongstrike,
	     thePlanes[iplane].numberofstepsdowndip,theDeltaT,thePlanePrintRate);

   }
 }    

}


void
print_planecoords()
{
    int iPlane, iStrike, iDownDip;
    double xLocal, yLocal,topo,lon,lat;
    vector3D_t origin, pointLocal, pointGlobal;

    for (iPlane = 0; iPlane < theNumberOfPlanes ;iPlane++) {

	/* Compute the coordinates of the nodes of the plane */
	origin.x[0] = thePlanes[ iPlane ].origincoords.x[0];
	origin.x[1] = thePlanes[ iPlane ].origincoords.x[1];
	origin.x[2] = thePlanes[ iPlane ].origincoords.x[2];

	//fprintf(stdout, " \n %e %e %e ",origin.x[0],origin.x[1], origin.x[2]);
	//        fflush(stdout);
	//	exit(1);


	for (iStrike = 0; iStrike < thePlanes[iPlane].numberofstepsalongstrike;
	     iStrike++)
	{
	    xLocal = iStrike*thePlanes[iPlane].stepalongstrike;

	    for (iDownDip = 0; iDownDip<thePlanes[iPlane].numberofstepsdowndip;
		 iDownDip++)
	    {

		yLocal = iDownDip*thePlanes[ iPlane ].stepdowndip;
		pointLocal.x[0] = xLocal;
		pointLocal.x[1] = yLocal;
		pointLocal.x[2] = 0;

		pointGlobal.x[0]=xLocal;
		pointGlobal.x[1]=yLocal;
		pointGlobal.x[2]=0;

		pointGlobal =
		    compute_global_coords (origin, pointLocal,
					   thePlanes[ iPlane ].dip, 0,
					   thePlanes[ iPlane ].strike );


		compute_lonlat_from_domain_coords_linearinterp( yLocal+origin.x[1],
								xLocal+origin.x[0], 
								&lon, &lat,
								theSurfaceCornersLong,
								theSurfaceCornersLat,
								theDomainX, theDomainY);

	       
		Query_Topo_Bedrock_DepthToBedrock (lat,lon, &topo , &database,0);
		pointGlobal.x[2]=topo;


		//fprintf (thePlanes[iPlane].fpplanecoordsfile, "\n %e %e  %e  %e  %e ",
		//	 lon,lat,pointGlobal.x[0], pointGlobal.x[1], pointGlobal.x[2]);
		//		fprintf (stdout, "\n %e  %e  %e ",
		//			 pointGlobal.x[0], pointGlobal.x[1], pointGlobal.x[2]);
		//fflush(stdout);


	    } /* for (iDownDip ...) */
	} /* for (iStrike ....) */

	//fclose (thePlanes[ iPlane ].fpplanecoordsfile);
    } /* for (iPlane ... ) */
} /* print_planecoords() */








/*
  This is the stripped BigBen version.  It is simplified:
  1) It assumes all output is PE0
  2) It does not have a strip size limit
*/
static void
output_planes_setup(const char *numericalin)
{

  static const char* fname = "output_planes_setup()";
  double     *auxiliar;
  char       planecoordsfile[1024],planeDirOutCoordsInput[1024],
             planeFileOutCoordsInput[1024] ;
  int        iPlane, iCorner;
  int        nI,nJ,iC; /* array in case fake plane*/
  int        found;
  int fret0, fret1;
  double     *latIn,*lonIn,*depthIn;
  vector3D_t originPlaneCoords;
  int largestplanesize, typePlaneInput;
  FILE* fp;
  FILE* fpI;
  FILE *fp0, *fp1;
    
  if (myID == 0) {
    /* Obtain the planes specifications */
    if ( (fp = fopen ( numericalin, "r")) == NULL ) {
      solver_abort (fname, numericalin,
		    "Error opening numerical.in configuration file");
    }
    if ( (parsetext(fp, "output_planes_print_rate", 'i',
		    &thePlanePrintRate) != 0) )
      {
	solver_abort (fname, NULL, "Error parsing output_planes_print_rate field from %s\n",
		      numericalin);
      }
    auxiliar = (double *)malloc(sizeof(double)*8);
    if ( parsedarray( fp, "domain_surface_corners", 8 ,auxiliar) !=0 ) {
      solver_abort (fname, NULL, "Error parsing domain_surface_corners field from %s\n",
		    numericalin);
    }
    for ( iCorner = 0; iCorner < 4; iCorner++){
      theSurfaceCornersLong[ iCorner ] = auxiliar [ iCorner * 2 ];
      theSurfaceCornersLat [ iCorner ] = auxiliar [ iCorner * 2 +1 ];
    }
    free(auxiliar);

    if ((parsetext(fp, "output_planes_directory", 's',
		   &thePlaneDirOut) != 0))
      {
	solver_abort( fname, NULL,
		      "Error parsing output planes directory from %s\n",
		      numericalin );
      }
    thePlanes = (plane_t *) malloc ( sizeof( plane_t ) * theNumberOfPlanes);
    if ( thePlanes == NULL ) {
      solver_abort( fname, "Allocating memory for planes array",
		    "Unable to create plane information arrays" );
    }

    /* Differentiate the type of planes input */

    if ((parsetext(fp, "plane_coords_type_input", 'i',
		   &typePlaneInput) != 0)); /* if 0-true plane 1-fake plane*/                 


    for ( iPlane = 0; iPlane < theNumberOfPlanes; iPlane++ )
      thePlanes[iPlane].typeplaneinput=typePlaneInput;
    

    if(typePlaneInput==0){

      /* locate line where output_planes indicator is and read array */
      rewind (fp);
      found=0;
      while (!found) {
	char line[LINESIZE], delimiters[] = " =\n",querystring[] = "output_planes";
	char *name;
	/* Read in one line */
	if (fgets (line, LINESIZE, fp) == NULL) {
	  break;
	}
	name = strtok(line, delimiters);
	if ( (name != NULL) && (strcmp(name, querystring) == 0) ) {
	  largestplanesize = 0;
	  found = 1;
	  for ( iPlane = 0; iPlane < theNumberOfPlanes; iPlane++ ) {
	    fret0=fscanf(fp,"%lf %lf %lf", &thePlanes[iPlane].origincoords.x[0],&thePlanes[iPlane].origincoords.x[1],
			 &thePlanes[iPlane].origincoords.x[2]);	 	     
	    /* Origin coordinates must be given in Lat, Long and depth */
	    if (fret0 == 0) solver_abort (fname, NULL,"Unable to read planes origin in %s", numericalin);
	    /* convert to cartesian refered to the mesh */
	    thePlanes[iPlane].originlat=thePlanes[ iPlane ].origincoords.x[0];
	    thePlanes[iPlane].originlon=thePlanes[ iPlane ].origincoords.x[1];
	    
	    originPlaneCoords = compute_domain_coords_linearinterp(thePlanes[ iPlane ].origincoords.x[1],
	    thePlanes[ iPlane ].origincoords.x[0], theSurfaceCornersLong , theSurfaceCornersLat,theDomainY, theDomainX);
	    thePlanes[iPlane].origincoords.x[0] = originPlaneCoords.x[0];
	    thePlanes[iPlane].origincoords.x[1] = originPlaneCoords.x[1];
	    fret1 = fscanf (fp," %lf %d %lf %d %lf %lf", &thePlanes[iPlane].stepalongstrike,
                   &thePlanes[iPlane].numberofstepsalongstrike,&thePlanes[iPlane].stepdowndip,
			    &thePlanes[iPlane].numberofstepsdowndip,&thePlanes[iPlane].strike,&thePlanes[iPlane].dip);
	    if (fret1 == 0)solver_abort (fname, NULL, "Unable to read plane specification in %s",numericalin);

	    /*Find largest plane for output buffer allocation */
	    if ( (thePlanes[iPlane].numberofstepsdowndip * thePlanes[iPlane].numberofstepsalongstrike)
		 > largestplanesize)
	      largestplanesize = thePlanes[iPlane].numberofstepsdowndip *
		thePlanes[iPlane].numberofstepsalongstrike;
	  
	    open_write_header_planedisplacements(0,iPlane);

	    sprintf (planecoordsfile, "%s/planecoords.%d",
		     thePlaneDirOut, iPlane);
	    fp1 = fopen (planecoordsfile, "w");
	    if(fp1==NULL)solver_abort(fname,planecoordsfile,
	       "Error opening plane coordinates file: '%s'",planecoordsfile);	    
	    thePlanes[iPlane].fpplanecoordsfile = fp1;

	  } /* for */
	} /* if ( (name != NULL) ... ) */
      } /* while */     
    }/*  if(typePlaneInput==0) */ 
    else{
      /* Read the name where the files with coords will be at */
      if ((parsetext(fp, "input_planes_coords_directory", 's', &planeDirOutCoordsInput) != 0)) 
	solver_abort( fname, NULL,"\nError parsing input_planes_coords_directory %s\n", numericalin);
      
      largestplanesize = 0;
      
      for ( iPlane = 0; iPlane < theNumberOfPlanes; iPlane++ ){
	sprintf (planeFileOutCoordsInput, "%s/planeinputcoords.%d",planeDirOutCoordsInput, iPlane);

	if((fpI = fopen (planeFileOutCoordsInput,"r"))==NULL) 
	  solver_abort(fname,NULL,"Error opening planeFileOutCoordsInput file");

	fscanf(fpI," %d %d %d ",&(thePlanes[iPlane].fieldtooutput),
	                        &(thePlanes[iPlane].numberofstepsalongstrike),
	                        &(thePlanes[iPlane].numberofstepsdowndip));

	/*allocate the memory */
	nI=thePlanes[iPlane].numberofstepsalongstrike;
	nJ=thePlanes[iPlane].numberofstepsdowndip;
	thePlanes[iPlane].latIn   = (double *)malloc(sizeof(double)*(nI*nJ));
	thePlanes[iPlane].lonIn   = (double *)malloc(sizeof(double)*(nI*nJ));
	thePlanes[iPlane].depthIn = (double *)malloc(sizeof(double)*(nI*nJ));
	thePlanes[iPlane].vp  = (double *)malloc(sizeof(double)*(nI*nJ));
	thePlanes[iPlane].vs  = (double *)malloc(sizeof(double)*(nI*nJ));
	thePlanes[iPlane].rho = (double *)malloc(sizeof(double)*(nI*nJ));
	thePlanes[iPlane].elementtype = (double *)malloc(sizeof(double)*(nI*nJ));

	for (iC=0; iC < (nI*nJ); iC++)
	  fscanf(fpI," %lf %lf %lf ",&(thePlanes[iPlane].latIn[iC]),&(thePlanes[iPlane].lonIn[iC]),
                                     &(thePlanes[iPlane].depthIn[iC]));

	/*Find largest plane for output buffer allocation */
	if ( (thePlanes[iPlane].numberofstepsdowndip * thePlanes[iPlane].numberofstepsalongstrike)
	     > largestplanesize)
	  largestplanesize = thePlanes[iPlane].numberofstepsdowndip *
	    thePlanes[iPlane].numberofstepsalongstrike;

	open_write_header_planedisplacements(1,iPlane);

	sprintf (planecoordsfile, "%s/planecoords.%d",
		 thePlaneDirOut, iPlane);
	fp1 = fopen (planecoordsfile, "w");
	if(fp1==NULL)solver_abort(fname,planecoordsfile,
			  "Error opening plane coordinates file: '%s'",planecoordsfile);	    
	    thePlanes[iPlane].fpplanecoordsfile = fp1;	
      } /* for */      
    }/*  if(typePlaneInput==0) else*/     
  } /* if (myID == 0) */ 


  MPI_Barrier(MPI_COMM_WORLD);
 
   /*broadcast plane info*/
  MPI_Bcast( &theNumberOfPlanes, 1, MPI_INT, 0, MPI_COMM_WORLD );
  MPI_Bcast( &thePlanePrintRate, 1, MPI_INT, 0, MPI_COMM_WORLD );
  MPI_Barrier(MPI_COMM_WORLD);

  /*initialize the local structures */
  if (myID != 0) {
    thePlanes = (plane_t *) malloc( sizeof( plane_t ) * theNumberOfPlanes );
    if (thePlanes == NULL)
      solver_abort( "broadcast_planeinfo", NULL,"Error: Unable to create plane information arrays" );
  }

  MPI_Barrier(MPI_COMM_WORLD);


  for ( iPlane = 0; iPlane < theNumberOfPlanes; iPlane++ ) {
   
    MPI_Bcast( &(thePlanes[iPlane].numberofstepsalongstrike), 2,
	       MPI_INT, 0, MPI_COMM_WORLD);    
    MPI_Bcast( &(thePlanes[iPlane].typeplaneinput), 1,
	       MPI_INT, 0, MPI_COMM_WORLD);
  }

  MPI_Barrier(MPI_COMM_WORLD);
  //  fprintf(stdout,"\nBefore  initialize lonlat, pe= %d",myID);
  // MPI_Barrier(MPI_COMM_WORLD);
  

  for ( iPlane = 0; iPlane < theNumberOfPlanes; iPlane++ ) {   
    
    if(thePlanes[iPlane].typeplaneinput==0){
      MPI_Bcast( &(thePlanes[iPlane].stepalongstrike), 5,
		 MPI_DOUBLE,0, MPI_COMM_WORLD);
      MPI_Bcast( &(thePlanes[iPlane].origincoords.x[0]), 3,
		 MPI_DOUBLE,0, MPI_COMM_WORLD);

      MPI_Barrier(MPI_COMM_WORLD);
    }else{
      MPI_Bcast( &(thePlanes[iPlane].fieldtooutput), 1,
		 MPI_INT, 0, MPI_COMM_WORLD);       
      if (myID != 0) {
	nI=thePlanes[iPlane].numberofstepsalongstrike;
	nJ=thePlanes[iPlane].numberofstepsdowndip;
	thePlanes[iPlane].latIn   = (double *)malloc(sizeof(double)*(nI*nJ));
	thePlanes[iPlane].lonIn   = (double *)malloc(sizeof(double)*(nI*nJ));
	thePlanes[iPlane].depthIn = (double *)malloc(sizeof(double)*(nI*nJ));
	thePlanes[iPlane].vp  = (double *)malloc(sizeof(double)*(nI*nJ));
	thePlanes[iPlane].vs  = (double *)malloc(sizeof(double)*(nI*nJ));
	thePlanes[iPlane].rho = (double *)malloc(sizeof(double)*(nI*nJ));
	thePlanes[iPlane].elementtype = (double *)malloc(sizeof(double)*(nI*nJ));


      }
    }  
  }
  
  //fprintf(stdout,"\nBefore bcast, pe= %d",myID);
  MPI_Barrier(MPI_COMM_WORLD);
  
  typePlaneInput=thePlanes[iPlane].typeplaneinput;
  
 
    
  for ( iPlane = 0; iPlane < theNumberOfPlanes; iPlane++ ) { 
    typePlaneInput=thePlanes[iPlane].typeplaneinput;
    if( typePlaneInput ==1){
      
      nI=thePlanes[iPlane].numberofstepsalongstrike;
      nJ=thePlanes[iPlane].numberofstepsdowndip;
      MPI_Bcast( (thePlanes[iPlane].latIn),(nI*nJ), MPI_DOUBLE,0, MPI_COMM_WORLD);
      MPI_Bcast( (thePlanes[iPlane].lonIn),(nI*nJ), MPI_DOUBLE,0, MPI_COMM_WORLD);   
      MPI_Bcast( (thePlanes[iPlane].depthIn),(nI*nJ), MPI_DOUBLE,0, MPI_COMM_WORLD);
    }
  }
 
  //printf(stdout,"\nBefore strips, pe= %d",myID);

 MPI_Barrier(MPI_COMM_WORLD);

 output_planes_construct_strips();

 //printf(stdout,"\n After strips, pe= %d",myID);


     
  /*Master alloc largest plane*/
  if (myID == 0) {
    planes_output_buffer = (double *) malloc( 3 * sizeof(double) * largestplanesize );
    if(planes_output_buffer == NULL){
      fprintf(stderr, "Error creating buffer for plane output\n");
      exit(1);
    }
  }

  
  /*original routine - we no longer call this*/
  //if (myID == 0) {
  //  print_planecoorpds();
  //  fflush (stdout);
  // }
        
  return;
}


double theVpPlane;
double theVsPlane;
double theRhoPlane;
double theTypeOfElement;


/*This builds all of the strips used in the planes output
  BigBen version does not have strip size limits
*/
static void output_planes_construct_strips()
{

  int iNode, iPlane, iStrike, iDownDip;
  double xLocal, yLocal,lon,lat,topo,depth;
  vector3D_t origin, pointLocal, pointGlobal;
  octant_t *octant;
  int onstrip ;
  int32_t nodesToInterpolate[8];
  int planetypesurface=1;
  int iCount=0;
  elem_t  *elemp;
  edata_t *edata;
  int32_t eindex;
   int32_t lnid0;  

  // fprintf(stdout, "Before Starting Limits strips done myID=%d",myID);
    MPI_Barrier(MPI_COMM_WORLD);

  planes_LocalLargestStripCount = 0;

  for ( iPlane = 0; iPlane < theNumberOfPlanes ;iPlane++ ){
    thePlanes[ iPlane ].numberofstripsthisplane = 0;
    /* Compute the coordinates of the nodes of the plane */
    origin.x[0] = thePlanes[ iPlane ].origincoords.x[0]; origin.x[1] = thePlanes[ iPlane ].origincoords.x[1];
    origin.x[2] = thePlanes[ iPlane ].origincoords.x[2];
    //    fprintf(stdout, "\nIn the loop  Starting Limits strips done myID=%d %d",myID,thePlanes[iPlane].numberofstepsalongstrike);
    //    fprintf(stdout, "\nIn the loop  Starting Limits strips done myID=%d %d",myID,thePlanes[iPlane].numberofstepsdowndip);


    MPI_Barrier(MPI_COMM_WORLD);

    /*Find limits of consecutive strips*/
    onstrip = 0;
    iCount=0;
    for ( iStrike = 0; iStrike < thePlanes[iPlane].numberofstepsalongstrike; iStrike++ ){
      xLocal = iStrike*thePlanes[iPlane].stepalongstrike;
      for ( iDownDip = 0; iDownDip < thePlanes[iPlane].numberofstepsdowndip; iDownDip++ ) {
	if(thePlanes[iPlane].typeplaneinput==0){
	  yLocal = iDownDip*thePlanes[ iPlane ].stepdowndip;
	  pointLocal.x[0] =xLocal;  pointLocal.x[1] =yLocal;  pointLocal.x[2] =0;	  
	  pointGlobal.x[0]=xLocal;  pointGlobal.x[1]=yLocal;  pointGlobal.x[2]=0;	  	  
	  pointGlobal=compute_global_coords(origin,pointLocal,thePlanes[ iPlane ].dip,0,thePlanes[ iPlane ].strike );
	  compute_lonlat_from_domain_coords_linearinterp( yLocal+origin.x[1], xLocal+origin.x[0], 
	  						  &lon, &lat,theSurfaceCornersLong,theSurfaceCornersLat,
	  						  theDomainX, theDomainY);
	  Query_Topo_Bedrock_DepthToBedrock (lat,lon, &topo , &database,0);
	  pointGlobal.x[2]=topo;
	}else{
	  //	  fprintf(stdout, "\nInside Else myID=%d %d %d",myID,iStrike,iDownDip);

	  iCount=iDownDip+thePlanes[iPlane].numberofstepsdowndip*(iStrike);  // review
	  


	  lon=thePlanes[iPlane].lonIn[iCount];lat=thePlanes[iPlane].latIn[iCount];
	  //	  fprintf(stdout, "\nLon myID=%d %d %d %lf %lf ",myID,iStrike,iDownDip,lon,lat);

	  depth=thePlanes[iPlane].depthIn[iCount];	  
	  pointGlobal= compute_domain_coords_linearinterp(lon,lat,theSurfaceCornersLong,
                                  theSurfaceCornersLat,theDomainY,theDomainX);
	  //	  fprintf(stdout, "\nBefor querytopo myID=%d %d %d",myID,iStrike,iDownDip);


	  Query_Topo_Bedrock_DepthToBedrock (lat,lon, &topo , &database,0);
	  pointGlobal.x[2]=topo+depth;	 
	}
      

	//	fprintf(stdout, "\nInside Starting1 Limits strips myID=%d %d %d",myID,iStrike,iDownDip);	
	MPI_Barrier(MPI_COMM_WORLD);	   
	if( search_point( pointGlobal, &octant ) == 1 ){
	  
	  for ( eindex = 0; eindex < myMesh->lenum; eindex++ ) {
    	   
	    lnid0 = myMesh->elemTable[eindex].lnid[0];
	    
	    if ( ( myMesh->nodeTable[lnid0].x == octant->lx ) &&
		 ( myMesh->nodeTable[lnid0].y == octant->ly ) &&
		 ( myMesh->nodeTable[lnid0].z == octant->lz ) ) {
	      
	      elemp = &myMesh->elemTable[eindex];  
	      edata = (edata_t *)elemp->data;  	     
	      thePlanes[iPlane].vp[iCount] = edata->Vp;
	      thePlanes[iPlane].vs[iCount] = edata->Vs;
	      thePlanes[iPlane].rho[iCount]= edata->rho;
	      thePlanes[iPlane].elementtype[iCount]=edata->typeofelement;
	    }
	  }
	}else{
	      thePlanes[iPlane].vp[iCount] = -1e10;
	      thePlanes[iPlane].vs[iCount] = -1e10;
	      thePlanes[iPlane].rho[iCount]= -1e10;
	      thePlanes[iPlane].elementtype[iCount]=-1e10;

	}      



	

	/*Compute_Surface(pointGlobal.x[0]-origin.x[0],pointGlobal.x[1]-origin.x[1], &database);*/
	if ( search_point( pointGlobal, &octant ) == 1 ){
	  if (!onstrip){  /*Start new strip*/
	    thePlanes[ iPlane ].stripstart[thePlanes[ iPlane ].numberofstripsthisplane] =
	      iStrike * thePlanes[iPlane].numberofstepsdowndip + iDownDip;
	    onstrip = 1;
	  }
	}
	else{
	  if(onstrip){  /*Close strip */
	    onstrip = 0;
	    thePlanes[ iPlane ].stripend[thePlanes[ iPlane ].numberofstripsthisplane] =
	      iStrike * thePlanes[iPlane].numberofstepsdowndip + iDownDip - 1;
	    thePlanes[ iPlane ].numberofstripsthisplane++;
	    if(thePlanes[ iPlane ].numberofstripsthisplane > MAX_STRIPS_PER_PLANE){
	      fprintf(stderr, "Number of strips on plane exceeds MAX_STRIPS_PER_PLANE\n");
	      exit(1);
	    }	 
	  }
	}
      }
    }
 
    //   fprintf(stdout, "Limits strips done myID=%d",myID);
    MPI_Barrier(MPI_COMM_WORLD);

    //if(myID ==0){
    //  fclose(thePlanes[iPlane].fpplanecoordsfile);
    //}

    if (onstrip){  /*if on a strip at end of loop, close strip */
      thePlanes[ iPlane ].stripend[thePlanes[ iPlane ].numberofstripsthisplane] =
	(thePlanes[iPlane].numberofstepsdowndip * thePlanes[iPlane].numberofstepsalongstrike) - 1;
      thePlanes[ iPlane ].numberofstripsthisplane++;
    }

   

    /*Get strip counts to IO PEs*/
    MPI_Reduce (&thePlanes[ iPlane ].numberofstripsthisplane,&thePlanes[iPlane].globalnumberofstripsthisplane,
		1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);





    /* Allocate Strips */
    int stripnum, stripLength;
    for (stripnum = 0; stripnum < thePlanes[ iPlane ].numberofstripsthisplane; stripnum++){
      stripLength = thePlanes[iPlane].stripend[stripnum] - thePlanes[iPlane].stripstart[stripnum] +1;

      thePlanes[ iPlane ].strip[stripnum] = (plane_strip_element_t *) malloc(sizeof(plane_strip_element_t) * stripLength);
      if(thePlanes[ iPlane ].strip[stripnum] == NULL){
	fprintf(stderr, "Error malloc'ing array strips for plane output\n"
		"PE: %d  Plane: %d  Strip: %d  Size: $d\n",
		myID, iPlane, stripnum, sizeof(plane_strip_element_t) * stripLength);
	exit(1);
      }
      if (stripLength>planes_LocalLargestStripCount) planes_LocalLargestStripCount = stripLength;
    }

    /* Fill Strips */
    origin.x[0] = thePlanes[ iPlane ].origincoords.x[0];
    origin.x[1] = thePlanes[ iPlane ].origincoords.x[1];
    origin.x[2] = thePlanes[ iPlane ].origincoords.x[2];

    int elemnum;

    for (stripnum = 0; stripnum < thePlanes[ iPlane ].numberofstripsthisplane; stripnum++){
      for (elemnum = 0; elemnum <(thePlanes[iPlane].stripend[stripnum]-thePlanes[iPlane].stripstart[stripnum]+1); elemnum++){
	iStrike  = (elemnum+thePlanes[iPlane].stripstart[stripnum]) / thePlanes[iPlane].numberofstepsdowndip;
	iDownDip = (elemnum+thePlanes[iPlane].stripstart[stripnum]) % thePlanes[iPlane].numberofstepsdowndip;

	if(thePlanes[iPlane].typeplaneinput==0){
	  xLocal = iStrike*thePlanes[iPlane].stepalongstrike;
	  yLocal = iDownDip*thePlanes[ iPlane ].stepdowndip;
	
	  pointLocal.x[0] =xLocal; pointLocal.x[1] =yLocal; pointLocal.x[2] =0;
	  pointGlobal.x[0]=xLocal; pointGlobal.x[1]=yLocal; pointGlobal.x[2]=0;
	  
	  pointGlobal=compute_global_coords (origin, pointLocal,thePlanes[ iPlane ].dip, 0,
					     thePlanes[ iPlane ].strike );
	  
	  compute_lonlat_from_domain_coords_linearinterp( yLocal+origin.x[1], xLocal+origin.x[0], &lon, &lat,
							  theSurfaceCornersLong, theSurfaceCornersLat,
							  theDomainX, theDomainY);	
	  Query_Topo_Bedrock_DepthToBedrock (lat,lon, &topo , &database,0);
	  pointGlobal.x[2]=topo;	  

	}else{
	  iCount=iDownDip+thePlanes[iPlane].numberofstepsdowndip*(iStrike);  // review
	  
	  lon=thePlanes[iPlane].lonIn[iCount];
	  lat=thePlanes[iPlane].latIn[iCount];
	  depth=thePlanes[iPlane].depthIn[iCount];
	  
	  pointGlobal= compute_domain_coords_linearinterp(lon,lat,theSurfaceCornersLong,theSurfaceCornersLat,
							  theDomainY,theDomainX);
	  Query_Topo_Bedrock_DepthToBedrock (lat,lon, &topo , &database,0);
	  pointGlobal.x[2]=topo+depth;
	}
	
	if( search_point( pointGlobal, &octant) == 1 ){
	 thePlanes[ iPlane ].strip[stripnum][elemnum].h= compute_csi_eta_dzeta( octant, pointGlobal,
				 &(thePlanes[ iPlane ].strip[stripnum][elemnum].localcoords ),
				 thePlanes[ iPlane ].strip[stripnum][elemnum].nodestointerpolate);
	}
      }
    }


  iCount=0;
    for ( iStrike = 0; iStrike < thePlanes[iPlane].numberofstepsalongstrike; iStrike++ ){
      xLocal = iStrike*thePlanes[iPlane].stepalongstrike;
      for ( iDownDip = 0; iDownDip < thePlanes[iPlane].numberofstepsdowndip; iDownDip++ ) {
	if(thePlanes[iPlane].typeplaneinput==0){
	  yLocal = iDownDip*thePlanes[ iPlane ].stepdowndip;
	  pointLocal.x[0] =xLocal;  pointLocal.x[1] =yLocal;  pointLocal.x[2] =0;	  
	  pointGlobal.x[0]=xLocal;  pointGlobal.x[1]=yLocal;  pointGlobal.x[2]=0;	  	  
	  pointGlobal=compute_global_coords(origin,pointLocal,thePlanes[ iPlane ].dip,0,thePlanes[ iPlane ].strike );
	  compute_lonlat_from_domain_coords_linearinterp( yLocal+origin.x[1], xLocal+origin.x[0], 
	  						  &lon, &lat,theSurfaceCornersLong,theSurfaceCornersLat, theDomainX, theDomainY);
	  Query_Topo_Bedrock_DepthToBedrock (lat,lon, &topo , &database,0);
	  pointGlobal.x[2]=topo;
	  
	}else{
	  iCount=iDownDip+thePlanes[iPlane].numberofstepsdowndip*(iStrike);  // review
	  lon=thePlanes[iPlane].lonIn[iCount];lat=thePlanes[iPlane].latIn[iCount];
	  depth=thePlanes[iPlane].depthIn[iCount];	  
	  pointGlobal= compute_domain_coords_linearinterp(lon,lat,theSurfaceCornersLong,
                                  theSurfaceCornersLat,theDomainY,theDomainX);
	  Query_Topo_Bedrock_DepthToBedrock (lat,lon, &topo , &database,0);
	  pointGlobal.x[2]=topo+depth;	 
	}
   
	MPI_Barrier(MPI_COMM_WORLD);
	MPI_Reduce(&thePlanes[iPlane ].vp[iCount],&theVpPlane, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
	MPI_Reduce(&thePlanes[iPlane ].vs[iCount],&theVsPlane, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
	MPI_Reduce(&thePlanes[iPlane ].rho[iCount],&theRhoPlane, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
	MPI_Reduce(&thePlanes[iPlane ].elementtype[iCount],&theTypeOfElement, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

	thePlanes[iPlane ].vp[iCount]=theVpPlane;
	thePlanes[iPlane ].vs[iCount]=theVsPlane;
	thePlanes[iPlane ].rho[iCount]=theRhoPlane;
	thePlanes[iPlane].elementtype[iCount]=theTypeOfElement;

        /* Here I write the properties where displacements are calculated */

	if(myID==0){
	  fprintf (thePlanes[iPlane].fpplanecoordsfile, "\n %d %e %e %e %e %e %e %e %e %e",iCount,lon,lat,pointGlobal.x[0], pointGlobal.x[1], pointGlobal.x[2],thePlanes[iPlane].vp[iCount], thePlanes[iPlane].vs[iCount],thePlanes[iPlane].rho[iCount],thePlanes[iPlane].elementtype[iCount]);  	 
		}
	//MPI_Barrier(MPI_COMM_WORLD);

      }
    }

    if(myID ==0){
      fclose(thePlanes[iPlane].fpplanecoordsfile);
    }

  }/*end of plane loop*/



  MPI_Reduce (&planes_LocalLargestStripCount,&planes_GlobalLargestStripCount,
	      1, MPI_INT, MPI_MAX, 0, MPI_COMM_WORLD);

  /* Allocate MPI recv buffers for IO PE */
  if (myID == 0) {
    planes_stripMPIRecvBuffer = (double *) malloc(  sizeof(double) * (  (3*planes_GlobalLargestStripCount) +1 )  );
    /* This is large enough for 3 double componants of biggest strip plus location number*/
    if(planes_stripMPIRecvBuffer == NULL){
      fprintf(stderr, "Error creating MPI master buffer strip for plane output\n");
      exit(1);
    }
  }
    
  /* Allocate MPI send buffers for all PEs */
  planes_stripMPISendBuffer = (double *) malloc(   sizeof(double) * (  (3*planes_LocalLargestStripCount) +1 )    );
  /* This is large enough for 3 double componants of biggest strip plus location number*/
  if(planes_stripMPISendBuffer == NULL){
    fprintf(stderr, "Error creating MPI send buffer strip for plane output\n");
    exit(1);
  }
}


/* Close Planes Files */
void output_planes_finalize()
{
  int iPlane;
  if (myID==0){
    for (iPlane = 0; iPlane < theNumberOfPlanes ;iPlane++) {
      fclose (thePlanes[ iPlane ].fpoutputfile);
    }
  }
}



/**
 *   Read stations info: this is called by rank 0
 *
 */
void read_stations_info (const char *numericalin){

  int    iStation, iCorner,stationsInputType;
  double lon, lat, depth, *auxiliar;
  FILE *fp, *fpcoords;
  char  input_stations_directory[256], stationscoordsin[256];
  vector3D_t coords;
    
  static const char* fname = "output_stations_init()";
    
  /* Obtain the stations specifications */
    
  if ( (fp = fopen ( numericalin, "r")) == NULL )
    solver_abort (fname, numericalin, "Error opening numerical.in configuration file");
  
  auxiliar = (double *)malloc(sizeof(double)*8);
	
  if ( parsedarray( fp, "domain_surface_corners", 8 ,auxiliar) !=0 ) {
    solver_abort (fname, NULL, "Error parsing domain_surface_corners field from %s\n",
		  numericalin);
  }

  for ( iCorner = 0; iCorner < 4; iCorner++){
    theSurfaceCornersLong[ iCorner ] = auxiliar [ iCorner * 2 ];
    theSurfaceCornersLat [ iCorner ] = auxiliar [ iCorner * 2 +1 ];
  }
  free(auxiliar);

  
  if ( parsetext(fp, "stations_inputtype", 'i',&stationsInputType) != 0  ){
    solver_abort (fname, NULL,"Error parsing stations_inputtype %s\n",
		  numericalin);}
  if(stationsInputType==1){    
    if ( parsetext( fp, "input_stations_directory", 's', &input_stations_directory ) ){
      fprintf(stderr, "Err parsing input_stations_directory from %s",numericalin);
      fflush(stdout);
      return -1;
    }

    sprintf(stationscoordsin, "%s/stationscoords.in" ,input_stations_directory); /*Create the string for the file */
  
    if ( ( fpcoords = fopen(stationscoordsin , "r" ) ) == NULL ) {    
	fprintf(stderr, "Error opening %s\n", stationscoordsin);    
	return -1;    
    }
  }else
    fpcoords=fp;
    


  if ( parsetext(fp, "output_stations_print_rate", 'i',&theStationsPrintRate) != 0  )
    solver_abort (fname, NULL,"Error parsing output_stations_print_rate %s\n",
		  numericalin);
    
  auxiliar = (double *)malloc(sizeof(double)*theNumberOfStations*3);


  theStationX = (double *)malloc(sizeof(double)*theNumberOfStations);
  theStationY = (double *)malloc(sizeof(double)*theNumberOfStations);
  theStationZ = (double *)malloc(sizeof(double)*theNumberOfStations);

  if( theStationX == NULL || theStationY == NULL || theStationY == NULL ){
    fprintf(stdout,"Err alloc theStations arrays in output_stations_init");
    fflush(stdout);
    MPI_Abort(MPI_COMM_WORLD, ERROR);
    exit(1);
  }
    
  if ( parsedarray( fpcoords, "output_stations",theNumberOfStations*3,auxiliar) !=0 )
    solver_abort (fname, NULL, "Err parsing output_stations = from %s\n",numericalin);
    
  for ( iStation = 0; iStation < theNumberOfStations; iStation++){
	
    lat   = auxiliar [ iStation * 3 ];
    lon   = auxiliar [ iStation * 3 +1 ];
    depth = auxiliar [ iStation * 3 +2 ];
    coords = compute_domain_coords_linearinterp(lon,lat,
						theSurfaceCornersLong,
						theSurfaceCornersLat,
						theDomainY,theDomainX);
    theStationX [ iStation ] = coords.x[0];
    theStationY [ iStation ] = coords.x[1];

    theStationZ [ iStation ] = depth;    

  }
    
  free(auxiliar);
  if ( parsetext(fp, "output_stations_directory",'s',theStationsDirOut)!= 0)
    solver_abort (fname, NULL, "Error parsing fields from %s\n",
		  numericalin);
    

  return;
}


/**
 * Broadcast info about the stations.
 */
void broadcast_stations_info()
{
  /*initialize the local structures */
  if ( myID != 0 ) {
    theStationX = (double*)malloc( sizeof(double) * theNumberOfStations);
    theStationY = (double*)malloc( sizeof(double) * theNumberOfStations);
    theStationZ = (double*)malloc( sizeof(double) * theNumberOfStations);
    
    if (theStationX == NULL ||  theStationY == NULL || theStationZ==NULL) {
      solver_abort( "broadcast_stations_info", NULL,
		    "Error: Unable to create stations arrays" );
    }
  }

  MPI_Barrier( MPI_COMM_WORLD );

  MPI_Bcast( theStationX, theNumberOfStations, MPI_DOUBLE, 0, MPI_COMM_WORLD);
  MPI_Bcast( theStationY, theNumberOfStations, MPI_DOUBLE, 0, MPI_COMM_WORLD);
  MPI_Bcast( theStationZ, theNumberOfStations, MPI_DOUBLE, 0, MPI_COMM_WORLD);

  broadcast_char_array ( theStationsDirOut, sizeof(theStationsDirOut), 0,
			 MPI_COMM_WORLD );

  return;
}



/**
 *
 * Prepare all the info every station needs once it is located in a processor
 *
 */
void setup_stations_data(){

  
  int32_t iStation, iLCStation=0; /* LC local count */
    
  vector3D_t stationCoords;
    
  octant_t *octant;
  
  static char stationFile[256];
    
  /* look for the stations in the domain each processor has */
    
  for ( iStation = 0; iStation < theNumberOfStations; iStation++){

    stationCoords.x[0]=theStationX[iStation];
    stationCoords.x[1]=theStationY[iStation]; 
    stationCoords.x[2]=theStationZ[iStation];
	
    if ( search_point( stationCoords, &octant ) == 1 )
      myNumberOfStations+=1;
	
  }
    
  /*    fprintf(stdout,"\n myNumberOfStations %d theStations = %d ", myNumberOfStations, theNumberOfStations);
	fflush(stdout);*/

  /**
   * This barrier is commented in LEO's 'successful' code
   *
   * MPI_Barrier(MPI_COMM_WORLD);
   *
   */
    
  /* Allocate memory if necessary and generate the list of stations per
     processor */
  if( myNumberOfStations != 0 ){
    myStations = (station_t *)malloc(sizeof(station_t)*myNumberOfStations);
    if(myStations ==0){
      fprintf(stderr, "Err alloc myStations:setup_stations_data");
      exit(1);
    }
	
  }
    
  for ( iStation = 0; iStation < theNumberOfStations; iStation++){
	
    stationCoords.x[0]=theStationX[iStation];
    stationCoords.x[1]=theStationY[iStation];
    stationCoords.x[2]=theStationZ[iStation];
      
    if ( search_point( stationCoords, &octant ) == 1 ){
      myStations[iLCStation].id = iStation;
      myStations[iLCStation].coords=stationCoords;
      sprintf(stationFile, "%s/station.%d",theStationsDirOut,iStation);
      myStations[iLCStation].fpoutputfile = hu_fopen( stationFile,"w" );
      myStations[iLCStation].h=compute_csi_eta_dzeta( octant,
			     myStations[iLCStation].coords,
			     &(myStations[iLCStation].localcoords),
			     myStations[iLCStation].nodestointerpolate);
      fputc ('\n', myStations[iLCStation].fpoutputfile );

      iLCStation += 1;
    }

  }
  
   
  free(theStationX);
  free(theStationY);
  free(theStationZ);
}


/**
 *  interpolate_station_displacements ():
 *
 */
static int
interpolate_station_displacements( int32_t step )
{
  int iPhi;

  /* Auxiliar array to handle shapefunctions in a loop */
  double  xi[3][8]={ {-1,  1, -1,  1, -1,  1, -1, 1} ,
		     {-1, -1,  1,  1, -1, -1,  1, 1} ,
		     {-1, -1, -1, -1,  1,  1,  1, 1} };

  double     phi[8],displacementsX,displacementsY,displacementsZ,dphidx,dphidy,dphidz;
  int32_t    iStation,nodesToInterpolate[8];;
  vector3D_t localCoords; /* convinient renaming */
 double x,y,z,h,hcube;
  tick_t edgeticks;


 double dudx,dudy,dudz,dvdx,dvdy,dvdz,dwdx,dwdy,dwdz;

    
  for (iStation=0;iStation<myNumberOfStations; iStation++) {
    localCoords = myStations[iStation].localcoords;

    h=myStations[iStation].h;
    hcube=h*h*h;
    x=localCoords.x[0];
    y=localCoords.x[1];
    z=localCoords.x[2];



    for (iPhi=0; iPhi<8; iPhi++) {
      nodesToInterpolate[iPhi]
	= myStations[iStation].nodestointerpolate[iPhi];
    }

    /* Compute interpolation function (phi) for each node and
     * load the displacements
     */
    displacementsX = 0;
    displacementsY = 0;
    displacementsZ = 0;
    dudx=0;
    dudy=0;
    dudz=0;
    dvdx=0;
    dvdy=0;
    dvdz=0;
    dwdx=0;
    dwdy=0;
    dwdz=0;     


    if(myStations[iStation].coords.x[0] > 0 && myStations[iStation].coords.x[1] > 0 &&
       myStations[iStation].coords.x[0] < theDomainX && 
       myStations[iStation].coords.x[1] < theDomainY ){

      for (iPhi = 0; iPhi < 8; iPhi++){
		   
	phi[ iPhi ] =  (1+xi[0][iPhi]*localCoords.x[0])*
	  (1+xi[1][iPhi]*localCoords.x[1])*
	  (1+xi[2][iPhi]*localCoords.x[2])/8;
		   
	displacementsX
	  += phi[iPhi] * mySolver->tm1[ nodesToInterpolate[iPhi] ].f[0];
		   
	displacementsY
	  += phi[iPhi] * mySolver->tm1[ nodesToInterpolate[iPhi] ].f[1];
		   
	displacementsZ
	  += phi[iPhi] * mySolver->tm1[ nodesToInterpolate[iPhi] ].f[2];

	dphidx= (2 * xi[0][iPhi]) * (h + 2 * xi[1][iPhi] * y) * (h + 2 * xi[2][iPhi] * z) 
	    / (8 * hcube) ;
	dphidy= (2 * xi[1][iPhi]) * (h + 2 * xi[2][iPhi] * z) * (h + 2 * xi[0][iPhi] * x)
	    / (8 * hcube);     
	dphidz= (2 * xi[2][iPhi]) * (h + 2 * xi[0][iPhi] * x) * (h + 2 * xi[1][iPhi] * y)
	    / (8 * hcube);

	dudx+=dphidx * mySolver->tm1[ nodesToInterpolate[iPhi] ].f[0];
	dudy+=dphidy * mySolver->tm1[ nodesToInterpolate[iPhi] ].f[0];
	dudz+=dphidz * mySolver->tm1[ nodesToInterpolate[iPhi] ].f[0];
	dvdx+=dphidx * mySolver->tm1[ nodesToInterpolate[iPhi] ].f[1];
	dvdy+=dphidy * mySolver->tm1[ nodesToInterpolate[iPhi] ].f[1];
	dvdz+=dphidz * mySolver->tm1[ nodesToInterpolate[iPhi] ].f[1];
	dwdx+=dphidx * mySolver->tm1[ nodesToInterpolate[iPhi] ].f[2];
	dwdy+=dphidy * mySolver->tm1[ nodesToInterpolate[iPhi] ].f[2];
	dwdz+=dphidz * mySolver->tm1[ nodesToInterpolate[iPhi] ].f[2];    

      }
    }



    fprintf( myStations[iStation].fpoutputfile, "\n%e %e %e %e %e %e %e %e %e %e %e %e %e %e",
	     theDeltaT*step, displacementsX, displacementsY, displacementsZ, dudx,dudy,dudz,
	     dvdx,dvdy,dvdz,dwdx,dwdy,dwdz,h);
  }

  return 1;
}



/**
 *
 * Init stations info and data structures
 *
 */
void correct_stations_depth (){

  double north_m, east_m, depth_m,lon,lat;
  int iStation;

  for ( iStation = 0; iStation < theNumberOfStations; iStation++){

    north_m=theStationX [ iStation ];
    east_m =theStationY [ iStation ];
    


    compute_lonlat_from_domain_coords_linearinterp( east_m , north_m , &lon, &lat,
							theSurfaceCornersLong,
							theSurfaceCornersLat,
							theDomainX, theDomainY);


    Query_Topo_Bedrock_DepthToBedrock( lat,lon, &depth_m ,&database,0); 

    theStationZ [ iStation ] =  theStationZ [ iStation ] + depth_m;

  }
  
}


/**
 *
 * Init stations info and data structures
 *
 */
void output_stations_init (const char *numericalin){

  double output_stations_init_time = 0;
  
  if (myID == 0) {
    output_stations_init_time -= MPI_Wtime();
    read_stations_info(numericalin);
    /* Obtain the topography to modify the z coord, z means depth in the topography case*/
    correct_stations_depth();

  }
    
  broadcast_stations_info();
   
  setup_stations_data();
    
  if( myID == 0){
    output_stations_init_time += MPI_Wtime();
    fprintf(stdout,"\n output_stations_init_time= %lf",  output_stations_init_time);
    fflush(stdout);
  }
    
  MPI_Barrier(MPI_COMM_WORLD);
    
  return;
}



/**
 * \note This function should only be called by PE with rank 0.
 */
static int
load_output_parameters (const char* numericalin, output_parameters_t* params)
{
  FILE* fp;
  int   ret, value;
  char  filename[LINESIZE];

  assert (NULL != numericalin);
  assert (NULL != params);
  assert (myID == 0);

  /* Read output parameters from numerical.in */
  fp = fopen (numericalin, "r");

  if (NULL == fp) {
    solver_abort ("load_output_parameters", "fopen", "numerical.in=\"%s\"",
		  numericalin);
  }

  params->do_output		= 0;
  params->parallel_output     = 0;
  params->output_displacement = 0;
  params->output_velocity     = 0;
  params->output_debug        = 0;

  params->displacement_filename = NULL;
  params->velocity_filename     = NULL;


  /* read parameters from configuration file */

  value = 0;
  ret = parsetext (fp, "output_parallel", 'i', &value);

  if (0 == ret && 0 != value) {
    value = 0;
    ret = parsetext (fp, "output_displacement", 'i', &value);

    if (0 == ret && 0 != value) { /* output_displacement = 1 in config */
      ret = read_config_string (fp, "output_displacement_file",
				filename, LINESIZE);

      if (1 == ret && filename[0] != '\0') {
	params->displacement_filename = strdup (filename);
	params->output_displacement = 1;
      } else {
	solver_abort ("load_output_parameters", NULL,
		      "Output displacement file name not specified in "
		      "numerical.in=\"%s\"",
		      numericalin);
      }
    }

    value = 0;
    ret   = parsetext (fp, "output_velocity", 'i', &value);

    if (0 == ret && 0 != value) { /* output_displacement = 1 in config */
      ret = read_config_string (fp, "output_velocity_file",
				filename, LINESIZE);

      if (1 == ret && filename[0] != '\0') {
	params->velocity_filename = strdup (filename);
	params->output_velocity = 1;
      } else {
	solver_abort ("load_output_parameters", NULL,
		      "Output velocity file name not specified in "
		      "numerical.in=\"%s\"",
		      numericalin);
      }
    }

    params->stats_filename = "output-stats.txt"; /* default value */

    ret = read_config_string (fp, "output_stats_file", filename, LINESIZE);

    if (1 == ret && filename[0] != '\0') {
      params->stats_filename = strdup (filename);
    }

    params->debug_filename = "output-debug.txt"; /* default value */
    ret = read_config_string (fp, "output_debug_file", filename, LINESIZE);

    if (1 == ret && filename[0] != '\0') {
      params->debug_filename = strdup (filename);
    }


    if (params->output_velocity || params->output_displacement) {
      params->do_output = 1;
    }

    params->parallel_output = 1;

    ret = parsetext (fp, "output_debug", 'i', &value);
    if (0 == ret && 0 != value) { /* output_debug = 1 in config */
      params->output_debug = 1;
    }
  }

  ret = 0;

  fclose (fp);

  return ret;
}



/**
 * Initialize output structures, including the opening of 4D output files.
 *
 * \param numericsin Name of the file with the solver and output parameters
 *	i.e., "numerical.in".
 * \param[out] params output parameters, including filenames.
 *
 * \pre The following global variables should be initialized:
 *	- myID.
 *	- theGroupSize.
 *
 * \post On a successful return, the output argument \c params will be
 *	properly initialized.  If the routine fails, the state of the
 *	\c param struct is undefined.
 *
 * \return 0 on success, -1 on error.
 */
static int
output_init_parameters (const char* numericalin, output_parameters_t* params)
{
  /* jc: this ugly #define here is because the catamount compiler does not
   * like static const  */
#define VALUES_COUNT    4
  int ret;
  int32_t values[VALUES_COUNT];
  off_t output_steps;

  /* sanity cleanup */
  memset (params, 0, sizeof (output_parameters_t));

  params->do_output		  = 0;
  params->displacement_filename = NULL;
  params->velocity_filename     = NULL;
  params->stats_filename	  = NULL;

  if (myID == 0) {
    ret = load_output_parameters (numericalin, params);

    if (0 != ret) {
      solver_abort ("output_init_parameters", NULL, NULL);
      return -1;
    }
  }

  /* parameters that can be initialized from global variables */
  params->pe_id          = myID;
  params->pe_count       = theGroupSize;
  params->total_nodes    = theNTotal;
  params->total_elements = theETotal;
  params->output_rate	   = theRate;
  params->domain_x	   = theDomainX;
  params->domain_y	   = theDomainY;
  params->domain_z	   = theDomainZ;
  params->mesh	   = myMesh;
  params->solver	   = (solver_t*)mySolver;
  params->delta_t	   = theDeltaT;
  params->total_time_steps = theTotalSteps;

  output_steps             = (theTotalSteps - 1) / theRate + 1;


  values[0] = params->parallel_output;
  values[1] = params->output_displacement;
  values[2] = params->output_velocity;
  values[3] = params->output_debug;


  MPI_Bcast (values, VALUES_COUNT, MPI_INT, 0, MPI_COMM_WORLD);
  MPI_Bcast (&params->total_nodes, 1, MPI_INT64, 0, MPI_COMM_WORLD);

  params->parallel_output     = values[0];
  params->output_displacement = values[1];
  params->output_velocity	= values[2];
  params->output_debug        = values[3];
  params->output_size = output_steps * params->total_nodes * sizeof (fvector_t)
    + sizeof (out_hdr_t);
  theNTotal = params->total_nodes;


  if (params->parallel_output) {
    if (params->output_displacement) {
      broadcast_string (&params->displacement_filename, 0,MPI_COMM_WORLD);
    }

    if (params->output_velocity) {
      broadcast_string (&params->velocity_filename, 0, MPI_COMM_WORLD);
    }

    if (params->output_debug) {
      broadcast_string (&params->debug_filename, 0, MPI_COMM_WORLD);
    }

    /* set the expected file size */
    the4DOutSize = params->output_size;
  }

  return 0;
#undef VALUES_COUNT
}


/**
 * Intialize parallel output structures according to config file
 * parameters.
 *
 * \note params parameter could be a local var instead, it doesn't need
 * to be global, since this is not really used after the initialization
 * except for the stats file name.
 *
 * \return 0 on success, -1 on error (probably aborts instead).
 */
static int
output_init (const char* numericalin, output_parameters_t* params)
{
  int ret = -1;

  assert (NULL != numericalin);
  assert (NULL != params);

  ret = output_init_parameters (numericalin, params);

  if (ret != 0) {
    return -1;
  }

  /* initialize structures, open files, etc */
  ret = output_init_state (params);

  /* these aren't needed anymore after this point */
  xfree ((void**) &params->displacement_filename);
  xfree ((void**) &params->velocity_filename);

  return ret;
}


static int
output_get_stats (void)
{
  output_stats_t disp_stats, vel_stats;
  int    ret      = 0;
  double avg_tput = 0;


  if (theOutputParameters.parallel_output) {
    ret = output_collect_io_stats (theOutputParameters.stats_filename,
				   &disp_stats, &vel_stats, theE2ETime);

    /* magic trick so print_timing_stat() prints something sensible
     * for the 4D output time in the parallel case.
     *
     * if both displacement and velocity where written out, prefer
     * the displacement stat.
     */
    if (theOutputParameters.output_velocity) {
      avg_tput = vel_stats.tput_avg;
    }

    if (theOutputParameters.output_displacement) {
      avg_tput = disp_stats.tput_avg;
    }

    /* aggregate thruput */
    avg_tput *= theOutputParameters.pe_count;
    the4DOutTime = the4DOutSize / avg_tput;
  }

  return ret;
}


double thehvp;
double theppwlMIN, theppwlMAX;


/*
 *
 * etree_correct_properties_topo
 *
 */
static void  etree_correct_properties_topo(){
    
  elem_t  *elemp;
  edata_t *edata;
  int32_t eindex;
    
  double east_m, north_m, depth_m, vp,vs,rho,qp, qs,depth_mr,vpvsratio;
  int layertype;
  double lon, lat;
  double points[11];
  double topo;
  double h,hvp=1e10,ppwlMIN=1e10,ppwlMAX=-1e10,vpovervs;
  int containingunit; 
  int  numPointsBelowSurface=0,res=0;
  int32_t lnid0, lnid7,iNorth,iEast,iDepth,numPoints=3;
  int typeOfElementFinal=0;
  
  points[0] = .01; points[1] = 0.5; points[2] =0.99;

  if(myID==0) fprintf(stdout,"etree_correct_properties:..");

  MPI_Barrier(MPI_COMM_WORLD);
  /* Go elements by element */
  for ( eindex = 0; eindex < myMesh->lenum; eindex++ ) {
    //fprintf(stdout,"\n%d %d ", eindex, myMesh->lenum );
    //fflush(stdout);

    elemp = &myMesh->elemTable[eindex];
    edata = (edata_t *)elemp->data;
    lnid0 = myMesh->elemTable[eindex].lnid[0];
    lnid7 = myMesh->elemTable[eindex].lnid[7];
	
    h=fabs((myMesh->ticksize) * (double)( myMesh->nodeTable[lnid0].x -  myMesh->nodeTable[lnid7].x));    
	
    edata->Vp = 0;    edata->Vs = 0;    edata->rho = 0;    edata->typeofelement = 0;

    typeOfElementFinal=0;
    numPointsBelowSurface=0;

    	
    for ( iNorth = 0; iNorth < numPoints; iNorth++ ) {	    
      north_m = (myMesh->ticksize) * (double)myMesh->nodeTable[lnid0].x 
	+ edata->edgesize * points[iNorth] ;
	    
      for ( iEast = 0; iEast < numPoints; iEast++ ) {		
	east_m  = (myMesh->ticksize) * (double)myMesh->nodeTable[lnid0].y
	  + edata->edgesize * points[iEast];		
	compute_lonlat_from_domain_coords_linearinterp( east_m , north_m , &lon, &lat,
							theSurfaceCornersLong,
							theSurfaceCornersLat,
							theDomainX, theDomainY);
		
	for ( iDepth = 0; iDepth < numPoints; iDepth++) {		    
	  depth_m = (myMesh->ticksize) * (double)myMesh->nodeTable[lnid0].z 
	    + edata->edgesize * points[iDepth];
	
	  
	  if(doIIncludeTopo==1){
	    depth_m=database.datum-depth_m;
	    res=Single_Search(lat, depth_m, lon,&vp, &vs, &rho,&containingunit,&qp,&qs,&database,0,1.*doIIncludeBuildings);

	  }else
	    res=Single_Search(lat, depth_m, lon,&vp, &vs, &rho,&containingunit,&qp,&qs,&database,0,1.*doIIncludeBuildings);
	  
	  vpvsratio=vp/vs;

	  /* layer_type =  -1-air, 1-soil 2-water 10-11 attached to topo in depth*/
	  layertype=(database.geologicunits[containingunit]).layertype;
 	
	  if(layertype ==-1){
	    edata->Vp =edata->Vp+0;
	    edata->Vs =edata->Vs+0;
	    edata->rho=edata->rho+0;
	    numPointsBelowSurface= numPointsBelowSurface;
	  }

	  if((layertype ==1) || (layertype >9)){
	    if( vs <theVsCut){
	      vs=theVsCut;
	       vp=theVsCut*vpvsratio;
	    }
	    //if(vs<100)
	    //  exit(1);
	    edata->Vp =edata->Vp+vp;
	    edata->Vs =edata->Vs+vs;
	    edata->rho=edata->rho+rho;
	    numPointsBelowSurface= numPointsBelowSurface+1;
	  }


	  if(layertype ==2 ){
	    if( vs <theVsCut){
	      vs=theVsCut;
	      vp=theVsCut*vpvsratio;
	    }
	    edata->Vp =edata->Vp+vp;
	    edata->Vs =edata->Vs+vs;
	    edata->rho=edata->rho+rho;
	    numPointsBelowSurface= numPointsBelowSurface+1;
	    typeOfElementFinal=2;
	  }


	  if(layertype ==4 ){
	    if( vs <theVsCut){
	      vs=theVsCut;
	      vp=theVsCut*vpvsratio;
	    }
	    edata->Vp =edata->Vp+vp;
	    edata->Vs =edata->Vs+vs;
	    edata->rho=edata->rho+rho;
	    numPointsBelowSurface= numPointsBelowSurface+1;
	  }

	}
      }	    
    }

	
    if(numPointsBelowSurface >0){      
      edata->Vp= edata->Vp/(numPointsBelowSurface);
      edata->Vs= edata->Vs/(numPointsBelowSurface);
      edata->rho= edata->rho/(numPointsBelowSurface);
      edata->typeofelement = 1;
     
      if(numPointsBelowSurface <((numPoints*numPoints*numPoints)-1))
	edata->typeofelement = 0;
      
      if(typeOfElementFinal==2){
	edata->Vp= 1531;
	edata->Vs= 5;
	edata->rho=1000 ;
	edata->typeofelement = 2;
      }


    }else{
      edata->Vp= 0.001;
      edata->Vs= 0.001;
      edata->rho= 0.0001;
      edata->typeofelement = -1;
    }

    //depth_m = (myMesh->ticksize) * (double)myMesh->nodeTable[lnid0].z ;
    //Query_Topo_Bedrock_DepthToBedrock( lat,lon,&topo,&database,0); 
   
    //if(depth_m>topo){
    //  edata->Vp= 0.001;
    //  edata->Vs= 0.001;
    //  edata->rho= 0.0001;
    //  edata->typeofelement = -1;

    // }

    if( edata->typeofelement == -1){
      edata->Vp= 0.001;
      edata->Vs= 0.001;
      edata->rho= 0.001;
    }



    if( (edata->typeofelement >= 0) && (edata->typeofelement |=2)){
      if(h/edata->Vp < 0.01){
	vpovervs=edata->Vp =edata->Vs;
	//if(vpovervs>1.5)
	  edata->Vp =edata->Vs;
	
      }   
      if(hvp > h/edata->Vp)hvp= h/edata->Vp;
      if((edata->Vs/theFreq)/h < ppwlMIN) ppwlMIN=(edata->Vs/theFreq)/h;
      if((edata->Vs/theFreq)/h > ppwlMAX) ppwlMAX=(edata->Vs/theFreq)/h;
      if(hvp > h/edata->Vp)hvp= h/edata->Vp;
    }

    // fprintf(stdout,"\n %lf %lf %lf %d",edata->Vp,edata->Vs,edata->rho,myID);

  }

 MPI_Barrier( MPI_COMM_WORLD );

      
  MPI_Reduce(&hvp, &thehvp, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
  MPI_Reduce(&ppwlMIN, &theppwlMIN, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
  MPI_Reduce(&ppwlMAX, &theppwlMAX, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
  if(myID == 0){
    fprintf(stdout,"\n id = %d h/vp=%f", myID, thehvp);
    fprintf(stdout,"\n id = %d minppwlMIN=%f", myID, theppwlMIN);
    fprintf(stdout,"\n id = %d minppwlMAX=%f", myID, theppwlMAX);
  }

  return;
    
}


/* mesh_output_matlab_format  */
static void mesh_output_matlab_format(){
  
  FILE *filetodebugmesh;
  
  int32_t eindex, iNode, lnid, count=0,i;
  
  double x[8],y[8],z[8],vs,vp,rho,tElement, xTest,yTest,zTest,typeofelement;
  
  char fileMeshName[1024];
  
  edata_t *edata;
  elem_t  *elemp;

  int ascii = 0;
 
  for ( eindex = 0; eindex < myMesh->lenum; eindex++ ) {
    elemp = &myMesh->elemTable[eindex];
    edata = (edata_t *)elemp->data;
     vs = ( edata->Vs);
    lnid = myMesh->elemTable[eindex].lnid[0];
    xTest = myMesh ->ticksize * myMesh -> nodeTable[lnid].x;
    yTest = myMesh ->ticksize * myMesh -> nodeTable[lnid].y;
    zTest = myMesh ->ticksize * myMesh -> nodeTable[lnid].z;
    
    if( zTest < 4e3 )
        count +=1;

  }
  
  sprintf (fileMeshName, "%s/mesh_%d.mat",theDatabasePath,myID);

  filetodebugmesh= fopen(fileMeshName,"w");

  if(ascii == 1)
    fprintf(filetodebugmesh,"\n %d\n", count);
  else
    fwrite(&count, sizeof(int32_t),1,filetodebugmesh);

  /* Go elements by element to print coords*/
  for ( eindex = 0; eindex < myMesh->lenum; eindex++ ) {

    elemp = &myMesh->elemTable[eindex];
    edata = (edata_t *)elemp->data;

    lnid = myMesh->elemTable[eindex].lnid[0];
    xTest = myMesh ->ticksize * myMesh -> nodeTable[lnid].x;
    yTest = myMesh ->ticksize * myMesh -> nodeTable[lnid].y;
    zTest = myMesh ->ticksize * myMesh -> nodeTable[lnid].z;
    if( zTest < 4e3 ){

    vp = ( edata->Vp);
    vs = ( edata->Vs);
    rho = ( edata->rho);
    tElement = (double) ( edata->typeofelement);

    for ( iNode=0; iNode<8; iNode++ ){
	
	lnid = myMesh->elemTable[eindex].lnid[iNode];
	
	x[iNode]= myMesh ->ticksize * myMesh -> nodeTable[lnid].x;
	y[iNode]= myMesh ->ticksize * myMesh -> nodeTable[lnid].y;
	z[iNode]= myMesh ->ticksize * myMesh -> nodeTable[lnid].z;
	
    }
    if(ascii == 1){
      for ( iNode=0; iNode<8; iNode++ ) fprintf(filetodebugmesh," %lf ", x[iNode]);
      for ( iNode=0; iNode<8; iNode++ ) fprintf(filetodebugmesh," %lf ", y[iNode]);
      for ( iNode=0; iNode<8; iNode++ ) fprintf(filetodebugmesh," %lf ", z[iNode]);}
    else{
      for ( iNode=0; iNode<8; iNode++ ) fwrite(&(x[iNode]), sizeof(double),1,filetodebugmesh );
      for ( iNode=0; iNode<8; iNode++ ) fwrite(&(y[iNode]), sizeof(double),1,filetodebugmesh );
      for ( iNode=0; iNode<8; iNode++ ) fwrite(&(z[iNode]), sizeof(double),1,filetodebugmesh );
    }
    
    if(ascii == 1){
      fprintf(filetodebugmesh,"%lf ", edata->Vs);	  
      fprintf(filetodebugmesh,"\n");
    }else{
      fwrite(&vp, sizeof(double),1,filetodebugmesh);
      fwrite(&vs, sizeof(double),1,filetodebugmesh);
      fwrite(&rho, sizeof(double),1,filetodebugmesh);
      fwrite(&tElement, sizeof(double),1,filetodebugmesh);
    }
    }   
  }
  fclose(filetodebugmesh); 
}

/***************************************************************/

int  Broadcast_Surface(int iDb){

  int nx,ny,iX;


  MPI_Barrier( MPI_COMM_WORLD );
  
  MPI_Bcast(&((database.geologicunits[iDb]).reflectorsurfaces.nxgrid),1, MPI_INT, 0, MPI_COMM_WORLD);
  MPI_Bcast(&((database.geologicunits[iDb]).reflectorsurfaces.nygrid),1, MPI_INT, 0, MPI_COMM_WORLD);
  nx=(database.geologicunits[iDb]).reflectorsurfaces.nxgrid;
  ny=(database.geologicunits[iDb]).reflectorsurfaces.nygrid;
  
  MPI_Barrier( MPI_COMM_WORLD );

  if(myID!=0){
    (database.geologicunits[iDb]).reflectorsurfaces.xgrid = (double* ) malloc (sizeof(double)* nx); 
    (database.geologicunits[iDb]).reflectorsurfaces.ygrid = (double* ) malloc (sizeof(double)* ny);
      
      if( (database.geologicunits[iDb]).reflectorsurfaces.xgrid == NULL || 
	  (database.geologicunits[iDb]).reflectorsurfaces.ygrid == NULL) { 
	fprintf(stdout,"Error allocating memory for surfaces");
	fflush(stdout); 
	exit(1);
      }
  }
  
  MPI_Barrier( MPI_COMM_WORLD );

  MPI_Bcast((database.geologicunits[iDb]).reflectorsurfaces.xgrid,nx, MPI_DOUBLE, 0, MPI_COMM_WORLD);
  MPI_Bcast((database.geologicunits[iDb]).reflectorsurfaces.ygrid,ny, MPI_DOUBLE, 0, MPI_COMM_WORLD);
  
  if(myID!=0){   
    (database.geologicunits[iDb]).reflectorsurfaces.zgrid = (double ** ) malloc( sizeof(double *) * nx);  
    
    for ( iX = 0; iX < nx; iX++){
      (database.geologicunits[iDb]).reflectorsurfaces.zgrid[ iX ] =  (double *) malloc( sizeof(double) * ny);
      if((database.geologicunits[iDb]).reflectorsurfaces.zgrid[ iX ] == NULL ) {
	fprintf(stdout,"Error allocating memory for surfaces");
	fflush(stdout); 
	exit(1);	    
      }
    }    
  }

  MPI_Barrier( MPI_COMM_WORLD );
  for ( iX = 0; iX < nx; iX++)
    MPI_Bcast((database.geologicunits[iDb]).reflectorsurfaces.zgrid[iX],ny, MPI_DOUBLE, 0, MPI_COMM_WORLD);
   
}

/***************************************************************/

int  Broadcast_Surface_vs30(int iDb){

  int nx,ny,iX;
  
  MPI_Bcast(&((database.geologicunits[iDb]).vs30surface.nxgrid),1, MPI_INT, 0, MPI_COMM_WORLD);
  MPI_Bcast(&((database.geologicunits[iDb]).vs30surface.nygrid),1, MPI_INT, 0, MPI_COMM_WORLD);
  nx=(database.geologicunits[iDb]).vs30surface.nxgrid;
  ny=(database.geologicunits[iDb]).vs30surface.nygrid;

  if(myID!=0){
    (database.geologicunits[iDb]).vs30surface.xgrid = (double* ) malloc (sizeof(double)* nx); 
    (database.geologicunits[iDb]).vs30surface.ygrid = (double* ) malloc (sizeof(double)* ny);
      
      if( (database.geologicunits[iDb]).vs30surface.xgrid == NULL || 
	  (database.geologicunits[iDb]).vs30surface.ygrid == NULL) { 
	fprintf(stdout,"Error allocating memory for surfaces");
	fflush(stdout); 
	exit(1);
      }
      //      fprintf(stdout,"\n nx=%d ny=%d myID=%d",nx,ny,myID);

  }
  
  MPI_Barrier( MPI_COMM_WORLD );
  //exit(1);

  MPI_Bcast((database.geologicunits[iDb]).vs30surface.xgrid,nx, MPI_DOUBLE, 0, MPI_COMM_WORLD);
  MPI_Bcast((database.geologicunits[iDb]).vs30surface.ygrid,ny, MPI_DOUBLE, 0, MPI_COMM_WORLD);
  
  if(myID!=0){   
    (database.geologicunits[iDb]).vs30surface.zgrid = (double ** ) malloc( sizeof(double *) * nx);  
    
    for ( iX = 0; iX < nx; iX++){
      (database.geologicunits[iDb]).vs30surface.zgrid[ iX ] =  (double *) malloc( sizeof(double) * ny);
      if((database.geologicunits[iDb]).vs30surface.zgrid[ iX ] == NULL ) {
	fprintf(stdout,"Error allocating memory for surfaces");
	fflush(stdout); 
	exit(1);	    
      }
    }    
  }

  MPI_Barrier( MPI_COMM_WORLD );
  for ( iX = 0; iX < nx; iX++)
    MPI_Bcast((database.geologicunits[iDb]).vs30surface.zgrid[iX],ny, MPI_DOUBLE, 0, MPI_COMM_WORLD);
   

  // Now check that the factors were broadcasted
  //  fprintf(stdout,"\n factorvs30=%lf ",(database.geologicunits[iDb]).factorvs30);
  //  fprintf(stdout,"\n vs30_00=%lf %d ",(database.geologicunits[iDb]).vs30surface.zgrid[0][0],myID);

  MPI_Barrier( MPI_COMM_WORLD );

}



/***************************************************************/

int  Broadcast_Surface_veldistribsurface(int iDb){

 int nx,ny,iX;
  
  MPI_Bcast(&((database.geologicunits[iDb]).veldistribsurface.nxgrid),1, MPI_INT, 0, MPI_COMM_WORLD);
  MPI_Bcast(&((database.geologicunits[iDb]).veldistribsurface.nygrid),1, MPI_INT, 0, MPI_COMM_WORLD);
  nx=(database.geologicunits[iDb]).veldistribsurface.nxgrid;
  ny=(database.geologicunits[iDb]).veldistribsurface.nygrid;

  if(myID!=0){
    (database.geologicunits[iDb]).veldistribsurface.xgrid = (double* ) malloc (sizeof(double)* nx); 
    (database.geologicunits[iDb]).veldistribsurface.ygrid = (double* ) malloc (sizeof(double)* ny);
      
      if( (database.geologicunits[iDb]).veldistribsurface.xgrid == NULL || 
	  (database.geologicunits[iDb]).veldistribsurface.ygrid == NULL) { 
	fprintf(stdout,"Error allocating memory for surfaces");
	fflush(stdout); 
	exit(1);
      }
  }
  
  MPI_Barrier( MPI_COMM_WORLD );

  MPI_Bcast((database.geologicunits[iDb]).veldistribsurface.xgrid,nx, MPI_DOUBLE, 0, MPI_COMM_WORLD);
  MPI_Bcast((database.geologicunits[iDb]).veldistribsurface.ygrid,ny, MPI_DOUBLE, 0, MPI_COMM_WORLD);
  
  if(myID!=0){   
    (database.geologicunits[iDb]).veldistribsurface.zgrid = (double ** ) malloc( sizeof(double *) * nx);  
    
    for ( iX = 0; iX < nx; iX++){
      (database.geologicunits[iDb]).veldistribsurface.zgrid[ iX ] =  (double *) malloc( sizeof(double) * ny);
      if((database.geologicunits[iDb]).veldistribsurface.zgrid[ iX ] == NULL ) {
	fprintf(stdout,"Error allocating memory for surfaces");
	fflush(stdout); 
	exit(1);	    
      }
    }    
  }

  MPI_Barrier( MPI_COMM_WORLD );
  for ( iX = 0; iX < nx; iX++)
    MPI_Bcast((database.geologicunits[iDb]).veldistribsurface.zgrid[iX],ny, MPI_DOUBLE, 0, MPI_COMM_WORLD);
   
   
 }



/***************************************************************/
int   Broadcast_Velocity_Distribution(int iDb){

  char fileVelProfiles[256],fileVelSurf[256];
  char parseaux[256];
  FILE *fpCurrent;
  double maxValProfile;
  int numProfiles,numPoints,iProfile;
  double maxVal;
  int order;

  MPI_Barrier( MPI_COMM_WORLD ); 
  if((database.geologicunits[iDb]).profiletype==0){ /*VEL DISTIB.SURF AND PROFILES 0 PROFILE*/ 
    MPI_Bcast(&((database.geologicunits[iDb]).znormalizationtype),1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&((database.geologicunits[iDb]).numprofiles),1, MPI_INT, 0, MPI_COMM_WORLD);
    numProfiles=(database.geologicunits[iDb]).numprofiles;
    MPI_Barrier( MPI_COMM_WORLD ); 
    //    fprintf(stdout, "\n   myID=%d before allocation",myID);
    
    if(myID!=0) 
      (database.geologicunits[iDb]).velprofiles= malloc(sizeof(velprofile_t)* numProfiles) ;

    MPI_Barrier( MPI_COMM_WORLD ); 
    /* /\*   fprintf(stdout, "\n   myID=%d After barrier numProfiles=%d ",myID,numProfiles); *\/ */

    for (iProfile=0; iProfile < numProfiles; iProfile++)
      MPI_Bcast(&((database.geologicunits[iDb]).velprofiles[iProfile].numpoints),1,MPI_INT, 0, MPI_COMM_WORLD);

    MPI_Barrier( MPI_COMM_WORLD ); 

    if(myID!=0){
      for (iProfile=0; iProfile < numProfiles; iProfile++){
 	numPoints=(database.geologicunits[iDb]).velprofiles[iProfile].numpoints;	
     	(database.geologicunits[iDb]).velprofiles[iProfile].depth= malloc(sizeof(float)*numPoints); 
    	(database.geologicunits[iDb]).velprofiles[iProfile].vs   = malloc(sizeof(float)*numPoints); 
     	(database.geologicunits[iDb]).velprofiles[iProfile].vp   = malloc(sizeof(float)*numPoints); 
     	(database.geologicunits[iDb]).velprofiles[iProfile].rho  = malloc(sizeof(float)*numPoints); 
      } 
    } 

    MPI_Barrier( MPI_COMM_WORLD );

    for (iProfile=0; iProfile < numProfiles; iProfile++){
      numPoints=(database.geologicunits[iDb]).velprofiles[iProfile].numpoints;
      MPI_Bcast((database.geologicunits[iDb]).velprofiles[iProfile].depth,numPoints, MPI_FLOAT, 0, MPI_COMM_WORLD);
      MPI_Bcast((database.geologicunits[iDb]).velprofiles[iProfile].vp   ,numPoints, MPI_FLOAT, 0, MPI_COMM_WORLD);
      MPI_Bcast((database.geologicunits[iDb]).velprofiles[iProfile].vs   ,numPoints, MPI_FLOAT, 0, MPI_COMM_WORLD);
      MPI_Bcast((database.geologicunits[iDb]).velprofiles[iProfile].rho  ,numPoints, MPI_FLOAT, 0, MPI_COMM_WORLD);
    }
  
  /*   fprintf(stdout, "\n   myID=%d After bcast velprofiles",myID); */
 
    MPI_Barrier( MPI_COMM_WORLD );

    Broadcast_Surface_veldistribsurface(iDb);
   
  /*   fprintf(stdout, "\n   myID=%d After bcast veldistsurf",myID); */
 
    MPI_Barrier( MPI_COMM_WORLD ); 
  }
  
  MPI_Barrier( MPI_COMM_WORLD ); 


  if((database.geologicunits[iDb]).profiletype==1){

    MPI_Bcast(&((database.geologicunits[iDb]).znormalizationtype),1, MPI_INT, 0, MPI_COMM_WORLD); 
    MPI_Bcast(&((database.geologicunits[iDb]).velprofilefunction),1, MPI_INT, 0, MPI_COMM_WORLD); 
    MPI_Bcast(&((database.geologicunits[iDb]).minvs),1, MPI_DOUBLE, 0, MPI_COMM_WORLD); 
    MPI_Bcast(&((database.geologicunits[iDb]).minvp),1, MPI_DOUBLE, 0, MPI_COMM_WORLD); 
    MPI_Bcast(&((database.geologicunits[iDb]).maxvs),1, MPI_DOUBLE, 0, MPI_COMM_WORLD); 
    MPI_Bcast(&((database.geologicunits[iDb]).maxvp),1, MPI_DOUBLE, 0, MPI_COMM_WORLD); 
    MPI_Bcast(&((database.geologicunits[iDb]).minrho),1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    MPI_Bcast(&((database.geologicunits[iDb]).maxrho),1, MPI_DOUBLE, 0, MPI_COMM_WORLD);

  /* /\*     /\\* weathering scheme *\\/ *\/ */
    MPI_Bcast(&((database.geologicunits[iDb]).weatheringfactor),1, MPI_DOUBLE, 0, MPI_COMM_WORLD); 
    MPI_Bcast(&((database.geologicunits[iDb]).weatheringexp)   ,1, MPI_DOUBLE, 0, MPI_COMM_WORLD); 

    switch( (database.geologicunits[iDb]).velprofilefunction){ 
    case 0: /* Polynomial */
      MPI_Bcast(&((database.geologicunits[iDb]).poly.order),1, MPI_INT, 0, MPI_COMM_WORLD); 
      order=(database.geologicunits[iDb]).poly.order;
      if(myID!=0){
	(database.geologicunits[iDb]).poly.coefficient=malloc(sizeof(double)*(order+1));
	(database.geologicunits[iDb]).poly.exponent   =malloc(sizeof(double)*(order+1));
      } 
      //      fprintf(stdout,"\n coef = %lf %lf %lf myID=%d",(database.geologicunits[iDb]).poly.coefficient[0],(database.geologicunits[iDb]).poly.coefficient[1],(database.geologicunits[iDb]).poly.coefficient[2],myID);

      MPI_Barrier( MPI_COMM_WORLD );  
      MPI_Bcast(((database.geologicunits[iDb]).poly.coefficient),order+1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      MPI_Bcast(((database.geologicunits[iDb]).poly.exponent )  ,order+1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      MPI_Bcast(&((database.geologicunits[iDb]).vpvsa) ,1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      MPI_Bcast(&((database.geologicunits[iDb]).vpvsb) ,1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      break; 
    case 1:
      MPI_Bcast(&((database.geologicunits[iDb]).exponentprofile) ,1, MPI_DOUBLE, 0, MPI_COMM_WORLD); 
      break; 
    }
  }

  MPI_Barrier( MPI_COMM_WORLD ); 

  if((database.geologicunits[iDb]).profiletype==2){

    MPI_Bcast(&((database.geologicunits[iDb]).znormalizationtype),1, MPI_INT, 0, MPI_COMM_WORLD); 
    MPI_Bcast(&((database.geologicunits[iDb]).velprofilefunction),1, MPI_INT, 0, MPI_COMM_WORLD); 
    MPI_Bcast(&((database.geologicunits[iDb]).minvs),1, MPI_DOUBLE, 0, MPI_COMM_WORLD); 
    MPI_Bcast(&((database.geologicunits[iDb]).minvp),1, MPI_DOUBLE, 0, MPI_COMM_WORLD); 
    MPI_Bcast(&((database.geologicunits[iDb]).maxvs),1, MPI_DOUBLE, 0, MPI_COMM_WORLD); 
    MPI_Bcast(&((database.geologicunits[iDb]).maxvp),1, MPI_DOUBLE, 0, MPI_COMM_WORLD); 
    MPI_Bcast(&((database.geologicunits[iDb]).minrho),1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    MPI_Bcast(&((database.geologicunits[iDb]).maxrho),1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    
    /* /\*     /\\* weathering scheme *\\/ *\/ */
    MPI_Bcast(&((database.geologicunits[iDb]).weatheringfactor),1, MPI_DOUBLE, 0, MPI_COMM_WORLD); 
    MPI_Bcast(&((database.geologicunits[iDb]).weatheringexp)   ,1, MPI_DOUBLE, 0, MPI_COMM_WORLD);  
    MPI_Barrier( MPI_COMM_WORLD );

    if((database.geologicunits[iDb]).velprofilefunction==0){ 
     /* Polynomial */
      MPI_Bcast(&((database.geologicunits[iDb]).poly.order),1, MPI_INT, 0, MPI_COMM_WORLD); 
      order=(database.geologicunits[iDb]).poly.order;
      if(myID!=0){
	(database.geologicunits[iDb]).poly.coefficient=malloc(sizeof(double)*(order+1));
	(database.geologicunits[iDb]).poly.exponent   =malloc(sizeof(double)*(order+1));
      } 
      
      MPI_Barrier( MPI_COMM_WORLD );  
      MPI_Bcast(((database.geologicunits[iDb]).poly.coefficient),order+1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      MPI_Bcast(((database.geologicunits[iDb]).poly.exponent )  ,order+1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      MPI_Bcast(&((database.geologicunits[iDb]).vpvsa) ,1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      MPI_Bcast(&((database.geologicunits[iDb]).vpvsb) ,1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    }
    if((database.geologicunits[iDb]).velprofilefunction==1){
      MPI_Bcast(&((database.geologicunits[iDb]).exponentprofile) ,1, MPI_DOUBLE, 0, MPI_COMM_WORLD);     
    }
    MPI_Bcast(&((database.geologicunits[iDb]).factorvs30) ,1, MPI_DOUBLE, 0, MPI_COMM_WORLD); 
    Broadcast_Surface_vs30(iDb);
  }
 MPI_Barrier( MPI_COMM_WORLD );  
}


int BroadCast_Database(){ /* everybody has the database and theDatabasePath */

  double  double_message[8];
  int     int_message[9];
  int iDb, howmanySurfaces=0, howmanyMeshes=0;
  
  if(myID==0){
    double_message[0]=database.xmin;
    double_message[1]=database.xmax;
    double_message[2]=database.ymin;
    double_message[3]=database.ymax;
    double_message[4]=database.depthmin;
    double_message[5]=database.depthmax;
    double_message[6]=database.datum;
    double_message[7]=database.depth0;
    int_message[0]=database.numberofobjects;
  }
  MPI_Barrier( MPI_COMM_WORLD );
  if(myID==0){
    fprintf(stdout, "\nBroadcasting information DB:.....");
    fprintf(stdout, "\n   bcast 1 DB:.....");
    fflush(stdout);
  }

  MPI_Bcast(double_message, 8, MPI_DOUBLE, 0, MPI_COMM_WORLD);
  MPI_Bcast(int_message   , 1, MPI_INT, 0, MPI_COMM_WORLD);
  
  database.xmin    = double_message[0];
  database.xmax    = double_message[1];
  database.ymin    = double_message[2];
  database.ymax    = double_message[3];
  database.depthmin= double_message[4];
  database.depthmax= double_message[5];
  database.datum   = double_message[6];
  database.depth0  = double_message[7];
    database.numberofobjects=int_message[0];

  if(myID==0){
    fprintf(stdout, "\n   bcast 2 DB:.....\n");
    fflush(stdout);
  }
  else{    
    database.priority = malloc( sizeof(int) * database.numberofobjects );
    database.subtype  = malloc( sizeof(int) * database.numberofobjects );
  } 
   
  MPI_Barrier( MPI_COMM_WORLD );
  database.howmanygeologicunits=0;
  database.howmanymeshes       =0;
  for (iDb =0;iDb<database.numberofobjects; iDb++) {
    
    MPI_Bcast(&(database.subtype[iDb]),1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&(database.priority[iDb]),1, MPI_INT, 0, MPI_COMM_WORLD);
    
    if( database.subtype[iDb] == 0 ) (database.howmanygeologicunits)+=1;
    if( database.subtype[iDb] == 1 ) (database.howmanymeshes)+=1;
  }
  
  MPI_Barrier( MPI_COMM_WORLD );
   if(myID!=0){
      database.meshes        = malloc(sizeof(meshdb_t) * database.howmanymeshes);
      database.geologicunits = malloc(sizeof(geologicunit_t)*database.howmanygeologicunits);
    }
   
  MPI_Barrier( MPI_COMM_WORLD );

  if(myID==0){
    fprintf(stdout, "   bcast 3 DB:.....\n");
    fflush(stdout);
  }
    
  MPI_Barrier( MPI_COMM_WORLD );
  /* GO THROUGH GEOLOGIC UNITS BOTTOM SURFACES AND VELOCITY DISTRIBUTION*/
  for (iDb =0;iDb<database.howmanygeologicunits; iDb++) {
      Broadcast_Surface(iDb);
      MPI_Barrier( MPI_COMM_WORLD );
      if(myID==0){fprintf(stdout," \n Geolunit bcast S %d ", iDb ); fflush(stdout);}
      MPI_Bcast(&((database.geologicunits[iDb]).layertype)      ,1, MPI_INT, 0, MPI_COMM_WORLD);
      MPI_Bcast(&((database.geologicunits[iDb]).isbottombedrock),1, MPI_INT, 0, MPI_COMM_WORLD);
      MPI_Bcast(&((database.geologicunits[iDb]).profiletype)    ,1, MPI_INT, 0, MPI_COMM_WORLD);
      MPI_Barrier( MPI_COMM_WORLD );
      if(myID==0){fprintf(stdout," Geolunit bcast S3 %d ", iDb ); fflush(stdout);}
      MPI_Barrier( MPI_COMM_WORLD );
      Broadcast_Velocity_Distribution(iDb);
      MPI_Barrier( MPI_COMM_WORLD );
      if(myID==0){fprintf(stdout," Geolunit bcast F3 %d ", iDb );fflush(stdout);}
      MPI_Barrier( MPI_COMM_WORLD );
      MPI_Bcast(&((database.geologicunits[iDb]).isexcluded)     ,1, MPI_INT, 0, MPI_COMM_WORLD);
      MPI_Bcast(&((database.geologicunits[iDb]).isbottomfreesurface),1, MPI_INT, 0, MPI_COMM_WORLD);
      MPI_Bcast(&((database.geologicunits[iDb]).interpolationscheme),1, MPI_INT, 0, MPI_COMM_WORLD);
      MPI_Bcast(&((database.geologicunits[iDb]).factorsurface),1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      MPI_Bcast(&((database.geologicunits[iDb]).minvp),1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      MPI_Bcast(&((database.geologicunits[iDb]).minvs),1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      MPI_Bcast(&((database.geologicunits[iDb]).minrho),1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      MPI_Bcast(&((database.geologicunits[iDb]).maxvp),1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      MPI_Bcast(&((database.geologicunits[iDb]).maxvs),1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      MPI_Bcast(&((database.geologicunits[iDb]).maxrho),1, MPI_DOUBLE, 0, MPI_COMM_WORLD);

  } 

}

/**
 * Program's standard entry point.
 *
 * \param argc number of arguments in the command line (duh!).
 * \param argv actual command line arguments.  The valid command line
 *	arguments depend on the compile defined flags (JC: this should
 *	be changed, it should be uniform!).
 * Here's the list of valid command line arguments when VIS is not define
 * (-DVIS):
 * -1 cvmdb
 * -2 physics.in
 * -3 numerical.in
 * -4 meshetree
 * -5 4D-output
 *
 * \see local_init
 */
int
main (int argc, char **argv)
{
#ifdef DEBUG
  int32_t flag;
  MPI_Status status;
#endif /* DEBUG */

  /* MPI initialization */
  MPI_Init(&argc, &argv);
  MPI_Barrier(MPI_COMM_WORLD);
  theE2ETime = -MPI_Wtime();

  MPI_Comm_rank(MPI_COMM_WORLD, &myID);
  MPI_Comm_size(MPI_COMM_WORLD, &theGroupSize);

  if (myID == 0) {
    fprintf(stderr, "Starting Version 3-08 of code on %d PEs.\n", theGroupSize);
    fprintf(stderr, "\n");
    fflush (stderr);
  }

  local_init(argc, argv);

  /* Input stencil plane */
  if (myID == 0) {
    Init_Database(&database,theDatabasePath,0);
  }
  BroadCast_Database(&database,theDatabasePath);

  if (myID == 0) {
    fprintf(stdout, "Starting Mesh generation and Setup \n", theGroupSize);
    fprintf(stdout, "\n");
    fflush (stdout);
  }

  MPI_Barrier(MPI_COMM_WORLD);
  /* Generate, partition and output unstructured octree mesh */
  mesh_generate();

  etree_correct_properties_topo();
  MPI_Barrier(MPI_COMM_WORLD);

  // mesh_output_matlab_format();
  if (theMeshOutFlag && DO_OUTPUT) {
    mesh_output();
   }
 
  // fprintf(stdout,"balin");
  //fflush(stdout);  
  //exit(1);
  mesh_printstat();

     /* Initialize the output planes ( displacements in a regular grid ) */
  if (theNumberOfPlanes != 0) {
    output_planes_setup(argv[3]);
    //MPI_Barrier(MPI_COMM_WORLD);

    if(myID==0)
      fprintf(stdout,"\noutput_planes_setup:done");
    fflush(stdout);
  }

  MPI_Barrier(MPI_COMM_WORLD);

  //  fprintf(stdout,"balin");
  //fflush(stdout);  
  //exit(1);

  if (theNumberOfStations !=0 ){
    output_stations_init(argv[3]);
    MPI_Barrier(MPI_COMM_WORLD);

    if(myID==0)
      fprintf(stdout,"\noutput_stations_init:done");	
    fflush(stdout);
  }

  MPI_Bcast (&theSurfaceCornersLong,4,MPI_DOUBLE,0,MPI_COMM_WORLD);
  MPI_Bcast (&theSurfaceCornersLat ,4,MPI_DOUBLE,0,MPI_COMM_WORLD);

  MPI_Barrier(MPI_COMM_WORLD);

  /* Initialize the solver, source and output structure */
  solver_init();
  MPI_Barrier(MPI_COMM_WORLD);

  solver_printstat();
  MPI_Barrier(MPI_COMM_WORLD);

  source_init(argv[2]);

  output_init (argv[3], &theOutputParameters);

  /* Run the solver and output the results */
  MPI_Barrier(MPI_COMM_WORLD);
  theSolverTime = -MPI_Wtime();

  solver_run();

  MPI_Barrier(MPI_COMM_WORLD);
  theSolverTime += MPI_Wtime(); /* Include vis time if VIS is enabled */

  output_fini();

#ifdef DEBUG
  /* Does the OS page out my resident set ? */
  if ((myID % PROCPERNODE) == 0) {
    /* system("ps xl"); */
  }
    
  /* Are there pending messages that I haven't processed */
  MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &status);
  if (flag != 0) {
    fprintf(stderr, "Thread %d: MPI error: unreceived incoming message\n",
	    myID);
  }
#endif /* DEBUG */

  /* Close the output planes */
  if (theNumberOfPlanes != 0) {
    output_planes_finalize();
  }        

  local_finalize();
  output_get_stats();

 MPI_Barrier(MPI_COMM_WORLD);
  theE2ETime += MPI_Wtime();

  /* Print out the timing stat */
  if (myID == 0) {
    print_timing_stat();
  }

  MPI_Finalize();

  return 0;
}

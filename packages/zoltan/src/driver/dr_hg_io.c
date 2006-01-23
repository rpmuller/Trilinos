/*****************************************************************************
 * Zoltan Library for Parallel Applications                                  *
 * Copyright (c) 2000,2001,2002, Sandia National Laboratories.               *
 * For more info, see the README file in the top-level Zoltan directory.     *  
 *****************************************************************************/
/*****************************************************************************
 * CVS File Information :
 *    $RCSfile$
 *    $Author$
 *    $Date$
 *    $Revision$
 ****************************************************************************/

#include <mpi.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>

#include "dr_const.h"
#include "dr_input_const.h"
#include "dr_util_const.h"
#include "dr_par_util_const.h"
#include "dr_err_const.h"
#include "dr_output_const.h"
#include "dr_elem_util_const.h"
#include "dr_maps_const.h"
#include "ch_input_const.h"
#include "ch_init_dist_const.h"
#include "dr_hg_readfile.h"

#ifdef __cplusplus
/* if C++, define the rest of this header file as extern C */
extern "C" {
#endif

#ifndef MAX_STR_LENGTH
#define MAX_STR_LENGTH 80
#endif

static int dist_hyperedges(MPI_Comm comm, PARIO_INFO_PTR, int, int, int, int *,
                           int *, int **, int **, int **, int **, 
                           int *, float **, short *);
static int process_mtxp_file(char *filebuf, int fsize, 
  int nprocs, int myrank,
  int *nGlobalEdges, int *nGlobalVtxs, int *vtxWDim, int *edgeWDim,
  int *nMyPins, int **myPinI, int **myPinJ,
  int *nMyVtx, int **myVtxNum, float **myVtxWgts,
  int *nMyEdgeWgts, int **myEdgeNum, float **myEdgeWgts);
static int create_edge_lists(int nMyPins, int *myPinI, int *myPinJ, 
      int *numHEdges, int **edgeGno, int **edgeIdx, int **pinGno);

/****************************************************************************/
/****************************************************************************/
/****************************************************************************/

/* Read in MatrixMarket file. 
 * For now, just call the hypergraph routine. 
 * In the future we may want to allow both graphs and hypergraphs 
 * to be read in MatrixMarket format.
 */
int read_mm_file(
  int Proc,
  int Num_Proc,
  PROB_INFO_PTR prob,
  PARIO_INFO_PTR pio_info,
  MESH_INFO_PTR mesh
)
{
  return read_hypergraph_file(Proc, Num_Proc, prob, pio_info, mesh);
}

/* Read from file and set up hypergraph. */
int read_hypergraph_file(
  int Proc,
  int Num_Proc,
  PROB_INFO_PTR prob,
  PARIO_INFO_PTR pio_info,
  MESH_INFO_PTR mesh
)
{
  /* Local declarations. */
  const char  *yo = "read_hypergraph_file";
  char   cmesg[256];

  int    i, gnvtxs; 
  int    nvtxs = 0, gnhedges = 0, nhedges = 0, npins = 0;
  int    vwgt_dim=0, hewgt_dim=0;
  int   *hindex = NULL, *hvertex = NULL, *hvertex_proc = NULL;
  int   *hgid = NULL;
  float *hewgts = NULL, *vwgts = NULL;
  FILE  *fp;
  int base = 0;   /* Smallest vertex number; usually zero or one. */
  char filename[256];

  /* Variables that allow graph-based functions to be reused. */
  /* If no chaco.graph or chaco.coords files exist, values are NULL or 0, 
   * since graph is not being built. If chaco.graph and/or chaco.coords
   * exist, these arrays are filled and values stored in mesh. 
   * Including these files allows for comparison of HG methods with other
   * methods, along with visualization of results and comparison of 
   * LB_Eval results.
   */
  int    ch_nvtxs = 0;        /* Temporary values for chaco_read_graph.   */
  int    ch_vwgt_dim = 0;     /* Their values are ignored, as vertex      */
  float *ch_vwgts = NULL;     /* info is provided by hypergraph file.     */
  int   *ch_start = NULL, *ch_adj = NULL, ch_ewgt_dim = 0;
  short *ch_assignments = NULL;
  float *ch_ewgts = NULL;
  int    ch_ndim = 0;
  float *ch_x = NULL, *ch_y = NULL, *ch_z = NULL;
  int    ch_no_geom = TRUE;   /* Assume no geometry info is given; reset if
                                 it is provided. */
  int    file_error = 0;

/***************************** BEGIN EXECUTION ******************************/

  DEBUG_TRACE_START(Proc, yo);

  if (Proc == 0) {

    /* Open and read the hypergraph file. */
    if (pio_info->file_type == HYPERGRAPH_FILE)
      sprintf(filename, "%s.hg", pio_info->pexo_fname);
    else if (pio_info->file_type == MATRIXMARKET_FILE)
      sprintf(filename, "%s.mtx", pio_info->pexo_fname);
    else {
        sprintf(cmesg, "fatal:  invalid file type %d", pio_info->file_type);
        Gen_Error(0, cmesg);
        return 0;
    }

    if ((fp = fopen(filename, "r")) == NULL) {
      /* If that didn't work, try without any suffix */
      sprintf(filename, "%s", pio_info->pexo_fname);
      fp = fopen(filename, "r");
    }
    file_error = (fp == NULL);
  }

  MPI_Bcast(&file_error, 1, MPI_INT, 0, MPI_COMM_WORLD);

  if (file_error) {
    sprintf(cmesg, "fatal:  Could not open hypergraph file %s", filename);
    Gen_Error(0, cmesg);
    return 0;
  }

  if (Proc == 0) {

    /* read the array in on processor 0 */
    if (pio_info->file_type == HYPERGRAPH_FILE) {
      if (HG_readfile(Proc, fp, &nvtxs, &nhedges, &npins,
                      &hindex, &hvertex, &vwgt_dim, &vwgts, 
                      &hewgt_dim, &hewgts, &base) != 0){
        Gen_Error(0, "fatal: Error returned from HG_readfile");
        return 0;
      }
    }
    else if (pio_info->file_type == MATRIXMARKET_FILE) {
      if (MM_readfile(Proc, fp, &nvtxs, &nhedges, &npins,
                      &hindex, &hvertex, &vwgt_dim, &vwgts, 
                      &hewgt_dim, &hewgts, &base, pio_info->matrix_obj) 
                      != 0){
        Gen_Error(0, "fatal: Error returned from MM_readfile");
        return 0;
      }
    }

    fclose(fp);

    /* If CHACO graph file is available, read it. */
    sprintf(filename, "%s.graph", pio_info->pexo_fname);
    fp = fopen(filename, "r");
    if (fp != NULL) {
      /* CHACO graph file is available. */
      /* Assuming hypergraph vertices are same as chaco vertices. */
      /* Chaco vertices and their weights are ignored in rest of function. */
      if (chaco_input_graph(fp, filename, &ch_start, &ch_adj, &ch_nvtxs,
                      &ch_vwgt_dim, &ch_vwgts, &ch_ewgt_dim, &ch_ewgts) != 0) {
        Gen_Error(0, "fatal: Error returned from chaco_input_graph");
        return 0;
      }
    }


    /* If coordinate file is available, read it. */
    sprintf(filename, "%s.coords", pio_info->pexo_fname);
    fp = fopen(filename, "r");
    if (fp != NULL) {
      /* CHACO coordinates file is available. */
      ch_no_geom = FALSE;
      if (chaco_input_geom(fp, filename, ch_nvtxs, &ch_ndim, 
                           &ch_x, &ch_y, &ch_z) != 0) {
        Gen_Error(0, "fatal: Error returned from chaco_input_geom");
        return 0;
      }
    }

       /* Read Chaco assignment file, if requested */
    if (pio_info->init_dist_type == INITIAL_FILE) {
      sprintf(filename, "%s.assign", pio_info->pexo_fname);
      fp = fopen(filename, "r");
      if (fp == NULL) {
        sprintf(cmesg, "Error:  Could not open Chaco assignment file %s; "
                "initial distribution cannot be read",
                filename);
        Gen_Error(0, cmesg);
        return 0;
      }
      else {
        /* read the coordinates in on processor 0 */
        ch_assignments = (short *) malloc(nvtxs * sizeof(short));
        if (nvtxs && !ch_assignments) {
          Gen_Error(0, "fatal: memory error in read_hypergraph_file");
          return 0;
        }
        if (chaco_input_assign(fp, filename, ch_nvtxs, ch_assignments) != 0){
          Gen_Error(0, "fatal: Error returned from chaco_input_assign");
          return 0;
        }
      }
    }
  }
  
  MPI_Bcast(&base, 1, MPI_INT, 0, MPI_COMM_WORLD);

  /* Distribute hypergraph graph */
  /* Use hypergraph vertex information and chaco edge information. */
  if (!chaco_dist_graph(MPI_COMM_WORLD, pio_info, 0, &gnvtxs, &nvtxs, 
             &ch_start, &ch_adj, &vwgt_dim, &vwgts, &ch_ewgt_dim, &ch_ewgts,
             &ch_ndim, &ch_x, &ch_y, &ch_z, &ch_assignments) != 0) {
    Gen_Error(0, "fatal: Error returned from chaco_dist_graph");
    return 0;
  }

  if (!dist_hyperedges(MPI_COMM_WORLD, pio_info, 0, base, gnvtxs, &gnhedges,
                       &nhedges, &hgid, &hindex, &hvertex, &hvertex_proc,
                       &hewgt_dim, &hewgts, ch_assignments)) {
    Gen_Error(0, "fatal: Error returned from dist_hyperedges");
    return 0;
  }
                       

  /* Initialize mesh structure for Hypergraph. */
  mesh->data_type = HYPERGRAPH;
  mesh->num_elems = nvtxs;
  mesh->vwgt_dim = vwgt_dim;
  mesh->ewgt_dim = ch_ewgt_dim;
  mesh->elem_array_len = mesh->num_elems + 5;
  mesh->num_dims = ch_ndim;
  mesh->num_el_blks = 1;

  mesh->gnhedges = gnhedges;
  mesh->nhedges = nhedges;
  mesh->hewgt_dim = hewgt_dim;

  mesh->hgid = hgid;
  mesh->hindex = hindex;
  mesh->hvertex = hvertex;
  mesh->hvertex_proc = hvertex_proc;
  mesh->heNumWgts = nhedges;
  mesh->heWgtId = NULL;
  mesh->hewgts = hewgts;
  

  mesh->eb_etypes = (int *) malloc (5 * mesh->num_el_blks * sizeof(int));
  if (!mesh->eb_etypes) {
    Gen_Error(0, "fatal: insufficient memory");
    return 0;
  }
  mesh->eb_ids = mesh->eb_etypes + mesh->num_el_blks;
  mesh->eb_cnts = mesh->eb_ids + mesh->num_el_blks;
  mesh->eb_nnodes = mesh->eb_cnts + mesh->num_el_blks;
  mesh->eb_nattrs = mesh->eb_nnodes + mesh->num_el_blks;

  mesh->eb_names = (char **) malloc (mesh->num_el_blks * sizeof(char *));
  if (!mesh->eb_names) {
    Gen_Error(0, "fatal: insufficient memory");
    return 0;
  }

  mesh->eb_etypes[0] = -1;
  mesh->eb_ids[0] = 1;
  mesh->eb_cnts[0] = nvtxs;
  mesh->eb_nattrs[0] = 0;
  /*
   * Each element has one set of coordinates (i.e., node) if a coords file
   * was provided; zero otherwise.
   */
  MPI_Bcast( &ch_no_geom, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (ch_no_geom)
    mesh->eb_nnodes[0] = 0;
  else
    mesh->eb_nnodes[0] = 1;

  /* allocate space for name */
  mesh->eb_names[0] = (char *) malloc((MAX_STR_LENGTH+1) * sizeof(char));
  if (!mesh->eb_names[0]) {
    Gen_Error(0, "fatal: insufficient memory");
    return 0;
  }
  strcpy(mesh->eb_names[0], "hypergraph");

  /* allocate the element structure array */
  mesh->elements = (ELEM_INFO_PTR) malloc (mesh->elem_array_len 
                                         * sizeof(ELEM_INFO));
  if (!(mesh->elements)) {
    Gen_Error(0, "fatal: insufficient memory");
    return 0;
  }

  /*
   * intialize all of the element structs as unused by
   * setting the globalID to -1
   */
  for (i = 0; i < mesh->elem_array_len; i++) 
    initialize_element(&(mesh->elements[i]));

  /*
   * now fill the element structure array with the
   * information from the Chaco file
   * Use hypergraph vertex information and chaco edge information.
   */
  if (!chaco_fill_elements(Proc, Num_Proc, prob, mesh, gnvtxs, nvtxs,
                     ch_start, ch_adj, vwgt_dim, vwgts, ch_ewgt_dim, ch_ewgts, 
                     ch_ndim, ch_x, ch_y, ch_z, ch_assignments, base)) {
    Gen_Error(0, "fatal: Error returned from chaco_fill_elements");
    return 0;
  }

  safe_free((void **) &vwgts);
  safe_free((void **) &ch_ewgts);
  safe_free((void **) &ch_vwgts);
  safe_free((void **) &ch_x);
  safe_free((void **) &ch_y);
  safe_free((void **) &ch_z);
  safe_free((void **) &ch_start);
  safe_free((void **) &ch_adj);
  safe_free((void **) &ch_assignments);

 if (Debug_Driver > 3)
   print_distributed_mesh(Proc, Num_Proc, mesh);

  DEBUG_TRACE_END(Proc, yo);
  return 1;
}
/*****************************************************************************/
static int dist_hyperedges(
  MPI_Comm comm,		/* MPI Communicator */
  PARIO_INFO_PTR pio_info,      /* Parallel IO info */
  int     host_proc,		/* processor where all the data is initially */
  int     base,                 /* indicates whether input is 0-based or
                                   1-based (i.e., is lowest vertex number 
                                   0 or 1?). */
  int     gnvtxs,               /* global number of vertices */
  int     *gnhedges,		/* global number of hyperedges */
  int     *nhedges,		/* local number of hyperedges */
  int     **hgid,		/* global hyperedge numbers */
  int     **hindex,		/* Starting hvertex entry for hyperedges */
  int     **hvertex,		/* Array of vertices in hyperedges; returned
                                   values are global IDs for vertices */
  int     **hvertex_proc,	/* Array of processor assignments for 
                                   vertices in hvertex.  */
  int     *hewgt_dim,           /* number of weights per hyperedge */
  float   **hewgts,		/* hyperedge weight list data */
  short   *assignments          /* assignments from Chaco file; may be NULL */
)
{
/*
 * Distribute hyperedges from one processor to all processors.
 * Vertex distribution is assumed already done through chaco_dist_graph.
 * The memory for the hyperedges on the host node is freed
 * and fresh memory is allocated for the distributed hyperedges.
 */

const char *yo = "dist_hyperedges";
int nprocs, myproc, i, h, p;
int *old_hindex = NULL, *old_hvertex = NULL, *old_hvertex_proc = NULL; 
int *size = NULL, *num_send = NULL;
int **send = NULL;
int *send_hgid = NULL, *send_hindex = NULL; 
int *send_hvertex = NULL, *send_hvertex_proc = NULL;
int max_size, max_num_send;
int hcnt[2];
int hecnt, hvcnt;
float *old_hewgts = NULL;
float *send_hewgts = NULL;
MPI_Status status;
int num_dist_procs;
int hedge_init_dist_type;

  hedge_init_dist_type = (pio_info->init_dist_type != INITIAL_FILE 
                          ? pio_info->init_dist_type
                          : INITIAL_LINEAR);

  /* Determine number of processors and my rank. */
  MPI_Comm_size (comm, &nprocs );
  MPI_Comm_rank (comm, &myproc );

  DEBUG_TRACE_START(myproc, yo);

  if (pio_info->init_dist_procs > 0 && pio_info->init_dist_procs <= nprocs)
    num_dist_procs = pio_info->init_dist_procs;
  else
    /* Reset num_dist_procs if not set validly by input */
    num_dist_procs = nprocs;

  /* Broadcast to all procs */
  MPI_Bcast( hewgt_dim, 1, MPI_INT, host_proc, comm);
  MPI_Bcast( nhedges, 1, MPI_INT, host_proc, comm);
  *gnhedges = *nhedges;

  /* Initialize */
  if (*gnhedges == 0) {
    *hindex = (int *) malloc(sizeof(int));
    if (!(*hindex)) {
      Gen_Error(0, "fatal: insufficient memory");
      return 0;
    }
    (*hindex)[0] = 0;
    *hvertex = NULL;
    *hvertex_proc = NULL;
    return 1;
  }
 
  if (nprocs == 1) {
    *nhedges = *gnhedges;
    *hgid = (int *) malloc(*gnhedges * sizeof(int));
    *hvertex_proc = (int *) malloc((*hindex)[*gnhedges] * sizeof(int));
    if ((*gnhedges && !(*hgid)) || ((*hindex)[*gnhedges] && !(*hvertex_proc))) {
      Gen_Error(0, "fatal: insufficient memory");
      return 0;
    }
    for (h = 0; h < *gnhedges; h++)
      (*hgid)[h] = h;
    for (h = 0; h < (*hindex)[*gnhedges]; h++)
      (*hvertex_proc)[h] = 0;
    return 1;
  }
  if (myproc == host_proc) {
    /* Store pointers to original data */
    old_hindex  = *hindex;
    old_hvertex = *hvertex;
    old_hewgts = *hewgts;
    old_hvertex_proc = (int *) malloc(old_hindex[*gnhedges] * sizeof(int));

    /* Allocate space for size and send flags */
    size = (int *) calloc(2 * nprocs, sizeof(int));
    num_send = size + nprocs;
    send = (int **) malloc(nprocs * sizeof(int *));
    if ((old_hindex[*gnhedges] && !old_hvertex_proc) || !size || !send) {
      Gen_Error(0, "fatal: insufficient memory");
      return 0;
    }

    for (i = 0; i < nprocs; i++) {
      send[i] = (int *) calloc(*gnhedges, sizeof(int));
      if (*gnhedges && !send[i]) {
        Gen_Error(0, "fatal: memory error in dist_hyperedges");
        return 0;
      }
    }

    /* Determine to which processors hyperedges should be sent */
    for (h = 0; h < *gnhedges; h++) {
      if (hedge_init_dist_type == INITIAL_CYCLIC)  
        p = h % num_dist_procs;
      else if (hedge_init_dist_type == INITIAL_LINEAR) 
        p = (int) ((float) (h * num_dist_procs) / (float)(*gnhedges));
      else if (hedge_init_dist_type == INITIAL_OWNER) 
        p = ch_dist_proc(old_hvertex[old_hindex[h]], assignments, base);
      size[p] += (old_hindex[h+1] - old_hindex[h]);
      num_send[p]++;
      send[p][h] = 1;
      for (i = old_hindex[h]; i < old_hindex[h+1]; i++) 
        old_hvertex_proc[i] = ch_dist_proc(old_hvertex[i], assignments, base);
    }

    /* Determine size of send buffers and allocate them. */
    max_size = 0;
    max_num_send = 0;
    for (p = 0; p < nprocs; p++) {
      if (size[p] > max_size) max_size = size[p];
      if (num_send[p] > max_num_send) max_num_send = num_send[p];
    }

    send_hgid = (int *) malloc((max_num_send) * sizeof(int));
    send_hindex = (int *) malloc((max_num_send+1) * sizeof(int));
    send_hvertex = (int *) malloc(max_size * sizeof(int));
    send_hvertex_proc = (int *) malloc(max_size * sizeof(int));
    if (*hewgt_dim)
      send_hewgts = (float *) malloc(max_num_send*(*hewgt_dim)*sizeof(float));
    if ((max_num_send && !send_hgid) || !send_hindex || 
        (max_size && (!send_hvertex || !send_hvertex_proc)) ||
        (max_num_send && *hewgt_dim && !send_hewgts)) {
      Gen_Error(0, "fatal: memory error in dist_hyperedges");
      return 0;
    }

    /* Load and send data */
    for (p = 0; p < nprocs; p++) {

      if (p == myproc) continue;

      hecnt = 0;
      hvcnt = 0;
      send_hindex[0] = 0;
      for (h = 0; h < *gnhedges; h++) {
        if (send[p][h]) {
          send_hgid[hecnt] = h;
          send_hindex[hecnt+1] = send_hindex[hecnt] 
                               + (old_hindex[h+1] - old_hindex[h]);
          for (i = 0; i < *hewgt_dim; i++)
            send_hewgts[hecnt*(*hewgt_dim)+i] = old_hewgts[h*(*hewgt_dim)+i];
          hecnt++;
          for (i = old_hindex[h]; i < old_hindex[h+1]; i++) {
            send_hvertex[hvcnt] = old_hvertex[i];
            send_hvertex_proc[hvcnt] = old_hvertex_proc[i];
            hvcnt++;
          }
        }
      }

      hcnt[0] = hecnt;
      hcnt[1] = hvcnt;

      MPI_Send(hcnt, 2, MPI_INT, p, 1, comm);
      MPI_Send(send_hgid, hecnt, MPI_INT, p, 2, comm);
      MPI_Send(send_hindex, hecnt+1, MPI_INT, p, 3, comm);
      MPI_Send(send_hvertex, hvcnt, MPI_INT, p, 4, comm);
      MPI_Send(send_hvertex_proc, hvcnt, MPI_INT, p, 5, comm);
      if (*hewgt_dim)
        MPI_Send(send_hewgts, hecnt*(*hewgt_dim), MPI_FLOAT, p, 6, comm);
    }

    safe_free((void **) &send_hgid);
    safe_free((void **) &send_hindex);
    safe_free((void **) &send_hvertex);
    safe_free((void **) &send_hvertex_proc);
    safe_free((void **) &send_hewgts);

    /* Copy data owned by myproc into new local storage */
    *nhedges = num_send[myproc];
    *hgid = (int *) malloc(*nhedges * sizeof(int));
    *hindex = (int *) malloc((*nhedges+1) * sizeof(int));
    *hvertex = (int *) malloc(size[myproc] * sizeof(int));
    *hvertex_proc = (int *) malloc(size[myproc] * sizeof(int));
    if (*hewgt_dim)
      *hewgts = (float *) malloc(*nhedges * *hewgt_dim * sizeof(float));
    if ((*nhedges && !(*hgid)) || !(*hindex) || 
        (size[myproc] && (!(*hvertex) || !(*hvertex_proc))) ||
        (*nhedges && *hewgt_dim && !(*hewgt_dim))) {
      Gen_Error(0, "fatal: memory error in dist_hyperedges");
      return 0;
    }

    hecnt = 0;
    hvcnt = 0;
    (*hindex)[0] = 0;

    for (h = 0; h < *gnhedges; h++) {
      if (send[myproc][h]) {
        (*hgid)[hecnt] = h;
        (*hindex)[hecnt+1] = (*hindex)[hecnt]
                           + (old_hindex[h+1] - old_hindex[h]);
        for (i = 0; i < *hewgt_dim; i++)
          (*hewgts)[hecnt*(*hewgt_dim)+i] = old_hewgts[h*(*hewgt_dim)+i];
        hecnt++;
        for (i = old_hindex[h]; i < old_hindex[h+1]; i++) {
          (*hvertex_proc)[hvcnt] = old_hvertex_proc[i];
          (*hvertex)[hvcnt] = old_hvertex[i];  /* Global index */
          hvcnt++;
        }
      }
    }

    for (p = 0; p < nprocs; p++) safe_free((void **) &(send[p]));
    safe_free((void **) &send);
    safe_free((void **) &size);
    safe_free((void **) &old_hindex);
    safe_free((void **) &old_hvertex);
    safe_free((void **) &old_hvertex_proc);
    safe_free((void **) &old_hewgts);
  }
  else {  
    /* host_proc != myproc; receive hedge data from host_proc */
    MPI_Recv(hcnt, 2, MPI_INT, host_proc, 1, comm, &status);
    *nhedges = hcnt[0];
    *hgid = (int *) malloc(hcnt[0] * sizeof(int));
    *hindex = (int *) malloc((hcnt[0]+1) * sizeof(int));
    *hvertex = (int *) malloc(hcnt[1] * sizeof(int));
    *hvertex_proc = (int *) malloc(hcnt[1] * sizeof(int));
    if (*hewgt_dim)
      *hewgts = (float *) malloc(hcnt[0] * *hewgt_dim * sizeof(float));
    if ((hcnt[0] && !(*hgid)) || !(*hindex) || 
        (hcnt[1] && (!(*hvertex) || !(*hvertex_proc))) ||
        (hcnt[0] && *hewgt_dim && !(*hewgt_dim))) {
      Gen_Error(0, "fatal: memory error in dist_hyperedges");
      return 0;
    }
    MPI_Recv(*hgid, hcnt[0], MPI_INT, host_proc, 2, comm, &status);
    MPI_Recv(*hindex, hcnt[0]+1, MPI_INT, host_proc, 3, comm, &status);
    MPI_Recv(*hvertex, hcnt[1], MPI_INT, host_proc, 4, comm, &status);
    MPI_Recv(*hvertex_proc, hcnt[1], MPI_INT, host_proc, 5, comm, &status);
    if (*hewgt_dim)
      MPI_Recv(*hewgts, hcnt[0]* *hewgt_dim, MPI_FLOAT, host_proc, 6, comm, 
               &status);
  }

  DEBUG_TRACE_END(myproc, yo);
  return 1;
}

/****************************************************************************/
/****************************************************************************/
/****************************************************************************/
/* Read "matrixmarket plus", the format written by Zoltan_Generate_Files
 * when hypergraph was given with the pin query functions.  (So each
 * process supplies some subset of pins to Zoltan.  Each process owns
 * some of the vertices and supplies weights for those.  Each process
 * may supply weights for edges.  The edges may not be the edges of 
 * their pins.  More than one process may supply a weight for the same
 * edge.)
 *
 * The file format is similar to matrixmarket, but lists vertex weights 
 * and edge weights, and lists processors owning pins, processors owning 
 * vertices, and processors providing edge weights.  
 *
 * The number of processes in the file may be different than the number
 * of zdrive processes.  In that case, some zdrive processes may have
 * no pins and vertices, or some may get the pins and vertices of more
 * than one of the processes in the file.
 */
int read_mtxplus_file(
  int Proc,
  int Num_Proc,
  PROB_INFO_PTR prob,
  PARIO_INFO_PTR pio_info,
  MESH_INFO_PTR mesh
)
{
  /* Local declarations. */
  const char  *yo = "read_mtxplus_file";
  char filename[256], cmesg[256];
  struct stat statbuf;
  int rc, fsize, i, j;
  char *filebuf=NULL;
  FILE *fp;
  int nGlobalEdges, nGlobalVtxs, vtxWDim, edgeWDim; 
  int nMyPins, nMyVtx, nMyEdgeWgts;
  int *myPinI, *myPinJ, *myVtxNum, *myEWGno;
  float *myVtxWgts, *myEdgeWgts;
  int status;
  int numHEdges; 
  int *edgeGno, *edgeIdx, *pinGno;

  DEBUG_TRACE_START(Proc, yo);

  /* Process 0 reads the file and broadcasts it */

  if (Proc == 0) {
    fsize = 0;
    fp = NULL;
    sprintf(filename, "%s.mtxp", pio_info->pexo_fname);

    rc = stat(filename, &statbuf);

    if (rc == 0){
      fsize = statbuf.st_size;

      fp = fopen(filename, "r");

      if (!fp){
        fsize = 0;
      }
      else{
        filebuf = (char *)malloc(fsize+1);

        rc = fread(filebuf, 1, fsize, fp);

        if (rc != fsize){
          free(filebuf);
          fsize = 0;
          fp = NULL;
        }
        else{
          filebuf[fsize] = 0;
          fsize++;
        }
        fclose(fp);
      }
    }
  }

  MPI_Bcast(&fsize, 1, MPI_INT, 0, MPI_COMM_WORLD);

  if (fsize == 0) {
    sprintf(cmesg, "fatal:  Could not open/read hypergraph file %s", filename);
    Gen_Error(0, cmesg);
    return 0;
  }

  if (Proc > 0){
    filebuf = (char *)malloc(fsize);
  }

  MPI_Bcast(filebuf, fsize, MPI_BYTE, 0, MPI_COMM_WORLD);

  /* Each process reads through the file, obtaining it's
   * pins, vertex weights and edge weights.  The file lists
   * global IDs for the vertices and edges.  These will be
   * assigned global numbers based on the order they appear
   * in the file.  The global numbers begin with zero.
   * Returns 1 on success, 0 on failure.
   */

  rc = process_mtxp_file(filebuf, fsize, Num_Proc, Proc,
          &nGlobalEdges, &nGlobalVtxs, &vtxWDim, &edgeWDim,
          &nMyPins, &myPinI, &myPinJ,
          &nMyVtx, &myVtxNum, &myVtxWgts,
          &nMyEdgeWgts, &myEWGno, &myEdgeWgts);

  free(filebuf);

  MPI_Allreduce(&rc, &status, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

  if (status != Num_Proc){
    return 0;
  }

  /*
   * From the lists of pins, create edge lists.  Then
   * write the mesh data structure.
   */

  rc = create_edge_lists(nMyPins, myPinI, myPinJ, 
            &numHEdges, &edgeGno, &edgeIdx, &pinGno);

  MPI_Allreduce(&rc, &status, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

  if (status != Num_Proc){
    return 0;
  }

  safe_free((void **)&myPinI);
  safe_free((void **)&myPinJ);

  /* Initialize mesh structure for Hypergraph. */
  mesh->data_type = HYPERGRAPH;
  mesh->num_elems = nMyVtx;
  mesh->vwgt_dim = vtxWDim;
  mesh->ewgt_dim = 0;
  mesh->elem_array_len = mesh->num_elems + 5;
  mesh->num_dims = 0;
  mesh->num_el_blks = 1;

  mesh->gnhedges = nGlobalEdges;
  mesh->nhedges = numHEdges;
  mesh->hewgt_dim = edgeWDim;

  mesh->hgid = edgeGno;
  mesh->hindex = edgeIdx;
  mesh->hvertex = pinGno;
  mesh->hvertex_proc = NULL;     /* don't know don't care */
  mesh->heNumWgts = nMyEdgeWgts;
  mesh->heWgtId = myEWGno;
  mesh->hewgts = myEdgeWgts;

  mesh->eb_etypes = (int *) malloc (5 * mesh->num_el_blks * sizeof(int));
  if (!mesh->eb_etypes) {
    Gen_Error(0, "fatal: insufficient memory");
    return 0;
  }
  mesh->eb_ids = mesh->eb_etypes + mesh->num_el_blks;
  mesh->eb_cnts = mesh->eb_ids + mesh->num_el_blks;
  mesh->eb_nnodes = mesh->eb_cnts + mesh->num_el_blks;
  mesh->eb_nattrs = mesh->eb_nnodes + mesh->num_el_blks;

  mesh->eb_names = (char **) malloc (mesh->num_el_blks * sizeof(char *));
  if (!mesh->eb_names) {
    Gen_Error(0, "fatal: insufficient memory");
    return 0;
  }

  mesh->eb_etypes[0] = -1;
  mesh->eb_ids[0] = 1;
  mesh->eb_cnts[0] = nGlobalVtxs;
  mesh->eb_nattrs[0] = 0;
  mesh->eb_nnodes[0] = 0;

  /* allocate space for name */
  mesh->eb_names[0] = (char *) malloc((MAX_STR_LENGTH+1) * sizeof(char));
  if (!mesh->eb_names[0]) {
    Gen_Error(0, "fatal: insufficient memory");
    return 0;
  }
  strcpy(mesh->eb_names[0], "hypergraph");

  /* allocate the element structure array */
  mesh->elements = (ELEM_INFO_PTR) malloc (mesh->elem_array_len 
                                         * sizeof(ELEM_INFO));
  if (!(mesh->elements)) {
    Gen_Error(0, "fatal: insufficient memory");
    return 0;
  }

  /*
   * Write the element structure with the vertices and weights
   */
  for (i = 0; i < mesh->elem_array_len; i++) {
    initialize_element(&(mesh->elements[i]));
    if (i < mesh->num_elems){
      mesh->elements[i].globalID = myVtxNum[i];
      mesh->elements[i].my_part  = Proc;
      for (j=0; j<vtxWDim; j++){
        mesh->elements[i].cpu_wgt[j] = myVtxWgts[i*vtxWDim + j];
      }
    }
  }

  safe_free((void **) &myVtxWgts);
  safe_free((void **) &myVtxNum);

 if (Debug_Driver > 3)
   print_distributed_mesh(Proc, Num_Proc, mesh);

  DEBUG_TRACE_END(Proc, yo);
  return 1;
}

/*****************************************************************************/
/* Read the "matrixmarket plus" file and create arrays of
 * my pins, vertices, vertex weights and edge weights.
 */
/*****************************************************************************/
static char *get_token(char *line, int which, int max);
static int find_gno(int *gnos, int gnolen, int id);
static int add_gid(int *gno, int gnolen, int *nextid, int id);
static char *first_char(char *buf, int max);
static char *next_line(char *buf, int max);
static int longest_line(char *buf);

static int process_mtxp_file(char *filebuf, int fsize, 
  int nprocs, int myrank,
  int *nGlobalEdges, int *nGlobalVtxs, int *vtxWDim, int *edgeWDim,
  int *nMyPins, int **myPinI, int **myPinJ,
  int *nMyVtx, int **myVtxNum, float **myVtxWgts,
  int *nMyEdgeWgts, int **myEdgeNum, float **myEdgeWgts)
{
int nedges, nvtxs, npins, vdim, edim, numew;
int countMyVtxs, countMyEdges, countMyPins;
int proc, mine, nextpin, linemax, rc;
int eid, vid, i, j;
int nexte, nextv;
float procf;
int *egno, *vgno, *myi, *myj, *myvno, *myeno;
char *line, *pinsBuf, *vwgtsBuf, *ewgtsBuf, *token;
float *myvwgt, *myewgt;
char cmesg[160];

  *nGlobalEdges = *nGlobalVtxs = *nMyPins = 0;
  *nMyVtx = *nMyEdgeWgts = *vtxWDim = *edgeWDim = 0;
  *myPinI = *myPinJ = *myVtxNum = *myEdgeNum = NULL;
  *myVtxWgts = *myEdgeWgts = NULL;

  linemax = longest_line(filebuf);
  line = first_char(filebuf, fsize);

  if (line && (*line == '%')){
    line = next_line(filebuf, fsize);  /* skip comments */
  }

  if (!line){
    sprintf(cmesg, "Truncated file\n");
    Gen_Error(0, cmesg);
    return 0;
  }

  rc = sscanf(line, "%d %d %d %d %d %d", &nedges, &nvtxs, &npins,
            &vdim, &numew, &edim);

  if (rc != 6){
    snprintf(cmesg, 160,"%s\nFirst line should have 6 values in it\n",line);
    Gen_Error(0, cmesg);
    return 0;
  }

  if (!nedges || !nvtxs){
    return 1;
  }

  myvno = myeno = myi = myj = NULL;
  myewgt = myvwgt = NULL;

  /* Read through the pins, vertex weights and edge weights.
   * Accumulate all vertex and edge IDs, and map these to
   * consecutive global numbers beginning with zero.
   *
   * Also count my pins, my vertices, and the number of edges
   * for which I provide weights.
   */

  countMyPins = 0;
  countMyVtxs = 0;
  countMyEdges = 0;
  nexte = nextv = 0;

  egno = (int *)malloc(sizeof(int) * nedges);
  vgno = (int *)malloc(sizeof(int) * nvtxs);
  if (!egno || !vgno){
    sprintf(cmesg, "memory allocation\n");
    Gen_Error(0, cmesg);
    goto failure;
  }

  for (i=0; i<npins; i++){           /* PINS */
    line = next_line(line, fsize);

    if (!line){  
      if (myrank == 0) printf("File is truncated in pins\n");
      goto failure;
    }

    if (i == 0) pinsBuf = line;

    rc = sscanf(line, "%d %d %f", &eid, &vid, &procf);
    if (rc != 3){
      snprintf(cmesg,160,"%s\nlooking for \"edge vertex process\"\n",line);
      Gen_Error(0, cmesg);
      goto failure;
    }
    mine = (((int)procf % nprocs) == myrank);

    rc = add_gid(egno, nedges, &nexte, eid);
    if (rc >= 0){
      rc = add_gid(vgno, nvtxs, &nextv, vid);
    }

    if (rc < 0){
      sprintf(cmesg,"File has more gids than totals at top indicate\n");
      Gen_Error(0, cmesg);
      goto failure;
    }

    if (mine){
      countMyPins++;
    }
  }

  for (i=0; i<nvtxs; i++){        /* VERTICES and possibly WEIGHTS */
    line = next_line(line, fsize);

    if (!line){
      sprintf(cmesg,"File is truncated at vertex weights\n");
      Gen_Error(0, cmesg);
      goto failure;
    }

    if (i == 0) vwgtsBuf = line;
    rc = sscanf(line, "%d %d", &proc, &vid);
    if (rc != 2){
      snprintf(cmesg,160,
      "%s\nlooking for \"process vertex {optional weights}\"\n",line);
      Gen_Error(0, cmesg);
      goto failure;
    }

    rc = add_gid(vgno, nvtxs, &nextv, vid);

    if (rc < 0){
      sprintf(cmesg,"File has more than %d vertices\n",nvtxs);
      Gen_Error(0, cmesg);
      goto failure;
    }

    if ((proc % nprocs) == myrank){
      countMyVtxs++;
    }
  }

  if (numew > 0){                      /* HYPEREDGE WEIGHTS */
    for (i=0; i<numew; i++){
      line = next_line(line, fsize);

      if (!line){  /* error */
        sprintf(cmesg,"File is truncated at edge weights\n");
        Gen_Error(0, cmesg);
        goto failure;
      }

      if (i == 0) ewgtsBuf = line;

      rc = sscanf(line, "%d %d", &proc, &eid);
      if (rc != 2){
        snprintf(cmesg,160,
        "%s\nlooking for \"process edge {optional weights}\"\n",line);
        Gen_Error(0, cmesg);
        goto failure;
      }
      rc = add_gid(egno, nedges, &nexte, eid);

      if (rc < 0){
        sprintf(cmesg,"File has more than %d edge IDs\n",nedges);
        Gen_Error(0, cmesg);
        goto failure;
      }

      if ((proc % nprocs) == myrank){
        countMyEdges++;
      }
    }
  }

  if (countMyPins > 0){
    myi = (int *)malloc(sizeof(int) * countMyPins);
    myj = (int *)malloc(sizeof(int) * countMyPins);
    if (!myi || !myj){
      sprintf(cmesg,"memory allocation\n");
      Gen_Error(0, cmesg);
      goto failure;
    }
    nextpin = 0;

    line = pinsBuf;
    for (i=0; i<npins; i++){
      sscanf(line, "%d %d %f", &eid, &vid, &procf);
      mine = (((int)procf % nprocs) == myrank);

      if (mine){
        myi[nextpin] = find_gno(egno, nedges, eid);
        myj[nextpin] = find_gno(vgno, nvtxs, vid);
        nextpin++;
      }
      line = next_line(line, fsize);
    }
  }

  if (countMyVtxs > 0){
    myvno = (int *)malloc(sizeof(int) * countMyVtxs);
    if (vdim > 0){
      myvwgt = (float *)malloc(sizeof(float) * countMyVtxs * vdim);
    }
    if (!myvno || (vdim && !myvwgt)){
      sprintf(cmesg,"memory allocation\n");
      Gen_Error(0, cmesg);
      goto failure;
    }
    line = vwgtsBuf;
    nextv = 0;

    for (i=0; i<nvtxs; i++){
      sscanf(line, "%d %d", &proc, &vid);
      if ((proc % nprocs) == myrank){
        myvno[nextv] = find_gno(vgno, nvtxs, vid); 
        for (j=0; j<vdim; j++){
          token = get_token(line, 2 + j, linemax);
          if (!token){
            snprintf(cmesg,160,"%s\nCan't find %d vertex weights\n",line,vdim);
            Gen_Error(0, cmesg);
            goto failure;
          }
          myvwgt[nextv*vdim + j] = (float)atof(token);
        }
        nextv++;
      }
      line = next_line(line, fsize);
    }
  }

  if (countMyEdges > 0){
    myeno = (int *)malloc(sizeof(int) * countMyEdges);
    myewgt = (float *)malloc(sizeof(float) * countMyEdges * edim);
    if (!myeno || !myewgt){
      sprintf(cmesg,"memory allocation\n");
      Gen_Error(0, cmesg);
      goto failure;
    }
    line = ewgtsBuf;
    nexte = 0;

    for (i=0; i<numew; i++){
      sscanf(line, "%d %d", &proc, &eid);
      if ((proc % nprocs) == myrank){
        myeno[nexte] = find_gno(egno, nedges, eid); 
        for (j=0; j<edim; j++){
          token = get_token(line, 2 + j, linemax);
          if (!token){
            snprintf(cmesg,160,"%s\nCan't find %d edge weights\n",line,edim);
            Gen_Error(0, cmesg);
            goto failure;
          }
          myewgt[nexte*edim + j] = (float)atof(token);
        }
        nexte++;
      }
      line = next_line(line, fsize);
    }
  }

  rc = 1;   /* success */
  goto done;

failure:
  if (myvno) free(myvno);
  if (myvwgt) free(myvwgt);
  if (myeno) free(myeno);
  if (myewgt) free(myewgt);
  if (myi) free(myi);
  if (myj) free(myj);
  nedges = nvtxs = vdim = edim = 0;
  countMyPins = countMyVtxs = countMyEdges = 0;
  rc = 0;

done:

  if (egno) free(egno);
  if (vgno) free(vgno);

  *nGlobalEdges = nedges;
  *nGlobalVtxs = nvtxs;
  *vtxWDim = vdim;
  *edgeWDim = edim;

  *nMyPins = countMyPins;
  *myPinI  = myi;
  *myPinJ  = myj;

  *nMyVtx = countMyVtxs;
  *myVtxNum = myvno;
  *myVtxWgts = myvwgt;

  *nMyEdgeWgts = countMyEdges;
  *myEdgeNum = myeno;
  *myEdgeWgts = myewgt;

  return rc;
}
static int create_edge_lists(int nMyPins, int *myPinI, int *myPinJ, 
      int *numHEdges, int **edgeGno, int **edgeIdx, int **pinGno)
{
int nedges, i, lid;
int *eids, *pins, *count, *start;

  *numHEdges = 0;
  *edgeGno = *edgeIdx = *pinGno = NULL;

  if (nMyPins == 0){
    *edgeIdx = (int *)malloc(sizeof(int));
    **edgeIdx = 0;
    return 1;
  }

  /* create list of each edge ID, and count of each */

  eids = (int *)malloc(sizeof(int) * nMyPins);
  count = (int *)calloc(sizeof(int), nMyPins);

  if (!eids || !count){
    safe_free((void**)&eids);
    safe_free((void**)&count);
    Gen_Error(0, "memory allocation");
    return 0;
  }
  nedges = 0;

  for (i = 0; i<nMyPins; i++){
    lid = add_gid(eids, nMyPins, &nedges, myPinI[i]);
    count[lid]++;
  }

  start = (int *)malloc(sizeof(int) * (nedges+1));
  pins = (int *)malloc(sizeof(int) * nMyPins);
  if (!start|| !pins){
    safe_free((void**)&eids);
    safe_free((void**)&count);
    safe_free((void**)&start);
    safe_free((void**)&pins);
    Gen_Error(0, "memory allocation");
    return 0;
  }
  start[0] = 0;
  for (i=0; i<nedges; i++){
    start[i+1] = start[i] + count[i]; 
    count[i] = 0;
  }

  for (i=0; i<nMyPins; i++){
    lid = find_gno(eids, nedges, myPinI[i]);
    pins[start[lid] + count[lid]] = myPinJ[i];
    count[lid]++;
  }

  free(count);

  *numHEdges = nedges;
  *edgeGno   = eids;
  *edgeIdx   = start;
  *pinGno    = pins;

  return 1;
}
static char *get_token(char *line, int which, int max)
{
char *c;
int l, i;

  /* Return a pointer to the which'th white space separated
   * token in the line.  which is 0 for the first token.
   */

  c = line;
  l = 0;

  while (isblank(*c) && (l < max)){ c++; l++; } /* skip initial spaces */

  if ((l>=max) || !*c || (*c == '\n')) return NULL;

  if (which == 0) return c;

  for (i=0; i < which; i++){
    while (isgraph(*c) && (l < max)){ c++; l++;} /* skip token */
    if ((l>=max) || !*c || (*c == '\n')) return NULL;
    while (isblank(*c) && (l < max)){ c++; l++;} /* skip space */
    if ((l>=max) || !*c || (*c == '\n')) return NULL;
  }

  return c;
}
static int find_gno(int *gnos, int gnolen, int id)
{
int i;

  for (i=0; i< gnolen; i++){
    if (gnos[i] == id){
      return i;
    }
  }
  return -1; 
}
static int add_gid(int *gno, int gnolen, int *nextid, int id)
{
int i;
int last = *nextid;

  if (last > gnolen) return -1;  /* no room in list */

  for (i=0; i< last; i++){
    if (gno[i] == id){
      return i;  /* already on the list */
    }
  }
  if (last == gnolen) return -1;  /* no room in list */

  gno[last] = id;

  *nextid = last + 1;

  return last;
}
static char *first_char(char *buf, int max)
{
char *c = buf;
int sanity = 0;

  while (*c && isspace(*c) && (sanity++ < max)) c++;

  if ((sanity > max) || !*c) return NULL;

  return c;
}
static char *next_line(char *buf, int max)
{
char *c = buf;
int sanity = 0;
int first=1;

  /* return pointer to the next non-comment */

  while (first || (*c == '%')){
    while (*c && (*c != '\n') && (sanity++ < max)) c++;

    if ((sanity >= max) || (*c == 0)){
      return NULL;
    }
    first = 0;
    c++;
  }

  return c;
}
static int longest_line(char *buf)
{
int maxline, linecount;
char *c;

  linecount = maxline = 0;
  c = buf;

  while (*c){
    while (*c && *c++ != '\n') linecount++;
    if (linecount > maxline){
      maxline = linecount;
    }
    linecount = 0;
  }
  return maxline;
}

/*****************************************************************************/

#ifdef __cplusplus
} /* closing bracket for extern "C" */
#endif

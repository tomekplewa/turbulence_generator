// *******************************************************************************
// *************************** Turbulence generator ******************************
// *******************************************************************************
//
// This header file contains functions and data structures used by the turbulence
// generator. The main input (parameter) file is 'turbulence_generator.inp'.
//
// Please see Federrath et al. (2010, A&A 512, A81) for details and cite :)
//
// DESCRIPTION
//
//  Contains functions to compute the time-dependent physical turbulent vector
//  field used to drive or initialise turbulence in hydro codes such as AREPO,
//  FLASH, GADGET, PHANTOM, PLUTO, QUOKKA.
//  The driving sequence follows an Ornstein-Uhlenbeck (OU) process.
//
//  For example applications see Federrath et al. (2008, ApJ 688, L79);
//  Federrath et al. (2010, A&A 512, A81); Federrath (2013, MNRAS 436, 1245);
//  Federrath et al. (2021, Nature Astronomy 5, 365)
//
// AUTHOR: Christoph Federrath, 2008-2022
//
// *******************************************************************************

#ifndef TURBULENCE_GENERATOR_H
#define TURBULENCE_GENERATOR_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <float.h>
#include <string.h>
#include <math.h>

#define MIN(a,b) ((a) < (b) ? a : b)
#define MAX(a,b) ((a) > (b) ? a : b)

// forward functions
int TurbGen_init_turbulence_generator(char * parameter_file, const int PE);
static inline bool TurbGen_check_for_update(double time);
static inline void TurbGen_get_turb_vector(const double x, const double y, const double z, double * vx, double * vy, double * vz);
int TurbGen_init_modes(void);
void TurbGen_print_info(void);
int TurbGen_read_from_parameter_file(char * search, char type, void * ret);
void TurbGen_OU_noise_init(void);
void TurbGen_OU_noise_update(void);
void TurbGen_get_decomposition_coeffs(void);
double TurbGen_grn(void);
double TurbGen_ran1s(int *idum);
double TurbGen_ran2(int* idum);
void TurbGen_printf(char * format, ...);

// global constants
static const int tgd_max_n_modes = 100000;
enum {X, Y, Z};

// global turbulence generator data structure
struct TurbGenData {
  bool debug;
  int PE; // MPI task for printf purposes, if provided
  char * parameter_file; // parameter file for controlling TurbGen
  int n_modes; // number of modes
  double OUvar; // OU variance corresponding to decay time and energy input rate
  double mode[3][tgd_max_n_modes], aka[3][tgd_max_n_modes], akb[3][tgd_max_n_modes]; // modes arrays
  double OUphases[6*tgd_max_n_modes]; // phases
  double ampl[tgd_max_n_modes]; // amplitudes
  // parameters, etc
  int ndim; // number of spatial dimensions
  int random_seed, seed; // 'random seed' is the orignial starting seed, then 'seed' gets updated by call to RNG
  int spect_form; // spectral form (Band, Parabola, Power Law)
  int nsteps_per_turnover_time; // number of driving patterns per turnover time
  int step; // internal OU step number
  double xmin, xmax, ymin, ymax, zmin, zmax, Lx; // bounding box
  double decay; // auto-correlation timescale
  double dt; // time step for OU update and for generating driving patterns
  double energy; // driving energy
  double stir_min, stir_max; // min and max wavnumber for driving
  double sol_weight, sol_weight_norm; // weight for decomposition into solenoidal and compressive modes
  double velocity; // velocity dispersion
  double power_law_exp; // for power-law spectrum: exponent
  double angles_exp; // for power-law spectrum: angles exponent for sparse sampling
};
static struct TurbGenData tgd; // create TurbGenData structure 'tgd'


// ******************************************************
int TurbGen_init_turbulence_generator(char * parameter_file, const int PE) {
// ******************************************************
// Initialise the turbulence generator and all relevant
// internal data structures by reading from 'parameter_file'.
// If called from an MPI-parallelised code, supply its MPI rank in PE, otherwise provide PE=0
// ******************************************************
  // set parameter file
  tgd.parameter_file = parameter_file;
  tgd.PE = PE;
  // check if parameter file is present
  FILE * fp = fopen(tgd.parameter_file, "r");
  if (fp == NULL) {
    printf("TurbGen: ERROR: cannot access parameter file '%s'.\n", tgd.parameter_file); exit(-1); 
  } else {
    fclose(fp);
  }
  // read parameter file
  double k_driv, k_min, k_max, energy_coeff;
  TurbGen_read_from_parameter_file("ndim", 'i', &tgd.ndim);
  TurbGen_read_from_parameter_file("xmin", 'd', &tgd.xmin);
  TurbGen_read_from_parameter_file("xmax", 'd', &tgd.xmax);
  TurbGen_read_from_parameter_file("ymin", 'd', &tgd.ymin);
  TurbGen_read_from_parameter_file("ymax", 'd', &tgd.ymax);
  TurbGen_read_from_parameter_file("zmin", 'd', &tgd.zmin);
  TurbGen_read_from_parameter_file("zmax", 'd', &tgd.zmax);
  TurbGen_read_from_parameter_file("velocity", 'd', &tgd.velocity);
  TurbGen_read_from_parameter_file("k_driv", 'd', &k_driv);
  TurbGen_read_from_parameter_file("k_min", 'd', &k_min);
  TurbGen_read_from_parameter_file("k_max", 'd', &k_max);
  TurbGen_read_from_parameter_file("sol_weight", 'd', &tgd.sol_weight);
  TurbGen_read_from_parameter_file("spect_form", 'i', &tgd.spect_form);
  TurbGen_read_from_parameter_file("power_law_exp", 'd', &tgd.power_law_exp);
  TurbGen_read_from_parameter_file("angles_exp", 'd', &tgd.angles_exp);
  TurbGen_read_from_parameter_file("energy_coeff", 'd', &energy_coeff);
  TurbGen_read_from_parameter_file("random_seed", 'i', &tgd.random_seed);
  TurbGen_read_from_parameter_file("nsteps_per_turnover_time", 'i', &tgd.nsteps_per_turnover_time);
  // define derived physical quantities
  tgd.Lx = tgd.xmax-tgd.xmin;                           // Length of box in x; used for normalisations below
  tgd.stir_min = (k_min-DBL_EPSILON) * 2*M_PI / tgd.Lx;  // Minimum driving wavenumber <~  k_min * 2pi / Lx
  tgd.stir_max = (k_max+DBL_EPSILON) * 2*M_PI / tgd.Lx;  // Maximum driving wavenumber >~  k_max * 2pi / Lx
  tgd.decay = tgd.Lx / k_driv / tgd.velocity;           // Auto-correlation time, t_turb = Lx / k_driv / velocity;
                                                        // aka turbulent turnover (crossing) time; note that k_driv is in units of 2pi/Lx
  tgd.energy = energy_coeff * 
                  pow(tgd.velocity,3.0) / tgd.Lx;       // Energy input rate => driving amplitude ~ sqrt(energy/decay)
                                                        // Note that energy input rate ~ velocity^3 * L_box^-1
                                                        // energy_coeff needs to be adjusted to approach actual target velocity dispersion
  tgd.OUvar = sqrt(tgd.energy/tgd.decay);               // Ornstein-Uhlenbeck variance
  tgd.dt = tgd.decay / tgd.nsteps_per_turnover_time;    // time step in OU process and for creating new driving pattern
  tgd.step = -1;                                         // set internal OU step to 0 for start-up
  tgd.seed = tgd.random_seed;                           // copy orignial seed into local seed; local seeds gets updated everytime RNG is called
  // this makes the rms of the turbulent field constant, irrespective of the solenoidal weight
  if (tgd.ndim == 3) tgd.sol_weight_norm = sqrt(3.0/3.0)*sqrt(3.0)*1.0/sqrt(1.0-2.0*tgd.sol_weight+3.0*pow(tgd.sol_weight,2.0));
  if (tgd.ndim == 2) tgd.sol_weight_norm = sqrt(3.0/2.0)*sqrt(3.0)*1.0/sqrt(1.0-2.0*tgd.sol_weight+2.0*pow(tgd.sol_weight,2.0));
  if (tgd.ndim == 1) tgd.sol_weight_norm = sqrt(3.0/1.0)*sqrt(3.0)*1.0/sqrt(1.0-2.0*tgd.sol_weight+1.0*pow(tgd.sol_weight,2.0));
  TurbGen_printf("===============================================================================\n");
  // initialise modes
  TurbGen_init_modes();
  // initialise Ornstein-Uhlenbeck sequence
  TurbGen_OU_noise_init();
  // calculate solenoidal and compressive coefficients (aka, akb) from OUphases
  TurbGen_get_decomposition_coeffs();
  // print info
  TurbGen_print_info();
  TurbGen_printf("===============================================================================\n");
  return 0;
} // TurbGen_init_turbulence_generator


// ******************************************************
static inline bool TurbGen_check_for_update(double time) {
// ******************************************************
// Update driving pattern based on input 'time'.
// If it is 'time' to update the pattern, call OU noise update
// and update the decomposition coefficients; otherwise, simply return.
// ******************************************************
  int step_requested = floor(time / tgd.dt); // requested OU step number based on input current 'time'
  if (tgd.debug) TurbGen_printf("step_requested = %i\n", step_requested);
  if (step_requested <= tgd.step) {
    if (tgd.debug) TurbGen_printf("no update of pattern...returning.\n");
    return false; // no update (yet) -> return false, i.e., no change to driving pattern
  }
  // update OU vector
  for (int is = tgd.step; is < step_requested; is++) {
    TurbGen_OU_noise_update(); // this seeks to the requested OU state (updates OUphases)
    if (tgd.debug) TurbGen_printf("step = %i, time = %f\n", tgd.step, tgd.step*tgd.dt); // print some info
  }
  TurbGen_get_decomposition_coeffs(); // calculate solenoidal and compressive coefficients (aka, akb) from OUphases
  double time_gen = tgd.step * tgd.dt;
  TurbGen_printf("Generated new turbulence driving pattern: #%6i, time = %f, time/t_turb = %f\n", tgd.step, time_gen, time_gen/tgd.decay); // print some info
  return true; // we just updated the driving pattern
} // TurbGen_check_for_update


// ******************************************************
static inline void TurbGen_get_turb_vector(const double x, const double y, const double z, double * vx, double * vy, double * vz) {
// ******************************************************
// Compute physical turbulent vector vx, vy, vz at position x, y, z
// from loop over all turbulent modes; return into vx, vy, vz
// ******************************************************
  double sinx[tgd_max_n_modes], cosx[tgd_max_n_modes];
  double siny[tgd_max_n_modes], cosy[tgd_max_n_modes];
  double sinz[tgd_max_n_modes], cosz[tgd_max_n_modes];
  double real, imag;
  // pre-compute some trigonometry
  for (int m = 0; m < tgd.n_modes; m++) {
    sinx[m] = sin(tgd.mode[X][m]*x);
    cosx[m] = cos(tgd.mode[X][m]*x);
    siny[m] = sin(tgd.mode[Y][m]*y);
    cosy[m] = cos(tgd.mode[Y][m]*y);
    sinz[m] = sin(tgd.mode[Z][m]*z);
    cosz[m] = cos(tgd.mode[Z][m]*z);
  }
  // init return vector with zero
  *vx = 0.0; *vy = 0.0; *vz = 0.0;
  // loop over all modes
  for (int m = 0; m < tgd.n_modes; m++) {
    // these are the real and imaginary parts, respectively, of
    //  e^{ i \vec{k} \cdot \vec{x} } = cos(kx*x + ky*y + kz*z) + i sin(kx*x + ky*y + kz*z)
    real = ( cosx[m]*cosy[m] - sinx[m]*siny[m] ) * cosz[m] - ( sinx[m]*cosy[m] + cosx[m]*siny[m] ) * sinz[m];
    imag = cosx[m] * ( cosy[m]*sinz[m] + siny[m]*cosz[m] ) + sinx[m] * ( cosy[m]*cosz[m] - siny[m]*sinz[m] );
    // return vector for this position x, y, z
    *vx += 2.0*tgd.sol_weight_norm*tgd.ampl[m] * (tgd.aka[X][m]*real - tgd.akb[X][m]*imag);
    *vy += 2.0*tgd.sol_weight_norm*tgd.ampl[m] * (tgd.aka[Y][m]*real - tgd.akb[Y][m]*imag);
    *vz += 2.0*tgd.sol_weight_norm*tgd.ampl[m] * (tgd.aka[Z][m]*real - tgd.akb[Z][m]*imag);
  }
} // TurbGen_get_turb_vector


// ******************************************************
int TurbGen_init_modes(void) {
// ******************************************************
// initialise all turbulent modes information
// ******************************************************

  int ikxmin, ikxmax, ikymin, ikymax, ikzmin, ikzmax;
  int ikx, iky, ikz, tot_n_modes;
  double kx, ky, kz, k, kc, Lx, Ly, Lz, amplitude, parab_prefact;

  // applies in case of power law (spect_form == 2)
  int iang, nang, ik, ikmin, ikmax;
  double rand, phi, theta;

  // this is for spect_form = 1 (paraboloid) only
  // prefactor for amplitude normalistion to 1 at kc = 0.5*(stir_min+stir_max)
  parab_prefact = -4.0 / pow(tgd.stir_max-tgd.stir_min,2.0);

  // characteristic k for scaling the amplitude below
  kc = tgd.stir_min;
  if (tgd.spect_form == 1) kc = 0.5*(tgd.stir_min+tgd.stir_max);

  ikxmin = 0;
  ikymin = 0;
  ikzmin = 0;

  ikxmax = 256;
  ikymax = 0;
  ikzmax = 0;
  if (tgd.ndim > 1) ikymax = 256;
  if (tgd.ndim > 2) ikzmax = 256;

  Lx = tgd.xmax - tgd.xmin;
  Ly = tgd.ymax - tgd.ymin;
  Lz = tgd.zmax - tgd.zmin;

  // determine the number of required modes (in case of full sampling)
  tgd.n_modes = 0;
  for (ikx = ikxmin; ikx <= ikxmax; ikx++) {
    kx = 2*M_PI * ikx / Lx;
    for (iky = ikymin; iky <= ikymax; iky++) {
      ky = 2*M_PI * iky / Ly;
      for (ikz = ikzmin; ikz <= ikzmax; ikz++) {
        kz = 2*M_PI * ikz / Lz;
        k = sqrt( kx*kx + ky*ky + kz*kz );
        if ((k >= tgd.stir_min) && (k <= tgd.stir_max)) {
            tgd.n_modes++;
            if (tgd.ndim > 1) tgd.n_modes += 1;
            if (tgd.ndim > 2) tgd.n_modes += 2;
        }
      }
    }
  }
  tot_n_modes = tgd.n_modes;
  if (tgd.spect_form != 2) { // for Band and Parabola
    if (tot_n_modes + pow(2.0,tgd.ndim-1) > tgd_max_n_modes) {
      TurbGen_printf("init_stir:  n_modes = %i, maxmodes = %i", tgd.n_modes, tgd_max_n_modes);
      TurbGen_printf("Too many stirring modes");
      exit(-1);
    }
    TurbGen_printf("Generating %i driving modes...\n", tot_n_modes);
  }

  tgd.n_modes = -1; // reset and use as index

  // ===================================================================
  // === for band and parabolic spectrum, use the standard full sampling
  if (tgd.spect_form != 2) {

    // loop over all kx, ky, kz to generate driving modes
    for (ikx = ikxmin; ikx <= ikxmax; ikx++) {
      kx = 2*M_PI * ikx / Lx;
      for (iky = ikymin; iky <= ikymax; iky++) {
        ky = 2*M_PI * iky / Ly;
        for (ikz = ikzmin; ikz <= ikzmax; ikz++) {
          kz = 2*M_PI * ikz / Lz;

          k = sqrt( kx*kx + ky*ky + kz*kz );

          if ((k >= tgd.stir_min) && (k <= tgd.stir_max)) {

            if (tgd.spect_form == 0) amplitude = 1.0;                                   // Band
            if (tgd.spect_form == 1) amplitude = fabs(parab_prefact*pow(k-kc,2.0)+1.0); // Parabola

            // note: power spectrum ~ amplitude^2 (1D), amplitude^2 * 2pi k (2D), amplitude^2 * 4pi k^2 (3D) 
            amplitude = sqrt(amplitude) * pow(kc/k,(tgd.ndim-1)/2.0);

            tgd.n_modes++;
            tgd.ampl[tgd.n_modes] = amplitude;
            if (tgd.debug) TurbGen_printf("init_stir:  ampl[%i] = %f\n", tgd.n_modes, tgd.ampl[tgd.n_modes]);
            tgd.mode[X][tgd.n_modes] = kx;
            tgd.mode[Y][tgd.n_modes] = ky;
            tgd.mode[Z][tgd.n_modes] = kz;

            if (tgd.ndim > 1) {

              tgd.n_modes++;
              tgd.ampl[tgd.n_modes] = amplitude;
              if (tgd.debug) TurbGen_printf("init_stir:  ampl[%i] = %f\n", tgd.n_modes, tgd.ampl[tgd.n_modes]);
              tgd.mode[X][tgd.n_modes] =  kx;
              tgd.mode[Y][tgd.n_modes] = -ky;
              tgd.mode[Z][tgd.n_modes] =  kz;

            }

            if (tgd.ndim > 2) {

              tgd.n_modes++;
              tgd.ampl[tgd.n_modes] = amplitude;
              if (tgd.debug) TurbGen_printf("init_stir:  ampl[%i] = %f\n", tgd.n_modes, tgd.ampl[tgd.n_modes]);
              tgd.mode[X][tgd.n_modes] =  kx;
              tgd.mode[Y][tgd.n_modes] =  ky;
              tgd.mode[Z][tgd.n_modes] = -kz;

              tgd.n_modes++;
              tgd.ampl[tgd.n_modes] = amplitude;
              if (tgd.debug) TurbGen_printf("init_stir:  ampl[%i] = %f\n", tgd.n_modes, tgd.ampl[tgd.n_modes]);
              tgd.mode[X][tgd.n_modes] =  kx;
              tgd.mode[Y][tgd.n_modes] = -ky;
              tgd.mode[Z][tgd.n_modes] = -kz;

            }

            if ((tgd.n_modes+1) % 1000 == 0) TurbGen_printf(" ... %i of total %i modes generated...\n", tgd.n_modes+1, tot_n_modes);

          } // in k range
        } // ikz
      } // iky
    } // ikx
  } // spect_form != 2

  // ===============================================================================
  // === for power law, generate modes that are distributed randomly on the k-sphere
  // === with the number of angles growing ~ k^angles_exp
  if (tgd.spect_form == 2) {

    TurbGen_printf("There would be %i driving modes, if k-space were fully sampled (angles_exp = 2.0)...\n", tot_n_modes);
    TurbGen_printf("Here we are using angles_exp = %f\n", tgd.angles_exp);

    // initialize additional random numbers (uniformly distributed) to randomise angles
    int seed = -tgd.seed; // initialise Numerical Recipes rand gen (call with negative integer)
    rand = TurbGen_ran2(&seed);

    // loop between smallest and largest k
    ikmin = MAX(1, round(tgd.stir_min*Lx/(2*M_PI)));
    ikmax =        round(tgd.stir_max*Lx/(2*M_PI));

    TurbGen_printf("Generating driving modes within k = [%i, %i]\n", ikmin, ikmax);

    for (ik = ikmin; ik <= ikmax; ik++) {

      nang = pow(2.0,tgd.ndim) * ceil(pow((double)ik,tgd.angles_exp));
      TurbGen_printf("ik, number of angles = %i, %i\n", ik, nang);

      for (iang = 1; iang <= nang; iang++) {

        phi = 2*M_PI * TurbGen_ran2(&tgd.seed); // phi = [0,2pi] sample the whole sphere
        if (tgd.ndim == 1) {
          if (phi <  M_PI) phi = 0.0;
          if (phi >= M_PI) phi = M_PI;
        }
        theta = M_PI/2.0;
        if (tgd.ndim > 2) theta = acos(1.0 - 2.0*TurbGen_ran2(&tgd.seed)); // theta = [0,pi] sample the whole sphere

        if (tgd.debug) TurbGen_printf("entering: theta = %f, phi = %f\n", theta, phi);

        rand = ik + TurbGen_ran2(&tgd.seed) - 0.5;
        kx = 2*M_PI * round(rand*sin(theta)*cos(phi)) / Lx;
        ky = 0.0;
        if (tgd.ndim > 1) ky = 2*M_PI * round(rand*sin(theta)*sin(phi)) / Ly;
        kz = 0.0;
        if (tgd.ndim > 2) kz = 2*M_PI * round(rand*cos(theta)         ) / Lz;

        k = sqrt( kx*kx + ky*ky + kz*kz );

        if ((k >= tgd.stir_min) && (k <= tgd.stir_max)) {

          if (tgd.n_modes + pow(2.0,tgd.ndim-1) > tgd_max_n_modes) {
            TurbGen_printf("init_stir:  n_modes = %i, maxmodes = %i", tgd.n_modes, tgd_max_n_modes);
            TurbGen_printf("Too many stirring modes");
            exit(-1);
          }

          amplitude = pow(k/kc,tgd.power_law_exp); // Power law

          // note: power spectrum ~ amplitude^2 (1D), amplitude^2 * 2pi k (2D), amplitude^2 * 4pi k^2 (3D)
          // ...and correct for the number of angles sampled relative to the full sampling (k^2 per k-shell in 3D)
          amplitude = sqrt( amplitude * pow((double)ik,tgd.ndim-1) / (double)(nang) * 4.0*sqrt(3.0) ) * pow(kc/k,(tgd.ndim-1)/2.0);

          tgd.n_modes++;
          tgd.ampl[tgd.n_modes] = amplitude;
          if (tgd.debug) TurbGen_printf("init_stir:  ampl[%i] = %f\n", tgd.n_modes, tgd.ampl[tgd.n_modes]);
          tgd.mode[X][tgd.n_modes] = kx;
          tgd.mode[Y][tgd.n_modes] = ky;
          tgd.mode[Z][tgd.n_modes] = kz;

          if ((tgd.n_modes+1) % 1000 == 0) TurbGen_printf(" ... %i modes generated...\n", tgd.n_modes+1);

        } // in k range

      } // loop over angles
    } // loop over k
  } // spect_form == 2

  tgd.n_modes++; // increase by 1 because of indexing use above
  return 0;
} // TurbGen_init_modes


// ******************************************************
void TurbGen_print_info(void) {
// ******************************************************
  TurbGen_printf("Initialized %i modes for turbulence based on parameter file '%s'.\n", tgd.n_modes, tgd.parameter_file);
  if (tgd.spect_form == 0) TurbGen_printf(" spectral form                                       = %i (Band)\n", tgd.spect_form);
  if (tgd.spect_form == 1) TurbGen_printf(" spectral form                                       = %i (Parabola)\n", tgd.spect_form);
  if (tgd.spect_form == 2) TurbGen_printf(" spectral form                                       = %i (Power Law)\n", tgd.spect_form);
  if (tgd.spect_form == 2) TurbGen_printf(" power-law exponent                                  = %f\n", tgd.power_law_exp);
  if (tgd.spect_form == 2) TurbGen_printf(" power-law angles sampling exponent                  = %f\n", tgd.angles_exp);
  TurbGen_printf(" box size Lx                                         = %f\n", tgd.Lx);
  TurbGen_printf(" turbulent dispersion                                = %f\n", tgd.velocity);
  TurbGen_printf(" auto-correlation time                               = %f\n", tgd.decay);
  TurbGen_printf("  -> characteristic turbulent wavenumber (in 2pi/Lx) = %f\n", tgd.Lx / tgd.velocity / tgd.decay);
  TurbGen_printf(" minimum wavenumber (in 2pi/Lx)                      = %f\n", tgd.stir_min / (2*M_PI) * tgd.Lx);
  TurbGen_printf(" maximum wavenumber (in 2pi/Lx)                      = %f\n", tgd.stir_max / (2*M_PI) * tgd.Lx);
  TurbGen_printf(" driving energy (injection rate)                     = %f\n", tgd.energy);
  TurbGen_printf("  -> energy coefficient (energy / velocity^3 * Lx)   = %f\n", tgd.energy / pow(tgd.velocity,3.0) * tgd.Lx);
  TurbGen_printf(" solenoidal weight (0.0: comp, 0.5: mix, 1.0: sol)   = %f\n", tgd.sol_weight);
  TurbGen_printf("  -> solenoidal weight norm (set based on Ndim = %i)  = %f\n", tgd.ndim, tgd.sol_weight_norm);
  TurbGen_printf(" random seed                                         = %i\n", tgd.random_seed);
} // TurbGen_print_info


// ******************************************************
int TurbGen_read_from_parameter_file(char * search, char type, void * ret) {
// ******************************************************
// parse each line in turbulence generator 'parameter_file' and search for 'search'
// at the beginning of each line; if 'search' is found return double of value after '='
// type: 'i' for int, 'd' for double return type void *ret
// ******************************************************
  FILE * fp;
  char * line = NULL;
  size_t len = 0;
  ssize_t read;
  fp = fopen(tgd.parameter_file, "r");
  if (fp == NULL) { printf("TurbGen: ERROR: could not open parameter file '%s'\n", tgd.parameter_file); exit(-1); }
  bool found = false;
  while ((read = getline(&line, &len, fp)) != -1) {
    if (strncmp(line, search, strlen(search)) == 0) {
      if (tgd.debug) TurbGen_printf("line = '%s'\n", line);
      char * substr1 = strstr(line, "="); // extract everything after (and including) '='
      if (tgd.debug) TurbGen_printf("substr1 = '%s'\n", substr1);
      char * substr2 = strstr(substr1, "!"); // deal with comment '! ...'
      char * substr3 = strstr(substr1, "#"); // deal with comment '# ...'
      int end_index = strlen(substr1);
      if ((substr2 != NULL) && (substr3 != NULL)) { // if comment is present, reduce end_index
        end_index -= MAX(strlen(substr2),strlen(substr3));
      } else { // if comment is present, reduce end_index
        if (substr2 != NULL) end_index -= strlen(substr2);
        if (substr3 != NULL) end_index -= strlen(substr3);
      }
      char dest[100]; memset(dest, '\0', sizeof(dest));
      strncpy(dest, substr1+1, end_index-1);
      if (tgd.debug) TurbGen_printf("dest = '%s'\n", dest);
      if (type == 'i') *(int*)(ret) = atoi(dest);
      if (type == 'd') *(double*)(ret) = atof(dest);
      found = true;
    }
    if (found) break;
  }
  fclose(fp);
  if (line) free(line);
  if (found) return 0; else {
    printf("TurbGen: ERROR: requested parameter '%s' not found in file '%s'\n", search, tgd.parameter_file);
    exit(-1);
  }
} // TurbGen_read_from_parameter_file


// ******************************************************
void TurbGen_OU_noise_init(void) {
// ******************************************************
// initialize pseudo random sequence for the Ornstein-Uhlenbeck (OU) process
// ******************************************************
  for (int i = 0; i < 6*tgd.n_modes; i++) tgd.OUphases[i] = tgd.OUvar * TurbGen_grn();
} // TurbGen_OU_noise_init


// ******************************************************
void TurbGen_OU_noise_update(void) {
// ******************************************************
// update Ornstein-Uhlenbeck sequence
//
// The sequence x_n is a Markov process that takes the previous value,
// weights by an exponential damping factor with a given correlation
// time 'ts', and drives by adding a Gaussian random variable with
// variance 'variance', weighted by a second damping factor, also
// with correlation time 'ts'. For a timestep of dt, this sequence
// can be written as
//
//     x_n+1 = f x_n + sigma * sqrt (1 - f**2) z_n,
//
// where f = exp (-dt / ts), z_n is a Gaussian random variable drawn
// from a Gaussian distribution with unit variance, and sigma is the
// desired variance of the OU sequence.
//
// The resulting sequence should satisfy the properties of zero mean,
// and stationarity (independent of portion of sequence) RMS equal to
// 'variance'. Its power spectrum in the time domain can vary from
// white noise to "brown" noise (P (f) = const. to 1 / f^2).
//
// References:
//   Bartosch (2001)
//   Eswaran & Pope (1988)
//   Schmidt et al. (2009)
//   Federrath et al. (2010, A&A 512, A81)
//
// ARGUMENTS
//
//   vector :       vector to be updated
//   vectorlength : length of vector to be updated
//   variance :     variance of the distribution
//   dt :           timestep
//   ts :           autocorrelation time
//
// ******************************************************
  const double damping_factor = exp(-tgd.dt/tgd.decay);
  for (int i = 0; i < 6*tgd.n_modes; i++) {
    tgd.OUphases[i] = tgd.OUphases[i] * damping_factor + 
                  sqrt(1.0 - damping_factor*damping_factor) * tgd.OUvar * TurbGen_grn();
  }
  tgd.step++; // update internal OU step number
} // TurbGen_OU_noise_update


// ******************************************************
void TurbGen_get_decomposition_coeffs(void) {
// ******************************************************
// This routine applies the projection operator based on the OU phases.
// ******************************************************
  double ka, kb, kk, diva, divb, curla, curlb;
  for (int i = 0; i < tgd.n_modes; i++) {
    ka = 0.0;
    kb = 0.0;
    kk = 0.0;
    for (int j = 0; j < tgd.ndim; j++) {
      kk = kk + tgd.mode[j][i]*tgd.mode[j][i];
      ka = ka + tgd.mode[j][i]*tgd.OUphases[6*i+2*j+1];
      kb = kb + tgd.mode[j][i]*tgd.OUphases[6*i+2*j+0];
    }
    for (int j = 0; j < tgd.ndim; j++) {
      diva  = tgd.mode[j][i]*ka/kk;
      divb  = tgd.mode[j][i]*kb/kk;
      curla = tgd.OUphases[6*i+2*j+0] - divb;
      curlb = tgd.OUphases[6*i+2*j+1] - diva;
      tgd.aka[j][i] = tgd.sol_weight*curla+(1.0-tgd.sol_weight)*divb;
      tgd.akb[j][i] = tgd.sol_weight*curlb+(1.0-tgd.sol_weight)*diva;
      // purely compressive
      // tgd.aka[j][i] = tgd.mode[j][i]*kb/kk;
      // tgd.akb[j][i] = tgd.mode[j][i]*ka/kk;
      // purely solenoidal
      // tgd.aka[j][i] = R - tgd.mode[j][i]*kb/kk;
      // tgd.akb[j][i] = I - tgd.mode[j][i]*ka/kk;
      if (tgd.debug) {
        TurbGen_printf("mode(dim=%1i, mode=%3i) = %12.6f\n", j, i, tgd.mode[j][i]);
        TurbGen_printf("aka (dim=%1i, mode=%3i) = %12.6f\n", j, i, tgd.aka [j][i]);
        TurbGen_printf("akb (dim=%1i, mode=%3i) = %12.6f\n", j, i, tgd.akb [j][i]);
        TurbGen_printf("ampl(dim=%1i, mode=%3i) = %12.6f\n", j, i, tgd.ampl   [i]);
      }
    }
  }
} // TurbGen_get_decomposition_coeffs


// ******************************************************
double TurbGen_grn(void) {
// ******************************************************
//  get random number; draws a number randomly from a Gaussian distribution
//  with the standard uniform distribution function "random_number"
//  using the Box-Muller transformation in polar coordinates. The
//  resulting Gaussian has unit variance.
// ******************************************************
  double r1 = TurbGen_ran1s(&tgd.seed);
  double r2 = TurbGen_ran1s(&tgd.seed);
  double g1 = sqrt(2.0*log(1.0/r1))*cos(2*M_PI*r2);
  return g1;
} // TurbGen_grn


// ************** Numerical recipes ran1s ***************
double TurbGen_ran1s(int * idum) {
// ******************************************************
  static const int IA=16807, IM=2147483647, IQ=127773, IR=2836, NTAB=32;
  static const double AM=1.0/IM, EPS=1.2e-7, RNMX=1.0-EPS;
  if (*idum <= 0) *idum = MAX(-*idum, 1);
  int k = *idum/IQ;
  *idum = IA*(*idum-k*IQ)-IR*k;
  if (*idum < 0) *idum = *idum+IM;
  int iy = *idum;
  double ret = MIN(AM*iy, RNMX);
  return ret;
} // TurbGen_ran1s


// ************** Numerical recipes ran2 ****************
double TurbGen_ran2(int * idum) {
// ******************************************************
// Long period (> 2 x 10^18) random number generator of L'Ecuyer
// with Bays-Durham shuffle and added safeguards.
// Returns a uniform random deviate between 0.0 and 1.0 (exclusive of the endpoint values).
// Call with idum a negative integer to initialize; thereafter,
// do not alter idum between successive deviates in a sequence.
// RNMX should approximate the largest floating value that is less than 1.
// ******************************************************
  static const int IM1=2147483563, IM2=2147483399, IMM1=IM1-1, IA1=40014, IA2=40692, IQ1=53668,
                   IQ2=52774, IR1=12211, IR2=3791, NTAB=32, NDIV=1+IMM1/NTAB;
  static const double AM=1.0/IM1, EPS=1.2e-7, RNMX=1.0-EPS;
  static int idum2 = 123456789;
  static int iy = 0;
  static int iv[NTAB];
  int j, k;
  if (*idum <= 0) {
    *idum = MAX(-*idum, 1);
    idum2 = *idum;
    for (j = NTAB+7; j >= 0; j--) {
      k = *idum/IQ1;
      *idum = IA1*(*idum-k*IQ1)-k*IR1;
      if (*idum < 0) *idum += IM1;
      if (j < NTAB) iv[j] = *idum;
    }
    iy = iv[0];
  }
  k = *idum/IQ1;
  *idum = IA1*(*idum-k*IQ1)-k*IR1;
  if (*idum < 0) *idum += IM1;
  k = idum2/IQ2;
  idum2 = IA2*(idum2-k*IQ2)-k*IR2;
  if (idum2 < 0) idum2 += IM2;
  j = iy/NDIV;
  iy = iv[j]-idum2;
  iv[j] = *idum;
  if (iy < 1) iy += IMM1;
  double ret = MIN(AM*iy, RNMX);
  return ret;
} // TurbGen_ran2


// ******************************************************
void TurbGen_printf(char * format, ...) {
// ******************************************************
// special printf prepends string and only lets PE=0 print
// ******************************************************
  va_list args;
  va_start(args, format);
  char new_format[1000];
  memset(new_format, '\0', sizeof(new_format));
  strcpy(new_format, "TurbGen: ");
  strcat(new_format, format);
  if (tgd.PE == 0) vfprintf(stdout, new_format, args);
  va_end(args);
} // TurbGen_printf


#endif
// end of TURBULENCE_GENERATOR_H
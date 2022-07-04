/*******************************************************************************
 * This file is part of SWIFT.
 * Copyright (c) 2018 Matthieu Schaller (schaller@strw.leidenuniv.nl)
 *               2021 Edo Altamura (edoardo.altamura@manchester.ac.uk)
 *               2022 Doug Rennehan (douglas.rennehan@gmail.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ******************************************************************************/
#ifndef SWIFT_YAM_BH_IACT_H
#define SWIFT_YAM_BH_IACT_H

/* Local includes */
#include "black_holes_parameters.h"
#include "entropy_floor.h"
#include "equation_of_state.h"
#include "gravity.h"
#include "gravity_iact.h"
#include "hydro.h"
#include "random.h"
#include "rays.h"
#include "space.h"
#include "timestep_sync_part.h"
#include "tracers.h"

/**
 * @brief Density interaction between two particles (non-symmetric).
 *
 * @param r2 Comoving square distance between the two particles.
 * @param dx Comoving vector separating both particles (pi - pj).
 * @param bi First particle (black hole).
 * @param gj Second particle (grav, not updated).
 */
__attribute__((always_inline)) INLINE static void
runner_iact_nonsym_bh_dm_density(const float r2, const float dx[3],
    struct bpart *bi, const struct gpart *gj) {
  /* Neighbour's (drifted) velocity in the frame of the black hole
   * (we don't include a Hubble term since we are interested in the
   * velocity contribution at the location of the black hole) */
  const float dv[3] = {gj->v_full[0] - bi->v[0], gj->v_full[1] - bi->v[1],
                       gj->v_full[2] - bi->v[2]};

  bi->mean_relative_velocity_dm[0] += dv[0];
  bi->mean_relative_velocity_dm[1] += dv[1];
  bi->mean_relative_velocity_dm[2] += dv[2];

  bi->dark_matter_N_ngb++; 
}

/**
 * @brief Computes the mass of DM with velocity less than BH.
 *
 * @param r2 Comoving square distance between the two particles.
 * @param dx Comoving vector separating both particles (pi - pj).
 * @param bi First particle (black hole).
 * @param gj Second particle (grav, not updated).
 */
__attribute__((always_inline)) INLINE static void
runner_iact_nonsym_bh_dm_velocities(const float r2, const float dx[3],
    struct bpart *bi, const struct gpart *gj) {

  if (bi->dark_matter_N_ngb == 0) return;

  bi->mean_relative_velocity_dm[0] /= bi->dark_matter_N_ngb;
  bi->mean_relative_velocity_dm[1] /= bi->dark_matter_N_ngb;
  bi->mean_relative_velocity_dm[2] /= bi->dark_matter_N_ngb;

  /* TODO OMG is this even necessary? */
  const float mean_relative_velocity_dm2 =
      bi->mean_relative_velocity_dm[0] * bi->mean_relative_velocity_dm[0] + 
      bi->mean_relative_velocity_dm[1] * bi->mean_relative_velocity_dm[1] + 
      bi->mean_relative_velocity_dm[2] * bi->mean_relative_velocity_dm[2];

  const float v_relative_mag2 = 
      (gj->v_full[0] - dm_com_velocity[0]) * (gj->v_full[0] - dm_com_velocity[0]) + 
      (gj->v_full[1] - dm_com_velocity[1]) * (gj->v_full[1] - dm_com_velocity[1]) + 
      (gj->v_full[2] - dm_com_velocity[2]) * (gj->v_full[2] - dm_com_velocity[2]);
  
  /* If the relative velocity of the dark matter, compared to v_com,
   * is SMALLER than the relative velocity of the black hole, compared to v_com,
   * then we count that mass towards dynamical friction */
  if (v_relative_mag2 < mean_relative_velocity_dm2) {
    bi->dm_mass_low_vel += gj->mass;
  }
}

/**
 * @brief Density interaction between two particles (non-symmetric).
 *
 * @param r2 Comoving square distance between the two particles.
 * @param dx Comoving vector separating both particles (pi - pj).
 * @param bi First particle (black hole).
 * @param sj Second particle (stars, not updated).
 */
__attribute__((always_inline)) INLINE static void
runner_iact_nonsym_bh_stars_density(
    const float r2, const float dx[3],
    struct bpart *bi, const struct spart *sj) {

  /* Neighbour's (drifted) velocity in the frame of the black hole
   * (we don't include a Hubble term since we are interested in the
   * velocity contribution at the location of the black hole) */
  const float dv[3] = {sj->v[0] - bi->v[0], sj->v[1] - bi->v[1],
                       sj->v[2] - bi->v[2]};

  bi->specific_angular_momentum_stars[0] += dx[1] * dv[2] - dx[2] * dv[1];
  bi->specific_angular_momentum_stars[1] += dx[2] * dv[0] - dx[0] * dv[2];
  bi->specific_angular_momentum_stars[2] += dx[0] * dv[1] - dx[1] * dv[0];  
}

/**
 * @brief Density interaction between two particles (non-symmetric).
 *
 * @param r2 Comoving square distance between the two particles.
 * @param dx Comoving vector separating both particles (pi - pj).
 * @param bi First particle (black hole).
 * @param sj Second particle (stars, not updated).
 */
__attribute__((always_inline)) INLINE static void
runner_iact_nonsym_bh_stars_bulge(
    const float r2, const float dx[3],
    struct bpart *bi, const struct spart *sj) {

  /* Neighbour's (drifted) velocity in the frame of the black hole
   * (we don't include a Hubble term since we are interested in the
   * velocity contribution at the location of the black hole) */
  const float dv[3] = {sj->v[0] - bi->v[0], sj->v[1] - bi->v[1],
                       sj->v[2] - bi->v[2]};

  const float star_angular_momentum[3] = {
    dx[1] * dv[2] - dx[2] * dv[1],
    dx[2] * dv[0] - dx[0] * dv[2],
    dx[0] * dv[1] - dx[1] * dv[0]
  };

  const float proj = star_angular_momentum[0] * bi->specific_angular_momentum_stars[0] 
                      + star_angular_momentum[1] * bi->specific_angular_momentum_stars[1]
                      + star_angular_momentum[2] * bi->specific_angular_momentum_stars[2];

  bi->stellar_mass += sj->mass;
  /* The bulge mass is twice the counter rotating mass */
  if (proj < 0.f) bi->stellar_bulge_mass += 2.f * sj->mass;
}

/**
 * @brief Density interaction between two particles (non-symmetric).
 *
 * @param r2 Comoving square distance between the two particles.
 * @param dx Comoving vector separating both particles (pi - pj).
 * @param hi Comoving smoothing-length of particle i.
 * @param hj Comoving smoothing-length of particle j.
 * @param bi First particle (black hole).
 * @param pj Second particle (gas, not updated).
 * @param xpj The extended data of the second particle (not updated).
 * @param with_cosmology Are we doing a cosmological run?
 * @param cosmo The cosmological model.
 * @param grav_props The properties of the gravity scheme (softening, G, ...).
 * @param bh_props The properties of the BH scheme
 * @param ti_current Current integer time value (for random numbers).
 * @param time Current physical time in the simulation.
 */
__attribute__((always_inline)) INLINE static void
runner_iact_nonsym_bh_gas_density(
    const float r2, const float dx[3], const float hi, const float hj,
    struct bpart *bi, const struct part *pj, const struct xpart *xpj,
    const int with_cosmology, const struct cosmology *cosmo,
    const struct gravity_props *grav_props,
    const struct black_holes_props *bh_props,
    const struct entropy_floor_properties *floor_props,
    const integertime_t ti_current, const double time,
    const double time_base) {

  /* Ignore decoupled winds in density computation */
  if (pj->feedback_data.decoupling_delay_time > 0.f) return;

  float wi, wi_dx;

  /* Compute the kernel function; note that r cannot be optimised
   * to r2 / sqrtf(r2) because of 1 / 0 behaviour. */
  const float r = sqrtf(r2);
  const float hi_inv = 1.0f / hi;
  const float ui = r * hi_inv;
  kernel_deval(ui, &wi, &wi_dx);

  /* Compute contribution to the number of neighbours */
  bi->density.wcount += wi;
  bi->density.wcount_dh -= (hydro_dimension * wi + ui * wi_dx);

  /* Contribution to the number of neighbours */
  bi->num_ngbs += 1;

  /* Neighbour gas mass */
  const float mj = hydro_get_mass(pj);

  /* Contribution to the BH gas density */
  bi->rho_gas += mj * wi;

  /* Contribution to the total neighbour mass */
  bi->ngb_mass += mj;

  /* Contribution to the smoothed sound speed */
  float cj = hydro_get_comoving_soundspeed(pj);
  if (bh_props->with_fixed_T_near_EoS) {

    /* Check whether we are close to the entropy floor. If we are, we
     * re-calculate the sound speed using the fixed internal energy */
    const float u_EoS = entropy_floor_temperature(pj, cosmo, floor_props) *
                        bh_props->temp_to_u_factor;
    const float u = hydro_get_drifted_comoving_internal_energy(pj);
    if (u < u_EoS * bh_props->fixed_T_above_EoS_factor &&
        u > bh_props->fixed_u_for_soundspeed) {
      cj = gas_soundspeed_from_internal_energy(
          pj->rho, bh_props->fixed_u_for_soundspeed);
    }
  }
  bi->sound_speed_gas += mj * wi * cj;

  /* Neighbour internal energy */
  const float uj = hydro_get_drifted_comoving_internal_energy(pj);

  /* Contribution to the smoothed internal energy */
  bi->internal_energy_gas += mj * uj * wi;

  /* Neighbour's (drifted) velocity in the frame of the black hole
   * (we don't include a Hubble term since we are interested in the
   * velocity contribution at the location of the black hole) */
  const float dv[3] = {pj->v[0] - bi->v[0], pj->v[1] - bi->v[1],
                       pj->v[2] - bi->v[2]};

  /* Account for hot and cold gas surrounding the SMBH */
  const float Tj = uj * cosmo->a_factor_internal_energy /
                       bh_props->temp_to_u_factor;

  if (Tj > bh_props->environment_temperature_cut) {
    bi->hot_gas_mass += mj;
    bi->hot_gas_internal_energy += mj * uj; /* Not kernel weighted */
  } else {
    bi->cold_gas_mass += mj;
  }

  /* Contribution to the smoothed velocity (gas w.r.t. black hole) */
  bi->velocity_gas[0] += mj * wi * dv[0];
  bi->velocity_gas[1] += mj * wi * dv[1];
  bi->velocity_gas[2] += mj * wi * dv[2];

  /* Contribution to the mass-weighted velocity (gas w.r.t. black hole) */
  bi->mass_weighted_velocity_gas[0] += mj * dv[0];
  bi->mass_weighted_velocity_gas[1] += mj * dv[1];
  bi->mass_weighted_velocity_gas[2] += mj * dv[2];

  /* Contribution to the specific angular momentum of gas, which is later
   * converted to the circular velocity at the smoothing length */
  bi->circular_velocity_gas[0] -= mj * wi * (dx[1] * dv[2] - dx[2] * dv[1]);
  bi->circular_velocity_gas[1] -= mj * wi * (dx[2] * dv[0] - dx[0] * dv[2]);
  bi->circular_velocity_gas[2] -= mj * wi * (dx[0] * dv[1] - dx[1] * dv[0]);

#ifdef DEBUG_INTERACTIONS_BH
  /* Update ngb counters */
  if (si->num_ngb_density < MAX_NUM_OF_NEIGHBOURS_BH)
    bi->ids_ngbs_density[si->num_ngb_density] = pj->id;

  /* Update ngb counters */
  ++si->num_ngb_density;
#endif
}

/**
 * @brief Repositioning interaction between two particles (non-symmetric).
 *
 * Function used to identify the gas particle that this BH may move towards.
 *
 * @param r2 Comoving square distance between the two particles.
 * @param dx Comoving vector separating both particles (pi - pj).
 * @param hi Comoving smoothing-length of particle i.
 * @param hj Comoving smoothing-length of particle j.
 * @param bi First particle (black hole).
 * @param pj Second particle (gas)
 * @param xpj The extended data of the second particle.
 * @param with_cosmology Are we doing a cosmological run?
 * @param cosmo The cosmological model.
 * @param grav_props The properties of the gravity scheme (softening, G, ...).
 * @param bh_props The properties of the BH scheme
 * @param ti_current Current integer time value (for random numbers).
 * @param time Current physical time in the simulation.
 */
__attribute__((always_inline)) INLINE static void
runner_iact_nonsym_bh_gas_repos(
    const float r2, const float dx[3], const float hi, const float hj,
    struct bpart *bi, const struct part *pj, const struct xpart *xpj,
    const int with_cosmology, const struct cosmology *cosmo,
    const struct gravity_props *grav_props,
    const struct black_holes_props *bh_props,
    const struct entropy_floor_properties *floor_props,
    const integertime_t ti_current, const double time,
    const double time_base) {

  /* Use a more realistic approach? */
  if (!bh_props->resposition_with_dynamical_friction) return;

  /* Ignore decoupled wind particles */
  if (pj->feedback_data.decoupling_delay_time > 0.f) return;

  float wi;

  /* Compute the kernel function; note that r cannot be optimised
   * to r2 / sqrtf(r2) because of 1 / 0 behaviour. */
  const float r = sqrtf(r2);
  const float hi_inv = 1.0f / hi;
  const float ui = r * hi_inv;
  kernel_eval(ui, &wi);

  /* (Square of) Max repositioning distance allowed based on the softening */
  const float max_dist_repos2 =
      kernel_gravity_softening_plummer_equivalent_inv *
      kernel_gravity_softening_plummer_equivalent_inv *
      bh_props->max_reposition_distance_ratio *
      bh_props->max_reposition_distance_ratio * grav_props->epsilon_baryon_cur *
      grav_props->epsilon_baryon_cur;

  /* Is this gas neighbour close enough that we can consider its potential
     for repositioning? */
  if (r2 < max_dist_repos2) {

    /* Flag to check whether neighbour is slow enough to be considered
     * as repositioning target. Always true if velocity cut is switched off. */
    int neighbour_is_slow_enough = 1;
    if (bh_props->with_reposition_velocity_threshold) {

      /* Compute relative peculiar velocity between the two BHs
       * Recall that in SWIFT v is (v_pec * a) */
      const float delta_v[3] = {bi->v[0] - pj->v[0], bi->v[1] - pj->v[1],
                                bi->v[2] - pj->v[2]};
      const float v2 = delta_v[0] * delta_v[0] + delta_v[1] * delta_v[1] +
                       delta_v[2] * delta_v[2];
      const float v2_pec = v2 * cosmo->a2_inv;

      /* Compute the maximum allowed velocity */
      float v2_max = bh_props->max_reposition_velocity_ratio *
                     bh_props->max_reposition_velocity_ratio *
                     bi->sound_speed_gas * bi->sound_speed_gas *
                     cosmo->a_factor_sound_speed * cosmo->a_factor_sound_speed;

      /* If desired, limit the value of the threshold (v2_max) to be no
       * smaller than a user-defined value */
      if (bh_props->min_reposition_velocity_threshold > 0) {
        const float v2_min_thresh =
            bh_props->min_reposition_velocity_threshold *
            bh_props->min_reposition_velocity_threshold;
        v2_max = max(v2_max, v2_min_thresh);
      }

      /* Is the neighbour too fast to jump to? */
      if (v2_pec >= v2_max) neighbour_is_slow_enough = 0;
    }

    if (neighbour_is_slow_enough) {
      float potential = pj->black_holes_data.potential;

      if (bh_props->correct_bh_potential_for_repositioning) {

        /* Let's not include the contribution of the BH
         * itself to the potential of the gas particle */

        /* Note: This assumes the BH and gas have the same
         * softening, which is currently true */
        const float eps = gravity_get_softening(bi->gpart, grav_props);
        const float eps2 = eps * eps;
        const float eps_inv = 1.f / eps;
        const float eps_inv3 = eps_inv * eps_inv * eps_inv;
        const float BH_mass = bi->mass;

        /* Compute the Newtonian or truncated potential the BH
         * exherts onto the gas particle */
        float dummy, pot_ij;
        runner_iact_grav_pp_full(r2, eps2, eps_inv, eps_inv3, BH_mass, &dummy,
                                 &pot_ij);

        /* Deduct the BH contribution */
        potential -= pot_ij * grav_props->G_Newton;
      }

      /* Is the potential lower? */
      if (potential < bi->reposition.min_potential) {

        /* Store this as our new best */
        bi->reposition.min_potential = potential;
        bi->reposition.delta_x[0] = -dx[0];
        bi->reposition.delta_x[1] = -dx[1];
        bi->reposition.delta_x[2] = -dx[2];
      }
    }
  }
}

/**
 * @brief Swallowing interaction between two particles (non-symmetric).
 *
 * Function used to flag the gas particles that will be swallowed
 * by the black hole particle.
 *
 * @param r2 Comoving square distance between the two particles.
 * @param dx Comoving vector separating both particles (pi - pj).
 * @param hi Comoving smoothing-length of particle i.
 * @param hj Comoving smoothing-length of particle j.
 * @param bi First particle (black hole).
 * @param pj Second particle (gas)
 * @param xpj The extended data of the second particle.
 * @param with_cosmology Are we doing a cosmological run?
 * @param cosmo The cosmological model.
 * @param grav_props The properties of the gravity scheme (softening, G, ...).
 * @param bh_props The properties of the BH scheme
 * @param ti_current Current integer time value (for random numbers).
 * @param time Current physical time in the simulation.
 */
__attribute__((always_inline)) INLINE static void
runner_iact_nonsym_bh_gas_swallow(
    const float r2, const float dx[3], const float hi, const float hj,
    struct bpart *bi, struct part *pj, struct xpart *xpj,
    const int with_cosmology, const struct cosmology *cosmo,
    const struct gravity_props *grav_props,
    const struct black_holes_props *bh_props,
    const struct entropy_floor_properties *floor_props,
    const integertime_t ti_current, const double time,
    const double time_base) {

  /* IMPORTANT: Do not even consider wind particles for accretion/feedback */
  if (pj->feedback_data.decoupling_delay_time > 0.f) return;

  float wi;

  /* Compute the kernel function; note that r cannot be optimised
   * to r2 / sqrtf(r2) because of 1 / 0 behaviour. */
  const float r = sqrtf(r2);
  const float hi_inv = 1.0f / hi;
  const float hi_inv_dim = pow_dimension(hi_inv);
  const float ui = r * hi_inv;
  kernel_eval(ui, &wi);

  /* Get particle time-step */
  double dt;
  if (with_cosmology) {
    const integertime_t ti_step = get_integer_timestep(bi->time_bin);
    const integertime_t ti_begin =
        get_integer_time_begin(ti_current - 1, bi->time_bin);

    dt = cosmology_get_delta_time(cosmo, ti_begin,
                                  ti_begin + ti_step);
  } else {
    dt = get_timestep(bi->time_bin, time_base);
  }

  /* Probability to swallow this particle */
  float prob = -1.f;

  /* Radiation was already accounted for in bi->subgrid_mass
    * so if is is bigger than bi->mass we can simply
    * flag particles to eat and satisfy the mass constraint.
    * 
    * If bi->subgrid_mass < bi->mass then there is a problem,
    * but we use the full accretion rate to select particles
    * and then don't actually take anything away from them.
    * The bi->mass variable is decreased previously to account
    * for the radiative losses.
    */
  const float mass_deficit = bi->subgrid_mass - bi->mass_at_start_of_step;
  if (mass_deficit >= 0.f) {
    /* Don't nibble from particles that are too small already */
    if (pj->mass < bh_props->min_gas_mass_for_nibbling) return;

    prob = (mass_deficit / bi->f_accretion) 
        * hi_inv_dim * wi / bi->rho_gas;
  } else {
    /* The subgrid mass has not caught up to the physical mass.
      * The probability is modified so that we will have the 
      * correct outflow.
      * 
      * bi->f_accretion is guaranteed to be nonzero.
      * 
      * bi->accretion_rate is the true accretion rate,
      * and does not have the radiation taken out yet.
      * We select particles assuming the full accretion rate,
      * and then decrease the BH mass accordingly.
      * That guarantees that the gas masses decrease fully,
      * and that mass never makes it into the black hole.
      */
    prob = ((1.f - bi->f_accretion) / bi->f_accretion) 
        * bi->accretion_rate 
        * dt 
        * (hi_inv_dim * wi / bi->rho_gas);
  }


  /* Draw a random number (Note mixing both IDs) */
  const float rand = random_unit_interval(bi->id + pj->id, ti_current,
                                          random_number_BH_swallow);

  /* Are we lucky? */
  if (rand < prob) {

    /* If the sub-grid mass is larger, eat away buddy */
    if (mass_deficit > 0.f) {
      const float bi_mass_orig = bi->mass;
      const float pj_mass_orig = pj->mass;
      const float nibbled_mass = bi->f_accretion * pj->mass;
      const float new_gas_mass = pj->mass - nibbled_mass;
      /* Don't go below the minimum for stability */
      if (new_gas_mass < bh_props->min_gas_mass_for_nibbling) return;

      bi->mass += nibbled_mass;
      hydro_set_mass(pj, new_gas_mass);

      /* Add the angular momentum of the accreted gas to the BH total.
      * Note no change to gas here. The cosmological conversion factors for
      * velocity (a^-1) and distance (a) cancel out, so the angular momentum
      * is already in physical units. */
      const float dv[3] = {bi->v[0] - pj->v[0], bi->v[1] - pj->v[1],
                          bi->v[2] - pj->v[2]};
      bi->swallowed_angular_momentum[0] +=
          nibbled_mass * (dx[1] * dv[2] - dx[2] * dv[1]);
      bi->swallowed_angular_momentum[1] +=
          nibbled_mass * (dx[2] * dv[0] - dx[0] * dv[2]);
      bi->swallowed_angular_momentum[2] +=
          nibbled_mass * (dx[0] * dv[1] - dx[1] * dv[0]);

      /* Update the BH momentum and velocity. Again, no change to gas here. */
      const float bi_mom[3] = {bi_mass_orig * bi->v[0] + nibbled_mass * pj->v[0],
                              bi_mass_orig * bi->v[1] + nibbled_mass * pj->v[1],
                              bi_mass_orig * bi->v[2] + nibbled_mass * pj->v[2]};

      bi->v[0] = bi_mom[0] / bi->mass;
      bi->v[1] = bi_mom[1] / bi->mass;
      bi->v[2] = bi_mom[2] / bi->mass;

      /* Update the BH and also gas metal masses */
      struct chemistry_bpart_data *bi_chem = &bi->chemistry_data;
      struct chemistry_part_data *pj_chem = &pj->chemistry_data;
      chemistry_transfer_part_to_bpart(
          bi_chem, pj_chem, nibbled_mass,
          nibbled_mass / pj_mass_orig);

    }

    /* This particle is swallowed by the BH with the largest ID of all the
      * candidates wanting to swallow it */
    if (pj->black_holes_data.swallow_id < bi->id) {
      pj->black_holes_data.swallow_id = bi->id;
    } else {
      message(
          "BH %lld wants to swallow gas particle %lld BUT CANNOT (old "
          "swallow id=%lld)",
          bi->id, pj->id, pj->black_holes_data.swallow_id);
    }
  }
}

/**
 * @brief Swallowing interaction between two BH particles (non-symmetric).
 *
 * Function used to identify the BH particle that this BH may move towards.
 *
 * @param r2 Comoving square distance between the two particles.
 * @param dx Comoving vector separating both particles (pi - pj).
 * @param hi Comoving smoothing-length of particle i.
 * @param hj Comoving smoothing-length of particle j.
 * @param bi First particle (black hole).
 * @param bj Second particle (black hole)
 * @param cosmo The cosmological model.
 * @param grav_props The properties of the gravity scheme (softening, G, ...).
 * @param bh_props The properties of the BH scheme
 * @param ti_current Current integer time value (for random numbers).
 */
__attribute__((always_inline)) INLINE static void
runner_iact_nonsym_bh_bh_repos(const float r2, const float dx[3],
                               const float hi, const float hj, struct bpart *bi,
                               const struct bpart *bj,
                               const struct cosmology *cosmo,
                               const struct gravity_props *grav_props,
                               const struct black_holes_props *bh_props,
                               const integertime_t ti_current) {

  /* Compute relative peculiar velocity between the two BHs
   * Recall that in SWIFT v is (v_pec * a) */
  const float delta_v[3] = {bi->v[0] - bj->v[0], bi->v[1] - bj->v[1],
                            bi->v[2] - bj->v[2]};
  const float v2 = delta_v[0] * delta_v[0] + delta_v[1] * delta_v[1] +
                   delta_v[2] * delta_v[2];

  const float v2_pec = v2 * cosmo->a2_inv;

  /* (Square of) Max repositioning distance allowed based on the softening */
  const float max_dist_repos2 =
      kernel_gravity_softening_plummer_equivalent_inv *
      kernel_gravity_softening_plummer_equivalent_inv *
      bh_props->max_reposition_distance_ratio *
      bh_props->max_reposition_distance_ratio * grav_props->epsilon_baryon_cur *
      grav_props->epsilon_baryon_cur;

  /* Is this BH neighbour close enough that we can consider its potential
     for repositioning? */
  if (r2 < max_dist_repos2) {

    /* Flag to check whether neighbour is slow enough to be considered
     * as repositioning target. Always true if velocity cut switched off */
    int neighbour_is_slow_enough = 1;
    if (bh_props->with_reposition_velocity_threshold) {

      /* Compute the maximum allowed velocity */
      float v2_max = bh_props->max_reposition_velocity_ratio *
                     bh_props->max_reposition_velocity_ratio *
                     bi->sound_speed_gas * bi->sound_speed_gas *
                     cosmo->a_factor_sound_speed * cosmo->a_factor_sound_speed;

      /* If desired, limit the value of the threshold (v2_max) to be no
       * smaller than a user-defined value */
      if (bh_props->min_reposition_velocity_threshold > 0) {
        const float v2_min_thresh =
            bh_props->min_reposition_velocity_threshold *
            bh_props->min_reposition_velocity_threshold;
        v2_max = max(v2_max, v2_min_thresh);
      }

      /* Is the neighbour too fast to jump to? */
      if (v2_pec >= v2_max) neighbour_is_slow_enough = 0;
    }

    if (neighbour_is_slow_enough) {
      float potential = bj->reposition.potential;

      if (bh_props->correct_bh_potential_for_repositioning) {

        /* Let's not include the contribution of the BH i
         * to the potential of the BH j */
        const float eps = gravity_get_softening(bi->gpart, grav_props);
        const float eps2 = eps * eps;
        const float eps_inv = 1.f / eps;
        const float eps_inv3 = eps_inv * eps_inv * eps_inv;
        const float BH_mass = bi->mass;

        /* Compute the Newtonian or truncated potential the BH
         * exherts onto the gas particle */
        float dummy, pot_ij;
        runner_iact_grav_pp_full(r2, eps2, eps_inv, eps_inv3, BH_mass, &dummy,
                                 &pot_ij);

        /* Deduct the BH contribution */
        potential -= pot_ij * grav_props->G_Newton;
      }

      /* Is the potential lower? */
      if (potential < bi->reposition.min_potential) {

        /* Store this as our new best */
        bi->reposition.min_potential = potential;
        bi->reposition.delta_x[0] = -dx[0];
        bi->reposition.delta_x[1] = -dx[1];
        bi->reposition.delta_x[2] = -dx[2];
      }
    }
  }
}

/**
 * @brief Swallowing interaction between two BH particles (non-symmetric).
 *
 * Function used to flag the BH particles that will be swallowed
 * by the black hole particle.
 *
 * @param r2 Comoving square distance between the two particles.
 * @param dx Comoving vector separating both particles (pi - pj).
 * @param hi Comoving smoothing-length of particle i.
 * @param hj Comoving smoothing-length of particle j.
 * @param bi First particle (black hole).
 * @param bj Second particle (black hole)
 * @param cosmo The cosmological model.
 * @param grav_props The properties of the gravity scheme (softening, G, ...).
 * @param bh_props The properties of the BH scheme
 * @param ti_current Current integer time value (for random numbers).
 */
__attribute__((always_inline)) INLINE static void
runner_iact_nonsym_bh_bh_swallow(const float r2, const float dx[3],
                                 const float hi, const float hj,
                                 struct bpart *bi, struct bpart *bj,
                                 const struct cosmology *cosmo,
                                 const struct gravity_props *grav_props,
                                 const struct black_holes_props *bh_props,
                                 const integertime_t ti_current) {

  /* Compute relative peculiar velocity between the two BHs
   * Recall that in SWIFT v is (v_pec * a) */
  const float delta_v[3] = {bi->v[0] - bj->v[0], bi->v[1] - bj->v[1],
                            bi->v[2] - bj->v[2]};
  const float v2 = delta_v[0] * delta_v[0] + delta_v[1] * delta_v[1] +
                   delta_v[2] * delta_v[2];

  const float v2_pec = v2 * cosmo->a2_inv;

  /* Find the most massive of the two BHs */
  float M = bi->subgrid_mass;
  float h = hi;
  if (bj->subgrid_mass > M) {
    M = bj->subgrid_mass;
    h = hj;
  }

  /* (Square of) max swallowing distance allowed based on the softening */
  const float max_dist_merge2 =
      kernel_gravity_softening_plummer_equivalent_inv *
      kernel_gravity_softening_plummer_equivalent_inv *
      bh_props->max_merging_distance_ratio *
      bh_props->max_merging_distance_ratio * grav_props->epsilon_baryon_cur *
      grav_props->epsilon_baryon_cur;

  const float G_Newton = grav_props->G_Newton;

  /* The BH with the smaller mass will be merged onto the one with the
   * larger mass.
   * To avoid rounding issues, we additionally check for IDs if the BHs
   * have the exact same mass. */
  if ((bj->subgrid_mass < bi->subgrid_mass) ||
      (bj->subgrid_mass == bi->subgrid_mass && bj->id < bi->id)) {

    /* Merge if gravitationally bound AND if within max distance
     * Note that we use the kernel support here as the size and not just the
     * smoothing length */

    /* Maximum velocity difference between BHs allowed to merge */
    float v2_threshold;

    if (bh_props->merger_threshold_type == BH_mergers_circular_velocity) {

      /* 'Old-style' merger threshold using circular velocity at the
       * edge of the more massive BH's kernel */
      v2_threshold = G_Newton * M / (kernel_gamma * h);
    } else {

      /* Arguably better merger threshold using the escape velocity at
       * the distance between the BHs */

      if (bh_props->merger_threshold_type == BH_mergers_escape_velocity) {

        /* Standard formula (not softening BH interactions) */
        v2_threshold = 2.f * G_Newton * M / sqrt(r2);
      } else if (bh_props->merger_threshold_type ==
                 BH_mergers_dynamical_escape_velocity) {

        /* General two-body escape velocity based on dynamical masses */
        v2_threshold = 2.f * G_Newton * (bi->mass + bj->mass) / sqrt(r2);
      } else {
        /* Cannot happen! */
#ifdef SWIFT_DEBUG_CHECKS
        error("Invalid choice of BH merger threshold type");
#endif
        v2_threshold = 0.f;
      }
    } /* Ends sections for different merger thresholds */

    if ((v2_pec < v2_threshold) && (r2 < max_dist_merge2)) {

      /* This particle is swallowed by the BH with the largest mass of all the
       * candidates wanting to swallow it (we use IDs to break ties)*/
      if ((bj->merger_data.swallow_mass < bi->subgrid_mass) ||
          (bj->merger_data.swallow_mass == bi->subgrid_mass &&
           bj->merger_data.swallow_id < bi->id)) {

        message("BH %lld wants to swallow BH particle %lld", bi->id, bj->id);

        bj->merger_data.swallow_id = bi->id;
        bj->merger_data.swallow_mass = bi->subgrid_mass;

      } else {

        message(
            "BH %lld wants to swallow gas particle %lld BUT CANNOT (old "
            "swallow id=%lld)",
            bi->id, bj->id, bj->merger_data.swallow_id);
      }
    }
  }
}

/**
 * @brief Feedback interaction between two particles (non-symmetric).
 *
 * @param r2 Comoving square distance between the two particles.
 * @param dx Comoving vector separating both particles (pi - pj).
 * @param hi Comoving smoothing-length of particle i.
 * @param hj Comoving smoothing-length of particle j.
 * @param bi First particle (black hole).
 * @param pj Second particle (gas)
 * @param xpj The extended data of the second particle.
 * @param with_cosmology Are we doing a cosmological run?
 * @param cosmo The cosmological model.
 * @param grav_props The properties of the gravity scheme (softening, G, ...).
 * @param bh_props The properties of the BH scheme
 * @param ti_current Current integer time value (for random numbers).
 * @param time current physical time in the simulation
 */
__attribute__((always_inline)) INLINE static void
runner_iact_nonsym_bh_gas_feedback(
    const float r2, const float dx[3], const float hi, const float hj,
    struct bpart *bi, struct part *pj, struct xpart *xpj,
    const int with_cosmology, const struct cosmology *cosmo,
    const struct gravity_props *grav_props,
    const struct black_holes_props *bh_props,
    const struct entropy_floor_properties *floor_props,
    const integertime_t ti_current, const double time,
    const double time_base) {

  /* This shouldn't happen, but just be sure anyway */
  if (pj->feedback_data.decoupling_delay_time > 0.f) return;

  /* In YAM, all nibbled particles are ejected as a wind */
  if (pj->black_holes_data.swallow_id == bi->id) {
    /* Save gas density and entropy before feedback */
    tracers_before_black_holes_feedback(pj, xpj, cosmo->a);

    float v_kick = bi->v_kick;  /* PHYSICAL */
    int jet_flag = 0;
    /* TODO enum */
    if(bi->state == 0) {
      const double random_number = 
        random_unit_interval(bi->id, ti_current, random_number_BH_feedback);
      if (random_number < bi->jet_prob) {
        v_kick = bh_props->jet_velocity; 
        jet_flag = 1;

        /* We don't do anything to this particle if we have run out
         * of jet energy.
         */
        if (bi->jet_energy_used >= bi->jet_energy_available) {
          black_holes_mark_part_as_not_swallowed(&pj->black_holes_data);
          return;
        }
      }
    }

    /* PHYSICAL */
    const double dE_kinetic_energy = 0.5f * hydro_get_mass(pj) * v_kick * v_kick;
    bi->delta_energy_this_timestep += dE_kinetic_energy;

    /* If we have a jet, we heat! */
    if (jet_flag) {
      float new_Tj = 0.f;
      /* Use the halo Tvir? */
      if (bh_props->jet_temperature < 0.f) {
        /* TODO: Get the halo Tvir for pj */
        new_Tj = 1.0e8f; /* K */
      } else {
        new_Tj = bh_props->jet_temperature; /* K */
      }

      /* Compute new energy per unit mass of this particle */
      const double u_init = hydro_get_physical_internal_energy(pj, xpj, cosmo);
      double u_new = bh_props->temp_to_u_factor * new_Tj;

      double dE_internal_energy = 
          (u_new - u_init > 0.) ? hydro_get_mass(pj) * (u_new - u_init) : 0.;
      double dE_particle = dE_kinetic_energy + dE_internal_energy;
      const float jet_energy_frac = 
          (dE_particle > 0.) ? (bi->jet_energy_available - bi->jet_energy_used) / dE_particle : 0.;
  
      /* It is VERY important that we checked jet_energy_used < jet_energy_available above !! */
      if ((bi->jet_energy_used + dE_particle) > bi->jet_energy_available &&
        dE_particle > 0.) {
        dE_particle = bi->jet_energy_available - bi->jet_energy_used;
        /* If this is not true, then it will skip this particle anyway */
        v_kick *= sqrtf(jet_energy_frac);

        /* TODO Do we actually want to limit the thermal output? */
        dE_internal_energy *= jet_energy_frac;

        u_new = u_init + (dE_internal_energy / hydro_get_mass(pj));
      }

      message("BH_JET: bid=%lld heating pid=%lld to T=%g K and kicking to v=%g km/s (limiter=%g)",
        bi->id, pj->id, u_new / bh_props->temp_to_u_factor,
        v_kick / bh_props->kms_to_internal,
        jet_energy_frac);

      bi->jet_energy_used += dE_particle;

      /* Don't decrease the gas temperature if it's already hotter */
      if (u_new > u_init) {
        /* We are overwriting the internal energy of the particle */
        hydro_set_physical_internal_energy(pj, xpj, cosmo, u_new);
        hydro_set_drifted_physical_internal_energy(pj, cosmo, u_new);

        const double delta_energy = (u_new - u_init) * hydro_get_mass(pj);

        /* Make sure the timestepping knows of this heating event */
        bi->delta_energy_this_timestep += delta_energy;

        tracers_after_black_holes_feedback(pj, xpj, with_cosmology, cosmo->a,
                                           time, delta_energy);
      }
    }

    /* TODO: Don't we have the angular momentum already? */
    /* Compute relative peculiar velocity between the two particles */
    const float delta_v[3] = {pj->v[0] - bi->v[0], pj->v[1] - bi->v[1],
                              pj->v[2] - bi->v[2]};

    /* compute direction of kick: r x v */ 
    const float dir[3] = {dx[1] * delta_v[2] - dx[2] * delta_v[1],
                          dx[2] * delta_v[0] - dx[0] * delta_v[2],
                          dx[0] * delta_v[1] - dx[1] * delta_v[0]};
    const float norm = 
        sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);

    /* TODO: Remove */
    float pj_vel_norm = 0.f;
    pj_vel_norm = pj->v[0] * pj->v[0] + pj->v[1] * pj->v[1] + pj->v[2] * pj->v[2];
    pj_vel_norm = sqrtf(pj_vel_norm);

    /* TODO: random_uniform() won't work here?? */
    /*const float dirsign = (random_uniform(-1.0, 1.0) > 0. ? 1.f : -1.f);*/
    const double random_number = 
        random_unit_interval(bi->id, ti_current, random_number_BH_feedback);
    const float dirsign = (random_number > 0.5) ? 1.f : -1.f;
    const float prefactor = v_kick * cosmo->a * dirsign / norm;

    pj->v[0] += prefactor * dir[0];
    pj->v[1] += prefactor * dir[1];
    pj->v[2] += prefactor * dir[2];

    message("BH_KICK: kicking id=%lld, v_kick=%g (internal), v_kick/v_part=%g",
        pj->id, v_kick * cosmo->a, v_kick * cosmo->a / pj_vel_norm);

    /* Set delay time */
    pj->feedback_data.decoupling_delay_time = 
        1.0e-4f * cosmology_get_time_since_big_bang(cosmo, cosmo->a);

    /* Update the signal velocity of the particle based on the velocity kick. */
    hydro_set_v_sig_based_on_velocity_kick(pj, cosmo, v_kick);

    /* Impose maximal viscosity */
    hydro_diffusive_feedback_reset(pj);
    
    /* Synchronize the particle on the timeline */
    timestep_sync_part(pj);

    /* IMPORTANT: The particle MUST NOT be swallowed. 
     * We are taking a f_accretion from each particle, and then
     * kicking the rest. We used the swallow marker as a temporary
     * passer in order to remember which particles have been "nibbled"
     * so that we can kick them out.
     */
    black_holes_mark_part_as_not_swallowed(&pj->black_holes_data);

#ifdef DEBUG_INTERACTIONS_BH
    /* Update ngb counters */
    if (si->num_ngb_force < MAX_NUM_OF_NEIGHBOURS_BH)
      bi->ids_ngbs_force[si->num_ngb_force] = pj->id;

    /* Update ngb counters */
    ++si->num_ngb_force;
#endif
  } else {
        /* We were not lucky, but we are lucky to heat via X-rays */
    if (bi->v_kick > bh_props->xray_heating_velocity_threshold
        && bi->delta_energy_this_timestep < bi->energy_reservoir) {
      /* Get particle time-step */
      double dt;
      if (with_cosmology) {
        const integertime_t ti_step = get_integer_timestep(bi->time_bin);
        const integertime_t ti_begin =
            get_integer_time_begin(ti_current - 1, bi->time_bin);

        dt = cosmology_get_delta_time(cosmo, ti_begin,
                                      ti_begin + ti_step);
      } else {
        dt = get_timestep(bi->time_bin, time_base);
      }

      const float r = sqrtf(r2);
      /* Hydrogen number density (X_H * rho / m_p) [cm^-3] */
      const float n_H_cgs = hydro_get_physical_density(pj, cosmo) * 
                            bh_props->rho_to_n_cgs;
      const double u_init = hydro_get_physical_internal_energy(pj, xpj, cosmo);
      const float T_gas_cgs = 
          u_init / (bh_props->temp_to_u_factor * bh_props->T_K_to_int);

      double du_xray_phys = 
          black_holes_compute_xray_feedback(bi, pj, bh_props, 
              cosmo, dx, dt, n_H_cgs, T_gas_cgs);

      /* Limit the amount of heating BEFORE dividing to avoid numerical
       * instability */
      if (du_xray_phys > bh_props->xray_maximum_heating_factor * u_init) {
        du_xray_phys = bh_props->xray_maximum_heating_factor * u_init;
      }

      const double dE_this_step = du_xray_phys * pj->mass;
      const double energy_after_step = 
          bi->delta_energy_this_timestep + dE_this_step;
      if (energy_after_step > bi->energy_reservoir) {
        du_xray_phys = 
            (bi->energy_reservoir - bi->delta_energy_this_timestep) /
            pj->mass;
      }

      /* If it goes over energy_reservoir it doesn't matter,
       * because we don't want it to continue anyway */
      bi->delta_energy_this_timestep += dE_this_step;

      /* Look for cold dense gas. Then push it. */
      if (n_H_cgs > bh_props->xray_heating_n_H_threshold_cgs &&
          T_gas_cgs < bh_props->xray_heating_T_threshold_cgs) {
        const float dv_phys = 2.f * sqrtf(
                                  bh_props->xray_kinetic_fraction * 
                                  du_xray_phys
                              );
        const float dv_comoving = dv_phys * cosmo->a;
        const float prefactor = dv_comoving / r;

        /* Push gas radially */
        pj->v[0] += prefactor * dx[0];
        pj->v[1] += prefactor * dx[1];
        pj->v[2] += prefactor * dx[2];

        du_xray_phys *= (1. - bh_props->xray_kinetic_fraction);

        /* Update the signal velocity of the particle based on the velocity kick. */
        hydro_set_v_sig_based_on_velocity_kick(pj, cosmo, dv_phys);
        message("BH_XRAY_KICK: dv_phys(km/s)=%g",
                dv_phys / bh_props->kms_to_internal);
      } 

      const double u_new = u_init + du_xray_phys;

      /* Do the energy injection. */
      hydro_set_physical_internal_energy(pj, xpj, cosmo, u_new);
      hydro_set_drifted_physical_internal_energy(pj, cosmo, u_new);

      /* Synchronize the particle on the timeline */
      timestep_sync_part(pj);
    }
  }
}

#endif /* SWIFT_YAM_BH_IACT_H */

/**
 * @file info_actors.h
 * @brief Define the player, monsters, projectiles, and effects in enum order.
 */

//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
    {
        // MT_PLAYER
        -1,                                                              // doomednum
        S_PLAY,                                                          // spawnstate
        100,                                                             // spawnhealth
        S_PLAY_RUN1,                                                     // seestate
        sfx_None,                                                        // seesound
        0,                                                               // reactiontime
        sfx_None,                                                        // attacksound
        S_PLAY_PAIN,                                                     // painstate
        255,                                                             // painchance
        sfx_plpain,                                                      // painsound
        S_NULL,                                                          // meleestate
        S_PLAY_ATK1,                                                     // missilestate
        S_PLAY_DIE1,                                                     // deathstate
        S_PLAY_XDIE1,                                                    // xdeathstate
        sfx_pldeth,                                                      // deathsound
        0,                                                               // speed
        16 * FRACUNIT,                                                   // radius
        56 * FRACUNIT,                                                   // height
        100,                                                             // mass
        0,                                                               // damage
        sfx_None,                                                        // activesound
        MF_SOLID | MF_SHOOTABLE | MF_DROPOFF | MF_PICKUP | MF_NOTDMATCH, // flags
        S_NULL                                                           // raisestate
    },

    {
        // MT_POSSESSED
        3004,                                   // doomednum
        S_POSS_STND,                            // spawnstate
        20,                                     // spawnhealth
        S_POSS_RUN1,                            // seestate
        sfx_posit1,                             // seesound
        8,                                      // reactiontime
        sfx_pistol,                             // attacksound
        S_POSS_PAIN,                            // painstate
        200,                                    // painchance
        sfx_popain,                             // painsound
        0,                                      // meleestate
        S_POSS_ATK1,                            // missilestate
        S_POSS_DIE1,                            // deathstate
        S_POSS_XDIE1,                           // xdeathstate
        sfx_podth1,                             // deathsound
        8,                                      // speed
        20 * FRACUNIT,                          // radius
        56 * FRACUNIT,                          // height
        100,                                    // mass
        0,                                      // damage
        sfx_posact,                             // activesound
        MF_SOLID | MF_SHOOTABLE | MF_COUNTKILL, // flags
        S_POSS_RAISE1                           // raisestate
    },

    {
        // MT_SHOTGUY
        9,                                      // doomednum
        S_SPOS_STND,                            // spawnstate
        30,                                     // spawnhealth
        S_SPOS_RUN1,                            // seestate
        sfx_posit2,                             // seesound
        8,                                      // reactiontime
        0,                                      // attacksound
        S_SPOS_PAIN,                            // painstate
        170,                                    // painchance
        sfx_popain,                             // painsound
        0,                                      // meleestate
        S_SPOS_ATK1,                            // missilestate
        S_SPOS_DIE1,                            // deathstate
        S_SPOS_XDIE1,                           // xdeathstate
        sfx_podth2,                             // deathsound
        8,                                      // speed
        20 * FRACUNIT,                          // radius
        56 * FRACUNIT,                          // height
        100,                                    // mass
        0,                                      // damage
        sfx_posact,                             // activesound
        MF_SOLID | MF_SHOOTABLE | MF_COUNTKILL, // flags
        S_SPOS_RAISE1                           // raisestate
    },

    {
        // MT_VILE
        64,                                     // doomednum
        S_VILE_STND,                            // spawnstate
        700,                                    // spawnhealth
        S_VILE_RUN1,                            // seestate
        sfx_vilsit,                             // seesound
        8,                                      // reactiontime
        0,                                      // attacksound
        S_VILE_PAIN,                            // painstate
        10,                                     // painchance
        sfx_vipain,                             // painsound
        0,                                      // meleestate
        S_VILE_ATK1,                            // missilestate
        S_VILE_DIE1,                            // deathstate
        S_NULL,                                 // xdeathstate
        sfx_vildth,                             // deathsound
        15,                                     // speed
        20 * FRACUNIT,                          // radius
        56 * FRACUNIT,                          // height
        500,                                    // mass
        0,                                      // damage
        sfx_vilact,                             // activesound
        MF_SOLID | MF_SHOOTABLE | MF_COUNTKILL, // flags
        S_NULL                                  // raisestate
    },

    {
        // MT_FIRE
        -1,                           // doomednum
        S_FIRE1,                      // spawnstate
        1000,                         // spawnhealth
        S_NULL,                       // seestate
        sfx_None,                     // seesound
        8,                            // reactiontime
        sfx_None,                     // attacksound
        S_NULL,                       // painstate
        0,                            // painchance
        sfx_None,                     // painsound
        S_NULL,                       // meleestate
        S_NULL,                       // missilestate
        S_NULL,                       // deathstate
        S_NULL,                       // xdeathstate
        sfx_None,                     // deathsound
        0,                            // speed
        20 * FRACUNIT,                // radius
        16 * FRACUNIT,                // height
        100,                          // mass
        0,                            // damage
        sfx_None,                     // activesound
        MF_NOBLOCKMAP | MF_NOGRAVITY, // flags
        S_NULL                        // raisestate
    },

    {
        // MT_UNDEAD
        66,                                     // doomednum
        S_SKEL_STND,                            // spawnstate
        300,                                    // spawnhealth
        S_SKEL_RUN1,                            // seestate
        sfx_skesit,                             // seesound
        8,                                      // reactiontime
        0,                                      // attacksound
        S_SKEL_PAIN,                            // painstate
        100,                                    // painchance
        sfx_popain,                             // painsound
        S_SKEL_FIST1,                           // meleestate
        S_SKEL_MISS1,                           // missilestate
        S_SKEL_DIE1,                            // deathstate
        S_NULL,                                 // xdeathstate
        sfx_skedth,                             // deathsound
        10,                                     // speed
        20 * FRACUNIT,                          // radius
        56 * FRACUNIT,                          // height
        500,                                    // mass
        0,                                      // damage
        sfx_skeact,                             // activesound
        MF_SOLID | MF_SHOOTABLE | MF_COUNTKILL, // flags
        S_SKEL_RAISE1                           // raisestate
    },

    {
        // MT_TRACER
        -1,                                                     // doomednum
        S_TRACER,                                               // spawnstate
        1000,                                                   // spawnhealth
        S_NULL,                                                 // seestate
        sfx_skeatk,                                             // seesound
        8,                                                      // reactiontime
        sfx_None,                                               // attacksound
        S_NULL,                                                 // painstate
        0,                                                      // painchance
        sfx_None,                                               // painsound
        S_NULL,                                                 // meleestate
        S_NULL,                                                 // missilestate
        S_TRACEEXP1,                                            // deathstate
        S_NULL,                                                 // xdeathstate
        sfx_barexp,                                             // deathsound
        10 * FRACUNIT,                                          // speed
        11 * FRACUNIT,                                          // radius
        8 * FRACUNIT,                                           // height
        100,                                                    // mass
        10,                                                     // damage
        sfx_None,                                               // activesound
        MF_NOBLOCKMAP | MF_MISSILE | MF_DROPOFF | MF_NOGRAVITY, // flags
        S_NULL                                                  // raisestate
    },

    {
        // MT_SMOKE
        -1,                           // doomednum
        S_SMOKE1,                     // spawnstate
        1000,                         // spawnhealth
        S_NULL,                       // seestate
        sfx_None,                     // seesound
        8,                            // reactiontime
        sfx_None,                     // attacksound
        S_NULL,                       // painstate
        0,                            // painchance
        sfx_None,                     // painsound
        S_NULL,                       // meleestate
        S_NULL,                       // missilestate
        S_NULL,                       // deathstate
        S_NULL,                       // xdeathstate
        sfx_None,                     // deathsound
        0,                            // speed
        20 * FRACUNIT,                // radius
        16 * FRACUNIT,                // height
        100,                          // mass
        0,                            // damage
        sfx_None,                     // activesound
        MF_NOBLOCKMAP | MF_NOGRAVITY, // flags
        S_NULL                        // raisestate
    },

    {
        // MT_FATSO
        67,                                     // doomednum
        S_FATT_STND,                            // spawnstate
        600,                                    // spawnhealth
        S_FATT_RUN1,                            // seestate
        sfx_mansit,                             // seesound
        8,                                      // reactiontime
        0,                                      // attacksound
        S_FATT_PAIN,                            // painstate
        80,                                     // painchance
        sfx_mnpain,                             // painsound
        0,                                      // meleestate
        S_FATT_ATK1,                            // missilestate
        S_FATT_DIE1,                            // deathstate
        S_NULL,                                 // xdeathstate
        sfx_mandth,                             // deathsound
        8,                                      // speed
        48 * FRACUNIT,                          // radius
        64 * FRACUNIT,                          // height
        1000,                                   // mass
        0,                                      // damage
        sfx_posact,                             // activesound
        MF_SOLID | MF_SHOOTABLE | MF_COUNTKILL, // flags
        S_FATT_RAISE1                           // raisestate
    },

    {
        // MT_FATSHOT
        -1,                                                     // doomednum
        S_FATSHOT1,                                             // spawnstate
        1000,                                                   // spawnhealth
        S_NULL,                                                 // seestate
        sfx_firsht,                                             // seesound
        8,                                                      // reactiontime
        sfx_None,                                               // attacksound
        S_NULL,                                                 // painstate
        0,                                                      // painchance
        sfx_None,                                               // painsound
        S_NULL,                                                 // meleestate
        S_NULL,                                                 // missilestate
        S_FATSHOTX1,                                            // deathstate
        S_NULL,                                                 // xdeathstate
        sfx_firxpl,                                             // deathsound
        20 * FRACUNIT,                                          // speed
        6 * FRACUNIT,                                           // radius
        8 * FRACUNIT,                                           // height
        100,                                                    // mass
        8,                                                      // damage
        sfx_None,                                               // activesound
        MF_NOBLOCKMAP | MF_MISSILE | MF_DROPOFF | MF_NOGRAVITY, // flags
        S_NULL                                                  // raisestate
    },

    {
        // MT_CHAINGUY
        65,                                     // doomednum
        S_CPOS_STND,                            // spawnstate
        70,                                     // spawnhealth
        S_CPOS_RUN1,                            // seestate
        sfx_posit2,                             // seesound
        8,                                      // reactiontime
        0,                                      // attacksound
        S_CPOS_PAIN,                            // painstate
        170,                                    // painchance
        sfx_popain,                             // painsound
        0,                                      // meleestate
        S_CPOS_ATK1,                            // missilestate
        S_CPOS_DIE1,                            // deathstate
        S_CPOS_XDIE1,                           // xdeathstate
        sfx_podth2,                             // deathsound
        8,                                      // speed
        20 * FRACUNIT,                          // radius
        56 * FRACUNIT,                          // height
        100,                                    // mass
        0,                                      // damage
        sfx_posact,                             // activesound
        MF_SOLID | MF_SHOOTABLE | MF_COUNTKILL, // flags
        S_CPOS_RAISE1                           // raisestate
    },

    {
        // MT_TROOP
        3001,                                   // doomednum
        S_TROO_STND,                            // spawnstate
        60,                                     // spawnhealth
        S_TROO_RUN1,                            // seestate
        sfx_bgsit1,                             // seesound
        8,                                      // reactiontime
        0,                                      // attacksound
        S_TROO_PAIN,                            // painstate
        200,                                    // painchance
        sfx_popain,                             // painsound
        S_TROO_ATK1,                            // meleestate
        S_TROO_ATK1,                            // missilestate
        S_TROO_DIE1,                            // deathstate
        S_TROO_XDIE1,                           // xdeathstate
        sfx_bgdth1,                             // deathsound
        8,                                      // speed
        20 * FRACUNIT,                          // radius
        56 * FRACUNIT,                          // height
        100,                                    // mass
        0,                                      // damage
        sfx_bgact,                              // activesound
        MF_SOLID | MF_SHOOTABLE | MF_COUNTKILL, // flags
        S_TROO_RAISE1                           // raisestate
    },

    {
        // MT_SERGEANT
        3002,                                   // doomednum
        S_SARG_STND,                            // spawnstate
        150,                                    // spawnhealth
        S_SARG_RUN1,                            // seestate
        sfx_sgtsit,                             // seesound
        8,                                      // reactiontime
        sfx_sgtatk,                             // attacksound
        S_SARG_PAIN,                            // painstate
        180,                                    // painchance
        sfx_dmpain,                             // painsound
        S_SARG_ATK1,                            // meleestate
        0,                                      // missilestate
        S_SARG_DIE1,                            // deathstate
        S_NULL,                                 // xdeathstate
        sfx_sgtdth,                             // deathsound
        10,                                     // speed
        30 * FRACUNIT,                          // radius
        56 * FRACUNIT,                          // height
        400,                                    // mass
        0,                                      // damage
        sfx_dmact,                              // activesound
        MF_SOLID | MF_SHOOTABLE | MF_COUNTKILL, // flags
        S_SARG_RAISE1                           // raisestate
    },

    {
        // MT_SHADOWS
        58,                                                 // doomednum
        S_SARG_STND,                                        // spawnstate
        150,                                                // spawnhealth
        S_SARG_RUN1,                                        // seestate
        sfx_sgtsit,                                         // seesound
        8,                                                  // reactiontime
        sfx_sgtatk,                                         // attacksound
        S_SARG_PAIN,                                        // painstate
        180,                                                // painchance
        sfx_dmpain,                                         // painsound
        S_SARG_ATK1,                                        // meleestate
        0,                                                  // missilestate
        S_SARG_DIE1,                                        // deathstate
        S_NULL,                                             // xdeathstate
        sfx_sgtdth,                                         // deathsound
        10,                                                 // speed
        30 * FRACUNIT,                                      // radius
        56 * FRACUNIT,                                      // height
        400,                                                // mass
        0,                                                  // damage
        sfx_dmact,                                          // activesound
        MF_SOLID | MF_SHOOTABLE | MF_SHADOW | MF_COUNTKILL, // flags
        S_SARG_RAISE1                                       // raisestate
    },

    {
        // MT_HEAD
        3005,                                                             // doomednum
        S_HEAD_STND,                                                      // spawnstate
        400,                                                              // spawnhealth
        S_HEAD_RUN1,                                                      // seestate
        sfx_cacsit,                                                       // seesound
        8,                                                                // reactiontime
        0,                                                                // attacksound
        S_HEAD_PAIN,                                                      // painstate
        128,                                                              // painchance
        sfx_dmpain,                                                       // painsound
        0,                                                                // meleestate
        S_HEAD_ATK1,                                                      // missilestate
        S_HEAD_DIE1,                                                      // deathstate
        S_NULL,                                                           // xdeathstate
        sfx_cacdth,                                                       // deathsound
        8,                                                                // speed
        31 * FRACUNIT,                                                    // radius
        56 * FRACUNIT,                                                    // height
        400,                                                              // mass
        0,                                                                // damage
        sfx_dmact,                                                        // activesound
        MF_SOLID | MF_SHOOTABLE | MF_FLOAT | MF_NOGRAVITY | MF_COUNTKILL, // flags
        S_HEAD_RAISE1                                                     // raisestate
    },

    {
        // MT_BRUISER
        3003,                                   // doomednum
        S_BOSS_STND,                            // spawnstate
        1000,                                   // spawnhealth
        S_BOSS_RUN1,                            // seestate
        sfx_brssit,                             // seesound
        8,                                      // reactiontime
        0,                                      // attacksound
        S_BOSS_PAIN,                            // painstate
        50,                                     // painchance
        sfx_dmpain,                             // painsound
        S_BOSS_ATK1,                            // meleestate
        S_BOSS_ATK1,                            // missilestate
        S_BOSS_DIE1,                            // deathstate
        S_NULL,                                 // xdeathstate
        sfx_brsdth,                             // deathsound
        8,                                      // speed
        24 * FRACUNIT,                          // radius
        64 * FRACUNIT,                          // height
        1000,                                   // mass
        0,                                      // damage
        sfx_dmact,                              // activesound
        MF_SOLID | MF_SHOOTABLE | MF_COUNTKILL, // flags
        S_BOSS_RAISE1                           // raisestate
    },

    {
        // MT_BRUISERSHOT
        -1,                                                     // doomednum
        S_BRBALL1,                                              // spawnstate
        1000,                                                   // spawnhealth
        S_NULL,                                                 // seestate
        sfx_firsht,                                             // seesound
        8,                                                      // reactiontime
        sfx_None,                                               // attacksound
        S_NULL,                                                 // painstate
        0,                                                      // painchance
        sfx_None,                                               // painsound
        S_NULL,                                                 // meleestate
        S_NULL,                                                 // missilestate
        S_BRBALLX1,                                             // deathstate
        S_NULL,                                                 // xdeathstate
        sfx_firxpl,                                             // deathsound
        15 * FRACUNIT,                                          // speed
        6 * FRACUNIT,                                           // radius
        8 * FRACUNIT,                                           // height
        100,                                                    // mass
        8,                                                      // damage
        sfx_None,                                               // activesound
        MF_NOBLOCKMAP | MF_MISSILE | MF_DROPOFF | MF_NOGRAVITY, // flags
        S_NULL                                                  // raisestate
    },

    {
        // MT_KNIGHT
        69,                                     // doomednum
        S_BOS2_STND,                            // spawnstate
        500,                                    // spawnhealth
        S_BOS2_RUN1,                            // seestate
        sfx_kntsit,                             // seesound
        8,                                      // reactiontime
        0,                                      // attacksound
        S_BOS2_PAIN,                            // painstate
        50,                                     // painchance
        sfx_dmpain,                             // painsound
        S_BOS2_ATK1,                            // meleestate
        S_BOS2_ATK1,                            // missilestate
        S_BOS2_DIE1,                            // deathstate
        S_NULL,                                 // xdeathstate
        sfx_kntdth,                             // deathsound
        8,                                      // speed
        24 * FRACUNIT,                          // radius
        64 * FRACUNIT,                          // height
        1000,                                   // mass
        0,                                      // damage
        sfx_dmact,                              // activesound
        MF_SOLID | MF_SHOOTABLE | MF_COUNTKILL, // flags
        S_BOS2_RAISE1                           // raisestate
    },

    {
        // MT_SKULL
        3006,                                              // doomednum
        S_SKULL_STND,                                      // spawnstate
        100,                                               // spawnhealth
        S_SKULL_RUN1,                                      // seestate
        0,                                                 // seesound
        8,                                                 // reactiontime
        sfx_sklatk,                                        // attacksound
        S_SKULL_PAIN,                                      // painstate
        256,                                               // painchance
        sfx_dmpain,                                        // painsound
        0,                                                 // meleestate
        S_SKULL_ATK1,                                      // missilestate
        S_SKULL_DIE1,                                      // deathstate
        S_NULL,                                            // xdeathstate
        sfx_firxpl,                                        // deathsound
        8,                                                 // speed
        16 * FRACUNIT,                                     // radius
        56 * FRACUNIT,                                     // height
        50,                                                // mass
        3,                                                 // damage
        sfx_dmact,                                         // activesound
        MF_SOLID | MF_SHOOTABLE | MF_FLOAT | MF_NOGRAVITY, // flags
        S_NULL                                             // raisestate
    },

    {
        // MT_SPIDER
        7,                                      // doomednum
        S_SPID_STND,                            // spawnstate
        3000,                                   // spawnhealth
        S_SPID_RUN1,                            // seestate
        sfx_spisit,                             // seesound
        8,                                      // reactiontime
        sfx_shotgn,                             // attacksound
        S_SPID_PAIN,                            // painstate
        40,                                     // painchance
        sfx_dmpain,                             // painsound
        0,                                      // meleestate
        S_SPID_ATK1,                            // missilestate
        S_SPID_DIE1,                            // deathstate
        S_NULL,                                 // xdeathstate
        sfx_spidth,                             // deathsound
        12,                                     // speed
        128 * FRACUNIT,                         // radius
        100 * FRACUNIT,                         // height
        1000,                                   // mass
        0,                                      // damage
        sfx_dmact,                              // activesound
        MF_SOLID | MF_SHOOTABLE | MF_COUNTKILL, // flags
        S_NULL                                  // raisestate
    },

    {
        // MT_BABY
        68,                                     // doomednum
        S_BSPI_STND,                            // spawnstate
        500,                                    // spawnhealth
        S_BSPI_SIGHT,                           // seestate
        sfx_bspsit,                             // seesound
        8,                                      // reactiontime
        0,                                      // attacksound
        S_BSPI_PAIN,                            // painstate
        128,                                    // painchance
        sfx_dmpain,                             // painsound
        0,                                      // meleestate
        S_BSPI_ATK1,                            // missilestate
        S_BSPI_DIE1,                            // deathstate
        S_NULL,                                 // xdeathstate
        sfx_bspdth,                             // deathsound
        12,                                     // speed
        64 * FRACUNIT,                          // radius
        64 * FRACUNIT,                          // height
        600,                                    // mass
        0,                                      // damage
        sfx_bspact,                             // activesound
        MF_SOLID | MF_SHOOTABLE | MF_COUNTKILL, // flags
        S_BSPI_RAISE1                           // raisestate
    },

    {
        // MT_CYBORG
        16,                                     // doomednum
        S_CYBER_STND,                           // spawnstate
        4000,                                   // spawnhealth
        S_CYBER_RUN1,                           // seestate
        sfx_cybsit,                             // seesound
        8,                                      // reactiontime
        0,                                      // attacksound
        S_CYBER_PAIN,                           // painstate
        20,                                     // painchance
        sfx_dmpain,                             // painsound
        0,                                      // meleestate
        S_CYBER_ATK1,                           // missilestate
        S_CYBER_DIE1,                           // deathstate
        S_NULL,                                 // xdeathstate
        sfx_cybdth,                             // deathsound
        16,                                     // speed
        40 * FRACUNIT,                          // radius
        110 * FRACUNIT,                         // height
        1000,                                   // mass
        0,                                      // damage
        sfx_dmact,                              // activesound
        MF_SOLID | MF_SHOOTABLE | MF_COUNTKILL, // flags
        S_NULL                                  // raisestate
    },

    {
        // MT_PAIN
        71,                                                               // doomednum
        S_PAIN_STND,                                                      // spawnstate
        400,                                                              // spawnhealth
        S_PAIN_RUN1,                                                      // seestate
        sfx_pesit,                                                        // seesound
        8,                                                                // reactiontime
        0,                                                                // attacksound
        S_PAIN_PAIN,                                                      // painstate
        128,                                                              // painchance
        sfx_pepain,                                                       // painsound
        0,                                                                // meleestate
        S_PAIN_ATK1,                                                      // missilestate
        S_PAIN_DIE1,                                                      // deathstate
        S_NULL,                                                           // xdeathstate
        sfx_pedth,                                                        // deathsound
        8,                                                                // speed
        31 * FRACUNIT,                                                    // radius
        56 * FRACUNIT,                                                    // height
        400,                                                              // mass
        0,                                                                // damage
        sfx_dmact,                                                        // activesound
        MF_SOLID | MF_SHOOTABLE | MF_FLOAT | MF_NOGRAVITY | MF_COUNTKILL, // flags
        S_PAIN_RAISE1                                                     // raisestate
    },

    {
        // MT_WOLFSS
        84,                                     // doomednum
        S_SSWV_STND,                            // spawnstate
        50,                                     // spawnhealth
        S_SSWV_RUN1,                            // seestate
        sfx_sssit,                              // seesound
        8,                                      // reactiontime
        0,                                      // attacksound
        S_SSWV_PAIN,                            // painstate
        170,                                    // painchance
        sfx_popain,                             // painsound
        0,                                      // meleestate
        S_SSWV_ATK1,                            // missilestate
        S_SSWV_DIE1,                            // deathstate
        S_SSWV_XDIE1,                           // xdeathstate
        sfx_ssdth,                              // deathsound
        8,                                      // speed
        20 * FRACUNIT,                          // radius
        56 * FRACUNIT,                          // height
        100,                                    // mass
        0,                                      // damage
        sfx_posact,                             // activesound
        MF_SOLID | MF_SHOOTABLE | MF_COUNTKILL, // flags
        S_SSWV_RAISE1                           // raisestate
    },

    {
        // MT_KEEN
        72,                                                                      // doomednum
        S_KEENSTND,                                                              // spawnstate
        100,                                                                     // spawnhealth
        S_NULL,                                                                  // seestate
        sfx_None,                                                                // seesound
        8,                                                                       // reactiontime
        sfx_None,                                                                // attacksound
        S_KEENPAIN,                                                              // painstate
        256,                                                                     // painchance
        sfx_keenpn,                                                              // painsound
        S_NULL,                                                                  // meleestate
        S_NULL,                                                                  // missilestate
        S_COMMKEEN,                                                              // deathstate
        S_NULL,                                                                  // xdeathstate
        sfx_keendt,                                                              // deathsound
        0,                                                                       // speed
        16 * FRACUNIT,                                                           // radius
        72 * FRACUNIT,                                                           // height
        10000000,                                                                // mass
        0,                                                                       // damage
        sfx_None,                                                                // activesound
        MF_SOLID | MF_SPAWNCEILING | MF_NOGRAVITY | MF_SHOOTABLE | MF_COUNTKILL, // flags
        S_NULL                                                                   // raisestate
    },

    {
        // MT_BOSSBRAIN
        88,                      // doomednum
        S_BRAIN,                 // spawnstate
        250,                     // spawnhealth
        S_NULL,                  // seestate
        sfx_None,                // seesound
        8,                       // reactiontime
        sfx_None,                // attacksound
        S_BRAIN_PAIN,            // painstate
        255,                     // painchance
        sfx_bospn,               // painsound
        S_NULL,                  // meleestate
        S_NULL,                  // missilestate
        S_BRAIN_DIE1,            // deathstate
        S_NULL,                  // xdeathstate
        sfx_bosdth,              // deathsound
        0,                       // speed
        16 * FRACUNIT,           // radius
        16 * FRACUNIT,           // height
        10000000,                // mass
        0,                       // damage
        sfx_None,                // activesound
        MF_SOLID | MF_SHOOTABLE, // flags
        S_NULL                   // raisestate
    },

    {
        // MT_BOSSSPIT
        89,                          // doomednum
        S_BRAINEYE,                  // spawnstate
        1000,                        // spawnhealth
        S_BRAINEYESEE,               // seestate
        sfx_None,                    // seesound
        8,                           // reactiontime
        sfx_None,                    // attacksound
        S_NULL,                      // painstate
        0,                           // painchance
        sfx_None,                    // painsound
        S_NULL,                      // meleestate
        S_NULL,                      // missilestate
        S_NULL,                      // deathstate
        S_NULL,                      // xdeathstate
        sfx_None,                    // deathsound
        0,                           // speed
        20 * FRACUNIT,               // radius
        32 * FRACUNIT,               // height
        100,                         // mass
        0,                           // damage
        sfx_None,                    // activesound
        MF_NOBLOCKMAP | MF_NOSECTOR, // flags
        S_NULL                       // raisestate
    },

    {
        // MT_BOSSTARGET
        87,                          // doomednum
        S_NULL,                      // spawnstate
        1000,                        // spawnhealth
        S_NULL,                      // seestate
        sfx_None,                    // seesound
        8,                           // reactiontime
        sfx_None,                    // attacksound
        S_NULL,                      // painstate
        0,                           // painchance
        sfx_None,                    // painsound
        S_NULL,                      // meleestate
        S_NULL,                      // missilestate
        S_NULL,                      // deathstate
        S_NULL,                      // xdeathstate
        sfx_None,                    // deathsound
        0,                           // speed
        20 * FRACUNIT,               // radius
        32 * FRACUNIT,               // height
        100,                         // mass
        0,                           // damage
        sfx_None,                    // activesound
        MF_NOBLOCKMAP | MF_NOSECTOR, // flags
        S_NULL                       // raisestate
    },

    {
        // MT_SPAWNSHOT
        -1,                                                                 // doomednum
        S_SPAWN1,                                                           // spawnstate
        1000,                                                               // spawnhealth
        S_NULL,                                                             // seestate
        sfx_bospit,                                                         // seesound
        8,                                                                  // reactiontime
        sfx_None,                                                           // attacksound
        S_NULL,                                                             // painstate
        0,                                                                  // painchance
        sfx_None,                                                           // painsound
        S_NULL,                                                             // meleestate
        S_NULL,                                                             // missilestate
        S_NULL,                                                             // deathstate
        S_NULL,                                                             // xdeathstate
        sfx_firxpl,                                                         // deathsound
        10 * FRACUNIT,                                                      // speed
        6 * FRACUNIT,                                                       // radius
        32 * FRACUNIT,                                                      // height
        100,                                                                // mass
        3,                                                                  // damage
        sfx_None,                                                           // activesound
        MF_NOBLOCKMAP | MF_MISSILE | MF_DROPOFF | MF_NOGRAVITY | MF_NOCLIP, // flags
        S_NULL                                                              // raisestate
    },

    {
        // MT_SPAWNFIRE
        -1,                           // doomednum
        S_SPAWNFIRE1,                 // spawnstate
        1000,                         // spawnhealth
        S_NULL,                       // seestate
        sfx_None,                     // seesound
        8,                            // reactiontime
        sfx_None,                     // attacksound
        S_NULL,                       // painstate
        0,                            // painchance
        sfx_None,                     // painsound
        S_NULL,                       // meleestate
        S_NULL,                       // missilestate
        S_NULL,                       // deathstate
        S_NULL,                       // xdeathstate
        sfx_None,                     // deathsound
        0,                            // speed
        20 * FRACUNIT,                // radius
        16 * FRACUNIT,                // height
        100,                          // mass
        0,                            // damage
        sfx_None,                     // activesound
        MF_NOBLOCKMAP | MF_NOGRAVITY, // flags
        S_NULL                        // raisestate
    },

    {
        // MT_BARREL
        2035,                                 // doomednum
        S_BAR1,                               // spawnstate
        20,                                   // spawnhealth
        S_NULL,                               // seestate
        sfx_None,                             // seesound
        8,                                    // reactiontime
        sfx_None,                             // attacksound
        S_NULL,                               // painstate
        0,                                    // painchance
        sfx_None,                             // painsound
        S_NULL,                               // meleestate
        S_NULL,                               // missilestate
        S_BEXP,                               // deathstate
        S_NULL,                               // xdeathstate
        sfx_barexp,                           // deathsound
        0,                                    // speed
        10 * FRACUNIT,                        // radius
        42 * FRACUNIT,                        // height
        100,                                  // mass
        0,                                    // damage
        sfx_None,                             // activesound
        MF_SOLID | MF_SHOOTABLE | MF_NOBLOOD, // flags
        S_NULL                                // raisestate
    },

    {
        // MT_TROOPSHOT
        -1,                                                     // doomednum
        S_TBALL1,                                               // spawnstate
        1000,                                                   // spawnhealth
        S_NULL,                                                 // seestate
        sfx_firsht,                                             // seesound
        8,                                                      // reactiontime
        sfx_None,                                               // attacksound
        S_NULL,                                                 // painstate
        0,                                                      // painchance
        sfx_None,                                               // painsound
        S_NULL,                                                 // meleestate
        S_NULL,                                                 // missilestate
        S_TBALLX1,                                              // deathstate
        S_NULL,                                                 // xdeathstate
        sfx_firxpl,                                             // deathsound
        10 * FRACUNIT,                                          // speed
        6 * FRACUNIT,                                           // radius
        8 * FRACUNIT,                                           // height
        100,                                                    // mass
        3,                                                      // damage
        sfx_None,                                               // activesound
        MF_NOBLOCKMAP | MF_MISSILE | MF_DROPOFF | MF_NOGRAVITY, // flags
        S_NULL                                                  // raisestate
    },

    {
        // MT_HEADSHOT
        -1,                                                     // doomednum
        S_RBALL1,                                               // spawnstate
        1000,                                                   // spawnhealth
        S_NULL,                                                 // seestate
        sfx_firsht,                                             // seesound
        8,                                                      // reactiontime
        sfx_None,                                               // attacksound
        S_NULL,                                                 // painstate
        0,                                                      // painchance
        sfx_None,                                               // painsound
        S_NULL,                                                 // meleestate
        S_NULL,                                                 // missilestate
        S_RBALLX1,                                              // deathstate
        S_NULL,                                                 // xdeathstate
        sfx_firxpl,                                             // deathsound
        10 * FRACUNIT,                                          // speed
        6 * FRACUNIT,                                           // radius
        8 * FRACUNIT,                                           // height
        100,                                                    // mass
        5,                                                      // damage
        sfx_None,                                               // activesound
        MF_NOBLOCKMAP | MF_MISSILE | MF_DROPOFF | MF_NOGRAVITY, // flags
        S_NULL                                                  // raisestate
    },

    {
        // MT_ROCKET
        -1,                                                     // doomednum
        S_ROCKET,                                               // spawnstate
        1000,                                                   // spawnhealth
        S_NULL,                                                 // seestate
        sfx_rlaunc,                                             // seesound
        8,                                                      // reactiontime
        sfx_None,                                               // attacksound
        S_NULL,                                                 // painstate
        0,                                                      // painchance
        sfx_None,                                               // painsound
        S_NULL,                                                 // meleestate
        S_NULL,                                                 // missilestate
        S_EXPLODE1,                                             // deathstate
        S_NULL,                                                 // xdeathstate
        sfx_barexp,                                             // deathsound
        20 * FRACUNIT,                                          // speed
        11 * FRACUNIT,                                          // radius
        8 * FRACUNIT,                                           // height
        100,                                                    // mass
        20,                                                     // damage
        sfx_None,                                               // activesound
        MF_NOBLOCKMAP | MF_MISSILE | MF_DROPOFF | MF_NOGRAVITY, // flags
        S_NULL                                                  // raisestate
    },

    {
        // MT_PLASMA
        -1,                                                     // doomednum
        S_PLASBALL,                                             // spawnstate
        1000,                                                   // spawnhealth
        S_NULL,                                                 // seestate
        sfx_plasma,                                             // seesound
        8,                                                      // reactiontime
        sfx_None,                                               // attacksound
        S_NULL,                                                 // painstate
        0,                                                      // painchance
        sfx_None,                                               // painsound
        S_NULL,                                                 // meleestate
        S_NULL,                                                 // missilestate
        S_PLASEXP,                                              // deathstate
        S_NULL,                                                 // xdeathstate
        sfx_firxpl,                                             // deathsound
        25 * FRACUNIT,                                          // speed
        13 * FRACUNIT,                                          // radius
        8 * FRACUNIT,                                           // height
        100,                                                    // mass
        5,                                                      // damage
        sfx_None,                                               // activesound
        MF_NOBLOCKMAP | MF_MISSILE | MF_DROPOFF | MF_NOGRAVITY, // flags
        S_NULL                                                  // raisestate
    },

    {
        // MT_BFG
        -1,                                                     // doomednum
        S_BFGSHOT,                                              // spawnstate
        1000,                                                   // spawnhealth
        S_NULL,                                                 // seestate
        0,                                                      // seesound
        8,                                                      // reactiontime
        sfx_None,                                               // attacksound
        S_NULL,                                                 // painstate
        0,                                                      // painchance
        sfx_None,                                               // painsound
        S_NULL,                                                 // meleestate
        S_NULL,                                                 // missilestate
        S_BFGLAND,                                              // deathstate
        S_NULL,                                                 // xdeathstate
        sfx_rxplod,                                             // deathsound
        25 * FRACUNIT,                                          // speed
        13 * FRACUNIT,                                          // radius
        8 * FRACUNIT,                                           // height
        100,                                                    // mass
        100,                                                    // damage
        sfx_None,                                               // activesound
        MF_NOBLOCKMAP | MF_MISSILE | MF_DROPOFF | MF_NOGRAVITY, // flags
        S_NULL                                                  // raisestate
    },

    {
        // MT_ARACHPLAZ
        -1,                                                     // doomednum
        S_ARACH_PLAZ,                                           // spawnstate
        1000,                                                   // spawnhealth
        S_NULL,                                                 // seestate
        sfx_plasma,                                             // seesound
        8,                                                      // reactiontime
        sfx_None,                                               // attacksound
        S_NULL,                                                 // painstate
        0,                                                      // painchance
        sfx_None,                                               // painsound
        S_NULL,                                                 // meleestate
        S_NULL,                                                 // missilestate
        S_ARACH_PLEX,                                           // deathstate
        S_NULL,                                                 // xdeathstate
        sfx_firxpl,                                             // deathsound
        25 * FRACUNIT,                                          // speed
        13 * FRACUNIT,                                          // radius
        8 * FRACUNIT,                                           // height
        100,                                                    // mass
        5,                                                      // damage
        sfx_None,                                               // activesound
        MF_NOBLOCKMAP | MF_MISSILE | MF_DROPOFF | MF_NOGRAVITY, // flags
        S_NULL                                                  // raisestate
    },

    {
        // MT_PUFF
        -1,                           // doomednum
        S_PUFF1,                      // spawnstate
        1000,                         // spawnhealth
        S_NULL,                       // seestate
        sfx_None,                     // seesound
        8,                            // reactiontime
        sfx_None,                     // attacksound
        S_NULL,                       // painstate
        0,                            // painchance
        sfx_None,                     // painsound
        S_NULL,                       // meleestate
        S_NULL,                       // missilestate
        S_NULL,                       // deathstate
        S_NULL,                       // xdeathstate
        sfx_None,                     // deathsound
        0,                            // speed
        20 * FRACUNIT,                // radius
        16 * FRACUNIT,                // height
        100,                          // mass
        0,                            // damage
        sfx_None,                     // activesound
        MF_NOBLOCKMAP | MF_NOGRAVITY, // flags
        S_NULL                        // raisestate
    },

    {
        // MT_BLOOD
        -1,            // doomednum
        S_BLOOD1,      // spawnstate
        1000,          // spawnhealth
        S_NULL,        // seestate
        sfx_None,      // seesound
        8,             // reactiontime
        sfx_None,      // attacksound
        S_NULL,        // painstate
        0,             // painchance
        sfx_None,      // painsound
        S_NULL,        // meleestate
        S_NULL,        // missilestate
        S_NULL,        // deathstate
        S_NULL,        // xdeathstate
        sfx_None,      // deathsound
        0,             // speed
        20 * FRACUNIT, // radius
        16 * FRACUNIT, // height
        100,           // mass
        0,             // damage
        sfx_None,      // activesound
        MF_NOBLOCKMAP, // flags
        S_NULL         // raisestate
    },

    {
        // MT_TFOG
        -1,                           // doomednum
        S_TFOG,                       // spawnstate
        1000,                         // spawnhealth
        S_NULL,                       // seestate
        sfx_None,                     // seesound
        8,                            // reactiontime
        sfx_None,                     // attacksound
        S_NULL,                       // painstate
        0,                            // painchance
        sfx_None,                     // painsound
        S_NULL,                       // meleestate
        S_NULL,                       // missilestate
        S_NULL,                       // deathstate
        S_NULL,                       // xdeathstate
        sfx_None,                     // deathsound
        0,                            // speed
        20 * FRACUNIT,                // radius
        16 * FRACUNIT,                // height
        100,                          // mass
        0,                            // damage
        sfx_None,                     // activesound
        MF_NOBLOCKMAP | MF_NOGRAVITY, // flags
        S_NULL                        // raisestate
    },

    {
        // MT_IFOG
        -1,                           // doomednum
        S_IFOG,                       // spawnstate
        1000,                         // spawnhealth
        S_NULL,                       // seestate
        sfx_None,                     // seesound
        8,                            // reactiontime
        sfx_None,                     // attacksound
        S_NULL,                       // painstate
        0,                            // painchance
        sfx_None,                     // painsound
        S_NULL,                       // meleestate
        S_NULL,                       // missilestate
        S_NULL,                       // deathstate
        S_NULL,                       // xdeathstate
        sfx_None,                     // deathsound
        0,                            // speed
        20 * FRACUNIT,                // radius
        16 * FRACUNIT,                // height
        100,                          // mass
        0,                            // damage
        sfx_None,                     // activesound
        MF_NOBLOCKMAP | MF_NOGRAVITY, // flags
        S_NULL                        // raisestate
    },

    {
        // MT_TELEPORTMAN
        14,                          // doomednum
        S_NULL,                      // spawnstate
        1000,                        // spawnhealth
        S_NULL,                      // seestate
        sfx_None,                    // seesound
        8,                           // reactiontime
        sfx_None,                    // attacksound
        S_NULL,                      // painstate
        0,                           // painchance
        sfx_None,                    // painsound
        S_NULL,                      // meleestate
        S_NULL,                      // missilestate
        S_NULL,                      // deathstate
        S_NULL,                      // xdeathstate
        sfx_None,                    // deathsound
        0,                           // speed
        20 * FRACUNIT,               // radius
        16 * FRACUNIT,               // height
        100,                         // mass
        0,                           // damage
        sfx_None,                    // activesound
        MF_NOBLOCKMAP | MF_NOSECTOR, // flags
        S_NULL                       // raisestate
    },

    {
        // MT_EXTRABFG
        -1,                           // doomednum
        S_BFGEXP,                     // spawnstate
        1000,                         // spawnhealth
        S_NULL,                       // seestate
        sfx_None,                     // seesound
        8,                            // reactiontime
        sfx_None,                     // attacksound
        S_NULL,                       // painstate
        0,                            // painchance
        sfx_None,                     // painsound
        S_NULL,                       // meleestate
        S_NULL,                       // missilestate
        S_NULL,                       // deathstate
        S_NULL,                       // xdeathstate
        sfx_None,                     // deathsound
        0,                            // speed
        20 * FRACUNIT,                // radius
        16 * FRACUNIT,                // height
        100,                          // mass
        0,                            // damage
        sfx_None,                     // activesound
        MF_NOBLOCKMAP | MF_NOGRAVITY, // flags
        S_NULL                        // raisestate
    },

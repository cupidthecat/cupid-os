/**
 * @file info_pickups.h
 * @brief Define Doom pickups in enum order.
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
        // MT_MISC0
        2018,          // doomednum
        S_ARM1,        // spawnstate
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
        MF_SPECIAL,    // flags
        S_NULL         // raisestate
    },

    {
        // MT_MISC1
        2019,          // doomednum
        S_ARM2,        // spawnstate
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
        MF_SPECIAL,    // flags
        S_NULL         // raisestate
    },

    {
        // MT_MISC2
        2014,                      // doomednum
        S_BON1,                    // spawnstate
        1000,                      // spawnhealth
        S_NULL,                    // seestate
        sfx_None,                  // seesound
        8,                         // reactiontime
        sfx_None,                  // attacksound
        S_NULL,                    // painstate
        0,                         // painchance
        sfx_None,                  // painsound
        S_NULL,                    // meleestate
        S_NULL,                    // missilestate
        S_NULL,                    // deathstate
        S_NULL,                    // xdeathstate
        sfx_None,                  // deathsound
        0,                         // speed
        20 * FRACUNIT,             // radius
        16 * FRACUNIT,             // height
        100,                       // mass
        0,                         // damage
        sfx_None,                  // activesound
        MF_SPECIAL | MF_COUNTITEM, // flags
        S_NULL                     // raisestate
    },

    {
        // MT_MISC3
        2015,                      // doomednum
        S_BON2,                    // spawnstate
        1000,                      // spawnhealth
        S_NULL,                    // seestate
        sfx_None,                  // seesound
        8,                         // reactiontime
        sfx_None,                  // attacksound
        S_NULL,                    // painstate
        0,                         // painchance
        sfx_None,                  // painsound
        S_NULL,                    // meleestate
        S_NULL,                    // missilestate
        S_NULL,                    // deathstate
        S_NULL,                    // xdeathstate
        sfx_None,                  // deathsound
        0,                         // speed
        20 * FRACUNIT,             // radius
        16 * FRACUNIT,             // height
        100,                       // mass
        0,                         // damage
        sfx_None,                  // activesound
        MF_SPECIAL | MF_COUNTITEM, // flags
        S_NULL                     // raisestate
    },

    {
        // MT_MISC4
        5,                         // doomednum
        S_BKEY,                    // spawnstate
        1000,                      // spawnhealth
        S_NULL,                    // seestate
        sfx_None,                  // seesound
        8,                         // reactiontime
        sfx_None,                  // attacksound
        S_NULL,                    // painstate
        0,                         // painchance
        sfx_None,                  // painsound
        S_NULL,                    // meleestate
        S_NULL,                    // missilestate
        S_NULL,                    // deathstate
        S_NULL,                    // xdeathstate
        sfx_None,                  // deathsound
        0,                         // speed
        20 * FRACUNIT,             // radius
        16 * FRACUNIT,             // height
        100,                       // mass
        0,                         // damage
        sfx_None,                  // activesound
        MF_SPECIAL | MF_NOTDMATCH, // flags
        S_NULL                     // raisestate
    },

    {
        // MT_MISC5
        13,                        // doomednum
        S_RKEY,                    // spawnstate
        1000,                      // spawnhealth
        S_NULL,                    // seestate
        sfx_None,                  // seesound
        8,                         // reactiontime
        sfx_None,                  // attacksound
        S_NULL,                    // painstate
        0,                         // painchance
        sfx_None,                  // painsound
        S_NULL,                    // meleestate
        S_NULL,                    // missilestate
        S_NULL,                    // deathstate
        S_NULL,                    // xdeathstate
        sfx_None,                  // deathsound
        0,                         // speed
        20 * FRACUNIT,             // radius
        16 * FRACUNIT,             // height
        100,                       // mass
        0,                         // damage
        sfx_None,                  // activesound
        MF_SPECIAL | MF_NOTDMATCH, // flags
        S_NULL                     // raisestate
    },

    {
        // MT_MISC6
        6,                         // doomednum
        S_YKEY,                    // spawnstate
        1000,                      // spawnhealth
        S_NULL,                    // seestate
        sfx_None,                  // seesound
        8,                         // reactiontime
        sfx_None,                  // attacksound
        S_NULL,                    // painstate
        0,                         // painchance
        sfx_None,                  // painsound
        S_NULL,                    // meleestate
        S_NULL,                    // missilestate
        S_NULL,                    // deathstate
        S_NULL,                    // xdeathstate
        sfx_None,                  // deathsound
        0,                         // speed
        20 * FRACUNIT,             // radius
        16 * FRACUNIT,             // height
        100,                       // mass
        0,                         // damage
        sfx_None,                  // activesound
        MF_SPECIAL | MF_NOTDMATCH, // flags
        S_NULL                     // raisestate
    },

    {
        // MT_MISC7
        39,                        // doomednum
        S_YSKULL,                  // spawnstate
        1000,                      // spawnhealth
        S_NULL,                    // seestate
        sfx_None,                  // seesound
        8,                         // reactiontime
        sfx_None,                  // attacksound
        S_NULL,                    // painstate
        0,                         // painchance
        sfx_None,                  // painsound
        S_NULL,                    // meleestate
        S_NULL,                    // missilestate
        S_NULL,                    // deathstate
        S_NULL,                    // xdeathstate
        sfx_None,                  // deathsound
        0,                         // speed
        20 * FRACUNIT,             // radius
        16 * FRACUNIT,             // height
        100,                       // mass
        0,                         // damage
        sfx_None,                  // activesound
        MF_SPECIAL | MF_NOTDMATCH, // flags
        S_NULL                     // raisestate
    },

    {
        // MT_MISC8
        38,                        // doomednum
        S_RSKULL,                  // spawnstate
        1000,                      // spawnhealth
        S_NULL,                    // seestate
        sfx_None,                  // seesound
        8,                         // reactiontime
        sfx_None,                  // attacksound
        S_NULL,                    // painstate
        0,                         // painchance
        sfx_None,                  // painsound
        S_NULL,                    // meleestate
        S_NULL,                    // missilestate
        S_NULL,                    // deathstate
        S_NULL,                    // xdeathstate
        sfx_None,                  // deathsound
        0,                         // speed
        20 * FRACUNIT,             // radius
        16 * FRACUNIT,             // height
        100,                       // mass
        0,                         // damage
        sfx_None,                  // activesound
        MF_SPECIAL | MF_NOTDMATCH, // flags
        S_NULL                     // raisestate
    },

    {
        // MT_MISC9
        40,                        // doomednum
        S_BSKULL,                  // spawnstate
        1000,                      // spawnhealth
        S_NULL,                    // seestate
        sfx_None,                  // seesound
        8,                         // reactiontime
        sfx_None,                  // attacksound
        S_NULL,                    // painstate
        0,                         // painchance
        sfx_None,                  // painsound
        S_NULL,                    // meleestate
        S_NULL,                    // missilestate
        S_NULL,                    // deathstate
        S_NULL,                    // xdeathstate
        sfx_None,                  // deathsound
        0,                         // speed
        20 * FRACUNIT,             // radius
        16 * FRACUNIT,             // height
        100,                       // mass
        0,                         // damage
        sfx_None,                  // activesound
        MF_SPECIAL | MF_NOTDMATCH, // flags
        S_NULL                     // raisestate
    },

    {
        // MT_MISC10
        2011,          // doomednum
        S_STIM,        // spawnstate
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
        MF_SPECIAL,    // flags
        S_NULL         // raisestate
    },

    {
        // MT_MISC11
        2012,          // doomednum
        S_MEDI,        // spawnstate
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
        MF_SPECIAL,    // flags
        S_NULL         // raisestate
    },

    {
        // MT_MISC12
        2013,                      // doomednum
        S_SOUL,                    // spawnstate
        1000,                      // spawnhealth
        S_NULL,                    // seestate
        sfx_None,                  // seesound
        8,                         // reactiontime
        sfx_None,                  // attacksound
        S_NULL,                    // painstate
        0,                         // painchance
        sfx_None,                  // painsound
        S_NULL,                    // meleestate
        S_NULL,                    // missilestate
        S_NULL,                    // deathstate
        S_NULL,                    // xdeathstate
        sfx_None,                  // deathsound
        0,                         // speed
        20 * FRACUNIT,             // radius
        16 * FRACUNIT,             // height
        100,                       // mass
        0,                         // damage
        sfx_None,                  // activesound
        MF_SPECIAL | MF_COUNTITEM, // flags
        S_NULL                     // raisestate
    },

    {
        // MT_INV
        2022,                      // doomednum
        S_PINV,                    // spawnstate
        1000,                      // spawnhealth
        S_NULL,                    // seestate
        sfx_None,                  // seesound
        8,                         // reactiontime
        sfx_None,                  // attacksound
        S_NULL,                    // painstate
        0,                         // painchance
        sfx_None,                  // painsound
        S_NULL,                    // meleestate
        S_NULL,                    // missilestate
        S_NULL,                    // deathstate
        S_NULL,                    // xdeathstate
        sfx_None,                  // deathsound
        0,                         // speed
        20 * FRACUNIT,             // radius
        16 * FRACUNIT,             // height
        100,                       // mass
        0,                         // damage
        sfx_None,                  // activesound
        MF_SPECIAL | MF_COUNTITEM, // flags
        S_NULL                     // raisestate
    },

    {
        // MT_MISC13
        2023,                      // doomednum
        S_PSTR,                    // spawnstate
        1000,                      // spawnhealth
        S_NULL,                    // seestate
        sfx_None,                  // seesound
        8,                         // reactiontime
        sfx_None,                  // attacksound
        S_NULL,                    // painstate
        0,                         // painchance
        sfx_None,                  // painsound
        S_NULL,                    // meleestate
        S_NULL,                    // missilestate
        S_NULL,                    // deathstate
        S_NULL,                    // xdeathstate
        sfx_None,                  // deathsound
        0,                         // speed
        20 * FRACUNIT,             // radius
        16 * FRACUNIT,             // height
        100,                       // mass
        0,                         // damage
        sfx_None,                  // activesound
        MF_SPECIAL | MF_COUNTITEM, // flags
        S_NULL                     // raisestate
    },

    {
        // MT_INS
        2024,                      // doomednum
        S_PINS,                    // spawnstate
        1000,                      // spawnhealth
        S_NULL,                    // seestate
        sfx_None,                  // seesound
        8,                         // reactiontime
        sfx_None,                  // attacksound
        S_NULL,                    // painstate
        0,                         // painchance
        sfx_None,                  // painsound
        S_NULL,                    // meleestate
        S_NULL,                    // missilestate
        S_NULL,                    // deathstate
        S_NULL,                    // xdeathstate
        sfx_None,                  // deathsound
        0,                         // speed
        20 * FRACUNIT,             // radius
        16 * FRACUNIT,             // height
        100,                       // mass
        0,                         // damage
        sfx_None,                  // activesound
        MF_SPECIAL | MF_COUNTITEM, // flags
        S_NULL                     // raisestate
    },

    {
        // MT_MISC14
        2025,          // doomednum
        S_SUIT,        // spawnstate
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
        MF_SPECIAL,    // flags
        S_NULL         // raisestate
    },

    {
        // MT_MISC15
        2026,                      // doomednum
        S_PMAP,                    // spawnstate
        1000,                      // spawnhealth
        S_NULL,                    // seestate
        sfx_None,                  // seesound
        8,                         // reactiontime
        sfx_None,                  // attacksound
        S_NULL,                    // painstate
        0,                         // painchance
        sfx_None,                  // painsound
        S_NULL,                    // meleestate
        S_NULL,                    // missilestate
        S_NULL,                    // deathstate
        S_NULL,                    // xdeathstate
        sfx_None,                  // deathsound
        0,                         // speed
        20 * FRACUNIT,             // radius
        16 * FRACUNIT,             // height
        100,                       // mass
        0,                         // damage
        sfx_None,                  // activesound
        MF_SPECIAL | MF_COUNTITEM, // flags
        S_NULL                     // raisestate
    },

    {
        // MT_MISC16
        2045,                      // doomednum
        S_PVIS,                    // spawnstate
        1000,                      // spawnhealth
        S_NULL,                    // seestate
        sfx_None,                  // seesound
        8,                         // reactiontime
        sfx_None,                  // attacksound
        S_NULL,                    // painstate
        0,                         // painchance
        sfx_None,                  // painsound
        S_NULL,                    // meleestate
        S_NULL,                    // missilestate
        S_NULL,                    // deathstate
        S_NULL,                    // xdeathstate
        sfx_None,                  // deathsound
        0,                         // speed
        20 * FRACUNIT,             // radius
        16 * FRACUNIT,             // height
        100,                       // mass
        0,                         // damage
        sfx_None,                  // activesound
        MF_SPECIAL | MF_COUNTITEM, // flags
        S_NULL                     // raisestate
    },

    {
        // MT_MEGA
        83,                        // doomednum
        S_MEGA,                    // spawnstate
        1000,                      // spawnhealth
        S_NULL,                    // seestate
        sfx_None,                  // seesound
        8,                         // reactiontime
        sfx_None,                  // attacksound
        S_NULL,                    // painstate
        0,                         // painchance
        sfx_None,                  // painsound
        S_NULL,                    // meleestate
        S_NULL,                    // missilestate
        S_NULL,                    // deathstate
        S_NULL,                    // xdeathstate
        sfx_None,                  // deathsound
        0,                         // speed
        20 * FRACUNIT,             // radius
        16 * FRACUNIT,             // height
        100,                       // mass
        0,                         // damage
        sfx_None,                  // activesound
        MF_SPECIAL | MF_COUNTITEM, // flags
        S_NULL                     // raisestate
    },

    {
        // MT_CLIP
        2007,          // doomednum
        S_CLIP,        // spawnstate
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
        MF_SPECIAL,    // flags
        S_NULL         // raisestate
    },

    {
        // MT_MISC17
        2048,          // doomednum
        S_AMMO,        // spawnstate
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
        MF_SPECIAL,    // flags
        S_NULL         // raisestate
    },

    {
        // MT_MISC18
        2010,          // doomednum
        S_ROCK,        // spawnstate
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
        MF_SPECIAL,    // flags
        S_NULL         // raisestate
    },

    {
        // MT_MISC19
        2046,          // doomednum
        S_BROK,        // spawnstate
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
        MF_SPECIAL,    // flags
        S_NULL         // raisestate
    },

    {
        // MT_MISC20
        2047,          // doomednum
        S_CELL,        // spawnstate
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
        MF_SPECIAL,    // flags
        S_NULL         // raisestate
    },

    {
        // MT_MISC21
        17,            // doomednum
        S_CELP,        // spawnstate
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
        MF_SPECIAL,    // flags
        S_NULL         // raisestate
    },

    {
        // MT_MISC22
        2008,          // doomednum
        S_SHEL,        // spawnstate
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
        MF_SPECIAL,    // flags
        S_NULL         // raisestate
    },

    {
        // MT_MISC23
        2049,          // doomednum
        S_SBOX,        // spawnstate
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
        MF_SPECIAL,    // flags
        S_NULL         // raisestate
    },

    {
        // MT_MISC24
        8,             // doomednum
        S_BPAK,        // spawnstate
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
        MF_SPECIAL,    // flags
        S_NULL         // raisestate
    },

    {
        // MT_MISC25
        2006,          // doomednum
        S_BFUG,        // spawnstate
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
        MF_SPECIAL,    // flags
        S_NULL         // raisestate
    },

    {
        // MT_CHAINGUN
        2002,          // doomednum
        S_MGUN,        // spawnstate
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
        MF_SPECIAL,    // flags
        S_NULL         // raisestate
    },

    {
        // MT_MISC26
        2005,          // doomednum
        S_CSAW,        // spawnstate
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
        MF_SPECIAL,    // flags
        S_NULL         // raisestate
    },

    {
        // MT_MISC27
        2003,          // doomednum
        S_LAUN,        // spawnstate
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
        MF_SPECIAL,    // flags
        S_NULL         // raisestate
    },

    {
        // MT_MISC28
        2004,          // doomednum
        S_PLAS,        // spawnstate
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
        MF_SPECIAL,    // flags
        S_NULL         // raisestate
    },

    {
        // MT_SHOTGUN
        2001,          // doomednum
        S_SHOT,        // spawnstate
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
        MF_SPECIAL,    // flags
        S_NULL         // raisestate
    },

    {
        // MT_SUPERSHOTGUN
        82,            // doomednum
        S_SHOT2,       // spawnstate
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
        MF_SPECIAL,    // flags
        S_NULL         // raisestate
    },

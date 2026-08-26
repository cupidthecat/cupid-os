# Freedoom runtime handoff

Cupid OS pins the official Freedoom v0.13.0 Phase 1 IWAD and its complete
upstream release archive under `third_party/freedoom/0.13.0/`. The fixture is
opt-in. The normal asset-free image and its recorded hashes do not change.

## Build and inspect the first image

From the repository root:

```sh
python -m unittest tests.test_freedoom_fixture
make WAD_SRCS=third_party/freedoom/0.13.0/freedoom1.wad all
make WAD_SRCS=third_party/freedoom/0.13.0/freedoom1.wad run
```

At the Cupid OS terminal, start Phase 1 explicitly:

```text
doom -iwad /disk/wads/freedoom1.wad
```

Record the image hash, kernel hash, QEMU version, CPU count, CPU model, NIC,
and serial-log hash. Use a private image copy for tests that write saves or
configuration.

## Runtime work still open

The first implementation slice should add a repeatable guest gate that starts
the pinned IWAD and proves a rendered gameplay frame without a panic. Extend
that gate in small steps to cover:

1. keyboard and mouse input that changes player or menu state;
2. AC97 and PC-speaker output during an IWAD-backed session;
3. menu-driven save and load through `/home/doom`;
4. shutdown, reboot, and successful reload of the saved game;
5. both e1000 and RTL8139 four-vCPU configurations used by the existing Doom
   recovery frontier.

Keep the existing missing-IWAD and return-to-shell checks. A successful manual
launch is useful diagnosis, but it does not close any runtime acceptance item.
Update issue #29 and the bootstrap capability, migration, dependency, and log
records with each executed boundary.

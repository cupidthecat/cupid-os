# Freedoom Phase 1 v0.13.0

`freedoom1.wad` is the unchanged Phase 1 IWAD from the official Freedoom
v0.13.0 release. It is available under the BSD 3-Clause license in
`COPYING.txt`. Keep that file and both credit files with every redistributed
copy.

## Upstream release

- Project: <https://github.com/freedoom/freedoom>
- Release: <https://github.com/freedoom/freedoom/releases/tag/v0.13.0>
- Tag: `v0.13.0`
- Tagged commit: `cfb8644b1a8dc7d7d2177e6a892ccaa2922bdaae`
- Published: 2024-01-29
- Source archive: `freedoom-0.13.0.zip`
- Archive SHA-256: `3f9b264f3e3ce503b4fb7f6bdcb1f419d93c7b546f4df3e874dd878db9688f59`
- Archive member: `freedoom-0.13.0/freedoom1.wad`

The release archive, signed upstream checksum, and detached archive signature
are pinned beside the extracted fixture. The checksum and signature files have
not been cryptographically verified in this repository because the release
signing key and a PGP verifier are not checked build dependencies. Their exact
bytes are retained for independent verification. A clean checkout can verify
that the extracted WAD is the archive member recorded below without contacting
GitHub.

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `freedoom1.wad` | 28,795,076 | `7323bcc168c5a45ff10749b339960e98314740a734c30d4b9f3337001f9e703d` |
| `COPYING.txt` | 1,644 | `7c62f2c520769c798774f416637eec4921e5f0aafdac1245ae7c8a8cf65fe102` |
| `CREDITS.txt` | 15,872 | `72364333d295c598d75ef9553d3b3661ad0b60e27271c87b7637c74b2f46918a` |
| `CREDITS-MUSIC.txt` | 9,388 | `2bb77677bbc4e587bba44ccb6514ee0f5397c7bb8ae9580b9188d3d6a57c0a31` |
| `README.html` | 34,415 | `e4ec61d6449c4116e818c79e15e178dacbf78cee43a637b7c468f04d0c4390b0` |
| `freedoom-0.13.0-CHECKSUM` | 898 | `01a627fa0c80b7446f4f435df6fffef8f354a9987b23870bc5ea5464ca82e977` |
| `freedoom-0.13.0.zip.sig` | 438 | `77f243f179a42317e22e816e3e7b012a505d36e8416fe5cb914220e58412c789` |

## Cupid OS use

The normal asset-free build remains unchanged. Build an IWAD-backed image
with the pinned fixture by passing its path explicitly:

```sh
make WAD_SRCS=third_party/freedoom/0.13.0/freedoom1.wad all
```

The image stages the file as `/disk/wads/freedoom1.wad`. This fixture is now
available for the gameplay, input, audio, save/load, and reboot-persistence
work tracked by the Doom compatibility issue. Its presence alone is not
runtime evidence for those behaviors.

See [the runtime handoff](../../../docs/bootstrap/FREEDOOM-RUNTIME.md) for the
first build and boot commands and the evidence still required.

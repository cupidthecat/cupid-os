# Formatted OS runtime frontier

The existing complete 31-command graphics, storage, USB and audio frontier
passes against a private copy of the formatted OS image. The run takes
892.128 seconds on native Windows with four QEMU CPUs, the `max` CPU model
and e1000. It also passes the SMP, RDRAND, desktop and 62 TLS checks.

This image was built with the installed seeds. The run does not establish a
replacement-tool OS build, Doom gameplay or Doom music acceptance.

## Captured output

Both framebuffer captures are 640 by 480 pixels; 97,552 pixels differ.
Independent decoding confirms that count and verifies that the PNG preview
has the same pixels as the raw PPM. Visual inspection shows the terminal
and desktop.

| Audio device | PCM frames | Peak magnitude | Nonzero samples |
| --- | ---: | ---: | ---: |
| AC97 | 36,432,161 | 25,600 | 843,669 |
| PC speaker | 74,775 | 30,799 | 149,354 |

Both captures contain stereo 16-bit PCM at 44,100 Hz. QEMU leaves its canonical
RIFF and data length fields at zero. The existing validator derives lengths
in memory. An independent reader checks the exact header, actual closed-file
length, block alignment and PCM samples without changing the captured bytes.
Direct `wave.open` rejects those placeholder lengths.

The source 200 MiB OS image and USB fixture keep their complete hashes. The
OS image has SHA-256
`c4129c8777b8056d4b30a4e4ce5567112d5c7c1bc4b6a1efac29265fb5d00bbf`.
The runner removes its private disk copies after capturing the evidence.

## Retained evidence

The [runtime archive](evidence/formatted-os-runtime-frontier-20261110.json.gz)
contains fourteen payloads: serial and controller logs, raw PCM captures,
framebuffers, the USB fixture, independent readbacks, controller source and
the unchanged guest runner. It binds the source OS image by hash without
embedding it. Every exported payload was decoded and its size and SHA-256
checked. The archive has 1,736,593 bytes and SHA-256
`7928d492aae8f79b3e379e44f4a8f24565ad02d08a0db328eb3c7eca3ecafe9c`.

After a separate byte-exact archive check, the raw audio copies and source
USB fixture are retired, freeing 179,582,264 bytes. Logs and screenshots
remain available alongside the compressed captures.

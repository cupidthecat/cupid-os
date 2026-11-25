# Build the disk template in Cupid OS

Run the existing CupidObj disk-template operation from an opt-in Cupid OS
worker. The job names the boot file, raw kernel and HomeFS output in order.
The worker uses the ordinary 200 MiB geometry and places the FAT16
partition at sector 20,480.

The result contains the MBR, boot code, kernel, zeroed reserve and empty
FAT16 structures. It ends before cluster 2. The replay compares those
bytes with checked hosted CupidObj, extends the file to the full size
with zeroes, then boots the disk.

The disk, linker and other guest workers share the line reader. Each job
requires canonical paths, and paths for published outputs stay under
`/home`.

# ringbuf — Linux ring buffer char device + SPSC benchmark

A small Linux kernel character device, `/dev/ringbuf`, backed by a fixed-size
ring buffer, together with a CPU-pinned single-producer/single-consumer (SPSC)
throughput benchmark. Everything is built and tested inside a QEMU virtual
machine so kernel experiments can't take down the host.

Project write-up: <https://hackmd.io/@cmosinverter/ring-buffer>

## Overview

- **Device** — `modules/ringbuf.c` registers a misc device `/dev/ringbuf`
  (mode `0666`). The buffer is a 4096-byte, power-of-two ring with `head`
  (next byte to read) and `tail` (next byte to write) indices. Power-of-two
  sizing lets wraparound use `head & MASK` / `tail & MASK` instead of modulo.
- **Concurrency** — `read()` and `write()` are serialized under a single mutex,
  so the device is safe to use from concurrent producer/consumer threads.
- **Benchmark** — `modules/bench.c` spawns a producer pinned to CPU 0 and a
  consumer pinned to CPU 1, streams 64 MiB through `/dev/ringbuf` in 256-byte
  chunks, verifies every byte against a deterministic pattern (`patt()`), and
  prints the elapsed time in milliseconds.

## Layout

| Path                 | Description                                        |
| -------------------- | -------------------------------------------------- |
| `modules/ringbuf.c`  | The `/dev/ringbuf` kernel module                   |
| `modules/bench.c`    | Userspace SPSC throughput / correctness harness    |
| `modules/Makefile`   | Builds the module and the `bench` binary           |
| `setup.sh`           | One-shot build of QEMU, the kernel, and BusyBox    |
| `run.sh`             | Boots the guest under QEMU                          |

## Environment setup

`setup.sh` builds the whole development environment. It is idempotent — already
cloned repos and finished build steps are skipped, so it's safe to re-run.

```bash
./setup.sh
```

This installs the apt prerequisites (requires `sudo`) and then builds:

- QEMU **v11.0.1**
- Linux kernel **v7.0**
- BusyBox **1_36_0**, packed into `initramfs.cpio.gz`

## Build

The module is built against the kernel tree in `../linux` (override with
`KDIR=...`):

```bash
cd modules
make          # builds ringbuf.ko
make bench    # builds the static `bench` binary
```

## Run

Boot the guest. `run.sh` shares the project directory into the VM over 9p,
mounted at `/mnt`:

```bash
./run.sh
```

Inside the guest shell:

```sh
cd /mnt
insmod modules/ringbuf.ko

# smoke test
echo "hello ring buffer" | tee /dev/ringbuf
cat /dev/ringbuf

# throughput benchmark
modules/bench
```

The benchmark prints a line like `Execution time: 123.456 ms`.

## Notes

- Ring buffer size is `4096` bytes; the benchmark transfers `64 MiB` in
  `256`-byte chunks.
- Producer and consumer are pinned to CPUs 0 and 1 respectively; data integrity
  is checked with the `patt()` byte pattern.
- See the [write-up](https://hackmd.io/@cmosinverter/ring-buffer) for the
  optional Ftrace-based performance analysis.

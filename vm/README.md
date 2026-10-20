# Exercise virtual machines

The supported VM interface is the `rklings` CLI.
It boots prepared Ubuntu cloud images through the Go implementation in
`internal/vm`; there is no standalone shell boot script.

Prepare the images, SSH key, certificates, and build toolchains once:

```console
$ ./rklings setup
```

Build an exercise, create an isolated overlay, stage its declared artifacts, and
open an interactive guest:

```console
$ ./rklings run L02
```

Run the exercise's manifest-declared proof in a fresh guest:

```console
$ ./rklings validate L02
```

Use `--reference` with either command to run the reference workspace.

## Runtime model

Each run creates a temporary qcow2 overlay on top of the cached course image.
Cloud-init installs the course SSH key, QEMU forwards a random host port to
guest SSH, and the CLI waits for the guest before staging files.
Closing the exercise shell tears down QEMU and removes the overlay.

`rklings run` boots as root for staging, then opens the interactive shell as the
exercise's declared VM user.
While the lab is running, the CLI prints the command for opening another student
or root shell with `rklings ssh`.

D00 uses its separate Ubuntu 24.04 image and provisions the pinned deployment
kernel.
Other exercises use the Ubuntu 26.04 course image.

## Files retained here

- `checks/` — shared guest-side validation library and checks.
- `tools/` — focused binaries staged only when a manifest requests them.
- `fixtures/fileless-agent/` — build fixture used by fileless execution tests.
- `ui.sh` — output helpers sourced by guest checks.

Exercise manifests decide which checks, tools, artifacts, users, and runtime
actions are staged.
Files in this directory are not discovered implicitly.

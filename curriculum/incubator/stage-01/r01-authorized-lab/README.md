# R01 — Work inside an authorized disposable lab

Kernel code has machine-wide privilege.
Before writing any, define the machine you are allowed to affect and prove that the course VM does not expose the host filesystem or network.

## Your task

Copy `template/lab.toml` into a private workspace and replace every `TODO`.
Describe one explicitly authorized local guest, name the conditions that make you stop, and keep the containment settings unchanged.

Your manifest must record:

- who owns the lab and where its authorization is recorded;
- the only permitted guest and the systems that are prohibited;
- at least two concrete stop conditions;
- how shutdown and discarded guest state provide cleanup evidence.

Do not put credentials, tokens, customer names, production hostnames, or other sensitive data in the manifest.

## Check

From the repository root, run:

```bash
curriculum/incubator/stage-01/r01-authorized-lab/check path/to/lab.toml
```

The check rejects incomplete or weakened containment declarations, then boots the existing diskless VM with no network device.
The guest independently confirms its mounted interfaces and powers down.
It never loads a module, attaches a host disk, or enables networking.

Success is both a valid manifest and `[+] PASSED` from the disposable guest.
A rejected manifest must not boot a VM.
After success, no guest process or guest filesystem remains.

# Capstones

Lings teach one concept. Capstones say: stop learning, start building.

Each capstone combines everything you learned into something you can run,
break, and show off. No new concepts, just assembly.

```
  lings 00-10         lings 11-15         lings 16-33
  foundation          interfaces          concurrency + introspection + hooking
       |                   |                   |
       v                   v                   v
  +-----------+       +-----------+       +-----------+
  | Capstone 0|       | Capstone 1|       | Capstone 2|
  | Message   |       | Command   |       | Tripwire  |
  | board     |       | box       |       |           |
  +-----------+       +-----------+       +-----------+
       visible             control             audit
       data                surface             tool

  lings 34-40         lings 41-49         lings 50-57
  hiding              networking          persistence + crypto
       |                   |                   |
       v                   v                   v
  +-----------+       +-----------+       +-----------+
  | Capstone 3|       | Capstone 4|       | Capstone 5|
  | Dead      |       | Remote    |       | Red vs    |
  | drop      |       | shell     |       | blue      |
  +-----------+       +-----------+       +-----------+
       spy                operator           adversary
       comms              console            game
```

## The arc

Each capstone is the previous one evolved:

0. **Message board** -- your first kernel "app." Even a junior sysadmin could find it.
1. **Command box** -- a control surface. Commands in, results out. C2 without the network.
2. **Tripwire** -- hook syscalls, log access. The "good guy" use. For now.
3. **Dead drop** -- hide the interface. Only you and your handler know it is there.
4. **Remote shell** -- put it on the network. You are the operator now.
5. **Red vs blue** -- full rootkit. The final exam is a game against yourself.

## How to use

```bash
./rootkitlings check capstone-0    # verify your solution
./rootkitlings peek  capstone-0    # diff against reference
./rootkitlings reset capstone-0    # restore reference solution
```

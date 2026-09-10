# Deepdive 032

Tu caches un port TCP de /proc/net/tcp. Mais il y a d'autres endroits
ou les connexions sont visibles.

Questions:

1. `/proc/net/tcp6` montre les sockets IPv6. Il faudrait aussi hooker
   `tcp6_seq_show`. Lis `net/ipv6/tcp_ipv6.c` et compare avec tcp4.

2. `ss` (iproute2) utilise-t-il /proc/net/tcp ou un autre mecanisme?
   Indice: `ss` utilise netlink (AF_NETLINK, SOCK_DIAG). Pour cacher
   aussi de `ss`, il faudrait hooker `inet_diag_dump_one_icsk`.

3. Le rootkit KoviD cache les ports en hookant a la fois seq_show et
   le netlink diag. Lis son code pour voir la double approche.

4. Un administrateur avise peut contourner ton hook en utilisant
   `bpftrace` ou un programme eBPF qui lit directement les structures
   kernel. Comment l'eBPF bypass-t-il les hooks de /proc?

L'exercice 033 cache des lignes dans un fichier arbitraire.

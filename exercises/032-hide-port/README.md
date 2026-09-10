# Exercice 032: hide a network port from netstat

VM: qemu (./lab/vm.sh)

## Le mecanisme

`netstat -tlnp` ou `ss -tlnp` lisent `/proc/net/tcp` pour afficher les
connexions TCP. Ce fichier est genere par le kernel via l'interface seq_file.

La fonction qui produit chaque ligne est `tcp4_seq_show` dans
`net/ipv4/tcp_ipv4.c`. Elle est appelee par le framework seq_file pour
chaque socket TCP.

Approche: on hooke `tcp4_seq_show`. Quand la fonction est appelee pour un
socket dont le port local correspond a celui qu'on veut cacher, on retourne
0 sans rien ecrire dans le seq_file. La ligne disparait de /proc/net/tcp.

Les ports dans /proc/net/tcp sont en hexadecimal. Le port 4444 est 0x115C.

Le struct sock est accessible via l'argument `void *v` de seq_show, qui
pointe vers un `struct sock`. Le port local est dans `inet_sk(sk)->inet_num`
ou `inet_sk(sk)->inet_sport` (en network byte order).

## Ce qu'il faut comprendre AVANT d'ecrire

1. Lis `/proc/net/tcp` dans la VM:
   ```bash
   cat /proc/net/tcp
   ```
   Chaque ligne montre: slot, local_address:port, remote_address:port, state...
   Les ports sont en hex.

2. Lis `tcp4_seq_show` dans `net/ipv4/tcp_ipv4.c`:
   https://elixir.bootlin.com/linux/latest/source/net/ipv4/tcp_ipv4.c
   Cherche la fonction et regarde comment elle accede au sock.

3. `inet_sk(sk)->inet_num` donne le port local en host byte order.

4. Le premier appel a seq_show a `v == SEQ_START_TOKEN`, c'est le header.
   Il faut le laisser passer sinon l'en-tete de /proc/net/tcp disparait.

## L'exercice

Dans `src/hideport.c`, complete le hook de tcp4_seq_show:
1. Verifie que v n'est pas SEQ_START_TOKEN
2. Extrais le port local via inet_sk
3. Si c'est le port cible, retourne 0 (skip la ligne)
4. Sinon, appelle l'original

8 lignes de code.

Compile avec `make`, puis lance `test.sh` dans la VM.

## Test

```bash
./test.sh
```

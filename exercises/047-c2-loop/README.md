# Exercice 047: boucle C2 complete

VM: qemu (./lab/vm.sh)

## Le mecanisme

Cet exercice combine tout ce qu'on a construit depuis l'exo 042:
- Un kthread (exo 010) qui tourne en boucle
- Un socket kernel (exo 042) qui se connecte au serveur
- Le protocole TLV (exo 045) pour structurer les messages
- L'execution de commandes (exo 046) via call_usermodehelper
- L'envoi du resultat (exo 043) via kernel_sendmsg

Le flow:
1. module_init cree un kthread
2. Le kthread se connecte au serveur
3. Il envoie un HEARTBEAT
4. Il attend une CMD_REQUEST
5. Il execute la commande
6. Il lit le fichier de sortie
7. Il envoie une CMD_RESPONSE avec le resultat
8. Il boucle (goto 3)

Pour lire le fichier de sortie dans le kernel, on utilise:
```c
struct file *f = filp_open("/tmp/rkl_out", O_RDONLY, 0);
kernel_read(f, buf, count, &pos);
filp_close(f, NULL);
```

Source:
- https://elixir.bootlin.com/linux/latest/source/fs/read_write.c (kernel_read)
- https://elixir.bootlin.com/linux/latest/source/fs/open.c (filp_open)

## Ce qu'il faut comprendre AVANT d'ecrire

1. Revois l'exo 010 (kthread) pour la structure de la boucle.
2. Revois l'exo 045 (protocole) pour pack/unpack.
3. Revois l'exo 046 (exec) pour call_usermodehelper.
4. filp_open retourne un ERR_PTR en cas d'erreur. Teste avec IS_ERR().

## L'exercice

Dans `src/c2loop.c`, l'ossature est fournie: le kthread, le connect, le send/recv,
le protocole. Complete la fonction `c2_thread_fn` pour implementer la boucle
principale: recv commande, exec, lire output, envoyer reponse.

## Test

```bash
./test.sh
```

Le test lance test_c2server.py, charge le module. Le serveur envoie "id",
le module l'execute et renvoie le resultat. Le serveur verifie qu'il
contient "uid=0".

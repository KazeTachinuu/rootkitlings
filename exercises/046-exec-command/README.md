# Exercice 046: executer une commande depuis le kernel

VM: qemu (./lab/vm.sh)

## Le mecanisme

Le kernel ne peut pas executer directement un programme comme le ferait
un process userspace. Il utilise `call_usermodehelper` (deja vu a l'exo 041)
pour fork+exec un programme en userspace.

Pour capturer la sortie d'une commande, l'astuce est de rediriger stdout
vers un fichier via un wrapper shell:

```bash
/bin/bash -c "COMMANDE > /tmp/output 2>&1"
```

Le module ecrit la commande recue dans /proc, lance call_usermodehelper avec
le wrapper shell, attend la fin (UMH_WAIT_PROC), puis lit le fichier de sortie
(optionnel, on le fait a l'exo 047 via le reseau).

Source:
- https://elixir.bootlin.com/linux/latest/source/include/linux/umh.h
- https://elixir.bootlin.com/linux/latest/source/kernel/umh.c

## Ce qu'il faut comprendre AVANT d'ecrire

1. Relis call_usermodehelper (exo 041). Cette fois on utilise UMH_WAIT_PROC
   au lieu de UMH_NO_WAIT car on veut attendre que la commande finisse.

2. La commande est passee via /proc/rootkitlings_cmd (ecriture depuis userspace).

3. argv pour bash -c: {"/bin/bash", "-c", "commande > /tmp/rkl_out 2>&1", NULL}

4. call_usermodehelper retourne 0 si le process a ete lance avec succes.
   Le code de retour du process lui-meme est encode dans les bits hauts
   du retour (comme waitpid).

## L'exercice

Dans `src/execmd.c`, complete la fonction `exec_cmd` pour:
1. Construire la chaine de commande avec redirection
2. Definir argv et envp
3. Appeler call_usermodehelper avec UMH_WAIT_PROC

Le module cree /proc/rootkitlings_cmd. Quand on y ecrit une commande,
il l'execute et ecrit la sortie dans /tmp/rkl_out.

## Test

```bash
./test.sh
```

Le test charge le module, ecrit "id" dans /proc/rootkitlings_cmd,
et verifie que /tmp/rkl_out contient "uid=0(root)".

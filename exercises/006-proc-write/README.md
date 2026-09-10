# Exercice 006: Ecrire dans /proc

Mode VM: vng (boot rapide)

## Le mecanisme

Jusqu'ici, notre entree /proc est en lecture seule. Maintenant on ajoute
l'ecriture: le userspace peut envoyer des donnees au module kernel via
`echo "data" > /proc/rootkitlings_rw`.

Pour lire les donnees envoyees par le userspace, on utilise `copy_from_user()`.
C'est une fonction critique definie dans `include/linux/uaccess.h`:

https://elixir.bootlin.com/linux/latest/source/include/linux/uaccess.h

Pourquoi `copy_from_user` et pas un simple memcpy ? Parce que le pointeur
vient du userspace. Il pourrait etre invalide, NULL, ou pointer vers de la
memoire kernel. `copy_from_user` verifie que l'adresse est bien dans
l'espace utilisateur avant de copier. Utiliser memcpy directement serait
une faille de securite.

L'implementation depend de l'architecture. Pour x86_64, regarde:
https://elixir.bootlin.com/linux/latest/source/arch/x86/lib/usercopy_64.c

Le callback write dans proc_ops a cette signature:
```c
ssize_t (*proc_write)(struct file *file, const char __user *buf,
                      size_t count, loff_t *ppos);
```

Note le `__user` sur `buf`. C'est une annotation qui indique que ce pointeur
vient du userspace. Le kernel utilise Sparse (outil d'analyse statique)
pour verifier que tu ne dereferences jamais un pointeur __user directement.

## Ce qu'il faut comprendre AVANT d'ecrire

1. Lis la page man de copy_from_user. La fonction retourne le nombre
   d'octets NON copies (0 = succes). C'est l'inverse de ce qu'on attend.

2. Regarde ce qui se passe si tu fais `memcpy(dst, user_buf, len)` au lieu
   de `copy_from_user`. Sur un kernel avec SMAP (Supervisor Mode Access
   Prevention), ca crash. SMAP empeche le kernel de lire la memoire user
   directement.

3. Le buffer kernel ou tu stockes les donnees doit etre alloue par toi.
   Ici on utilise un buffer statique pour simplifier.

## L'exercice

Dans `src/procwrite.c`, le module cree /proc/rootkitlings_rw avec read et
write. La fonction read (show) affiche le contenu du buffer. Complete le
TODO: ecris la fonction write qui:

1. Verifie que count ne depasse pas la taille du buffer
2. Copie les donnees du userspace avec copy_from_user
3. Ajoute un '\0' a la fin
4. Retourne count (nombre d'octets ecrits)

## Test

```bash
./test.sh
```

Le test ecrit "hello kernel" dans /proc, puis relit et verifie le contenu.

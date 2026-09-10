# Exercice 012: obtenir kallsyms_lookup_name via kprobe

## Le mecanisme

Depuis le kernel 5.7 (commit 0bd476e6c671), `kallsyms_lookup_name` n'est plus
exportee aux modules kernel. Un module ne peut plus faire
`kallsyms_lookup_name("commit_creds")` pour obtenir l'adresse d'un symbole.

Le workaround classique utilise kprobes. L'idee:
1. On cree un kprobe qui pointe sur `kallsyms_lookup_name` (kprobes peut
   toujours resoudre les noms de symboles internes au kernel)
2. On enregistre le kprobe (register_kprobe)
3. L'adresse resolue se trouve dans `kp.addr`
4. On desenregistre le kprobe (unregister_kprobe)
5. On cast `kp.addr` en pointeur de fonction

Le type de kallsyms_lookup_name:
```c
unsigned long kallsyms_lookup_name(const char *name);
```

On le stocke dans un pointeur de fonction:
```c
typedef unsigned long (*kallsyms_lookup_name_t)(const char *name);
static kallsyms_lookup_name_t my_kallsyms_lookup_name;
```

La struct kprobe est definie dans `include/linux/kprobes.h`:
```c
struct kprobe {
    const char *symbol_name;   // nom du symbole a instrumenter
    kprobe_opcode_t *addr;     // adresse resolue apres register
    // ... autres champs
};
```

## Ce qu'il faut comprendre AVANT d'ecrire

1. Ouvre la definition de struct kprobe:
   https://elixir.bootlin.com/linux/latest/source/include/linux/kprobes.h

2. Regarde register_kprobe dans kernel/kprobes.c:
   https://elixir.bootlin.com/linux/latest/source/kernel/kprobes.c
   Cherche comment il resout symbol_name en addr.

3. Comprends que le kprobe ici est un "abus" du mecanisme: on ne veut pas
   instrumenter la fonction, on veut juste son adresse. On register puis
   unregister immediatement.

## L'exercice

Dans `src/ksyms.c`, complete le code pour:
1. Enregistrer le kprobe sur "kallsyms_lookup_name"
2. Extraire l'adresse depuis kp.addr
3. Desenregistrer le kprobe
4. Utiliser le pointeur de fonction pour resoudre "commit_creds"

```bash
make
# Dans la VM:
insmod src/ksyms.ko
dmesg | tail
rmmod ksyms
```

## Test

```bash
./test.sh
```

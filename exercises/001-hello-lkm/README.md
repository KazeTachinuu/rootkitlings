# 001: ton premier module kernel

VM: vng (boot rapide, `./lab/vng.sh`)

Sources kernel: verifiees contre linux v6.10 (torvalds/linux).
Chaque snippet est copie du vrai source, pas de memoire.


## Contexte: pourquoi un module kernel

Le kernel Linux est un binaire monolithique qui tourne en ring 0 du CPU.
Un module kernel (LKM) est du code compile separement (fichier `.ko`) qu'on
injecte dans ce binaire a chaud, sans rebooter. Une fois charge, le module
tourne dans le MEME espace memoire que le kernel. Pas de sandbox. Pas
d'isolation. Acces total a toute la memoire, toutes les structures internes.

C'est le mecanisme utilise par les drivers (GPU, reseau, USB). Et c'est le
meme mecanisme qu'utilise un rootkit kernel.

On va construire un module from scratch, ligne par ligne.


## Etape 1: les includes

```c
#include <linux/module.h>
#include <linux/init.h>
```

Ce ne sont PAS les headers de `/usr/include/`. Ce sont les headers KERNEL,
situes dans le build dir du kernel installe. Sur Arch:

```bash
ls /usr/lib/modules/$(uname -r)/build/include/linux/module.h
ls /usr/lib/modules/$(uname -r)/build/include/linux/init.h
```

`linux/module.h` contient: la definition de `struct module` (la structure
qui represente un module en memoire kernel), les macros `module_init`,
`module_exit`, `MODULE_LICENSE`, `MODULE_AUTHOR`, etc.

`linux/init.h` contient: les macros `__init` et `__exit` (on verra ce que
c'est juste apres).

Il n'y a PAS de libc en kernel space. Pas de `stdio.h`, pas de `stdlib.h`,
pas de `string.h`. Le kernel a ses propres implementations de tout.


## Etape 2: la fonction d'initialisation

```c
static int __init hello_init(void)
{
    return 0;
}
```

C'est la fonction que le kernel appelle quand le module est charge (via
`insmod`). Decomposons chaque mot.

**static** : en C, `static` sur une fonction signifie que le symbole n'est
pas exporte hors de cette unite de compilation. La fonction existe dans le
.o mais n'est pas visible de l'exterieur. On fait ca pour eviter les
collisions de noms: si deux modules ont chacun une `hello_init`, le `static`
empeche le conflit. Ce n'est pas obligatoire (ca compile sans), mais c'est
une bonne pratique.

**int** : la fonction retourne un entier.
- `return 0` = succes, le module est charge
- `return -ENOMEM` (ou tout negatif) = erreur, le kernel annule le chargement

Les codes d'erreur kernel sont des entiers negatifs. `-ENOMEM` = -12,
`-EINVAL` = -22, etc. Si tu retournes une erreur, `insmod` echoue cote
userspace et affiche un message.

**__init** : un macro defini dans `include/linux/init.h` (v6.10, code exact):

```c
/* include/linux/init.h, v6.10 */
#define __init   __section(".init.text") __cold  __latent_entropy __noinitretpoline
```

La partie importante est `__section(".init.text")`. C'est un attribut GCC qui
dit: "place le code machine de cette fonction dans la section ELF `.init.text`
au lieu de la section standard `.text`."

Pourquoi? Le kernel sait que le code dans `.init.text` n'est execute qu'UNE
SEULE FOIS (au chargement). Apres l'init, le kernel peut liberer la memoire
de `.init.text`. Tu as deja vu le message au boot:
"Freeing unused kernel memory: XXXkB". C'est ca.

`__cold` dit a GCC que cette fonction est rarement appelee. GCC l'optimise
en consequence (pas de prefetch, placement en fin de binaire pour ne pas
polluer le cache d'instructions).

Si tu oublies `__init`, ca compile et ca marche. Tu perds juste l'optimisation
memoire (la fonction reste en memoire apres l'init au lieu d'etre liberee).

**void** : pas de parametre. La signature est fixe: `int (*)(void)`.

Verifie apres compilation que la section existe:

```bash
objdump -h src/hello.ko | grep init.text
```


## Etape 3: ecrire un message avec printk

```c
static int __init hello_init(void)
{
    pr_info("rootkitlings/001: hello from kernel space\n");
    return 0;
}
```

Pas de `printf` en kernel space (pas de libc). On utilise `printk`, qui ecrit
dans un ring buffer circulaire en memoire kernel. On lit ce buffer avec la
commande `dmesg`.

`pr_info` est un raccourci pour `printk` avec le niveau INFO. Voici la chaine
de macros exacte:

```c
/* include/linux/printk.h, v6.10 */
#define pr_info(fmt, ...) \
    printk(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)
```

```c
/* include/linux/kern_levels.h, v6.10 */
#define KERN_SOH     "\001"       /* ASCII Start Of Header */
#define KERN_INFO    KERN_SOH "6" /* informational */
```

Donc `pr_info("hello")` se developpe en `printk("\001" "6" "hello")`.

En C, des strings cote a cote sont concatenees par le compilateur:
`"\001" "6" "hello"` devient `"\0016hello"`.

Le `\001` (ASCII SOH, valeur 1) est un marqueur. Le kernel lit le caractere
qui suit (`6`) pour determiner le niveau de log. Les niveaux vont de 0
(EMERG, le plus grave) a 7 (DEBUG, le moins grave):

```c
/* include/linux/kern_levels.h, v6.10 */
#define KERN_EMERG   KERN_SOH "0" /* system is unusable */
#define KERN_ALERT   KERN_SOH "1" /* action must be taken immediately */
#define KERN_CRIT    KERN_SOH "2" /* critical conditions */
#define KERN_ERR     KERN_SOH "3" /* error conditions */
#define KERN_WARNING KERN_SOH "4" /* warning conditions */
#define KERN_NOTICE  KERN_SOH "5" /* normal but significant condition */
#define KERN_INFO    KERN_SOH "6" /* informational */
#define KERN_DEBUG   KERN_SOH "7" /* debug-level messages */
```

Le ring buffer a une taille fixe (configurable, typiquement 128 KB). Quand
il est plein, les vieux messages sont ecrases. `dmesg` lit ce buffer via le
syscall `syslog(2)`.


## Etape 4: la fonction de sortie

```c
static void __exit hello_exit(void)
{
    pr_info("rootkitlings/001: goodbye from kernel space\n");
}
```

Appelle quand le module est decharge (`rmmod`).

**void** (retour) : pas de code retour. Quand le kernel decide de decharger,
c'est fait. Ta fonction de sortie ne peut pas dire "non". Elle fait le
nettoyage (liberer memoire, supprimer des fichiers /proc, desenregistrer des
hooks, etc.) et c'est tout.

**__exit** : meme principe que `__init`, section ELF `.exit.text`:

```c
/* include/linux/init.h, v6.10 */
#define __exit   __section(".exit.text") __exitused __cold notrace
```

Particularite: si le module est compile en built-in (integre au noyau au lieu
d'un .ko), la fonction `__exit` n'est JAMAIS appelee (on ne peut pas
"decharger" du code built-in). Le kernel ne l'inclut meme pas dans le binaire.
Pour un .ko chargeable, elle est toujours incluse.


## Etape 5: enregistrer les fonctions avec module_init / module_exit

```c
module_init(hello_init);
module_exit(hello_exit);
```

Ce ne sont PAS des appels de fonctions. Ce sont des macros. Voici le vrai
code dans la branche `#ifdef MODULE` (quand on compile en .ko, kbuild definit
la macro `MODULE`):

```c
/* include/linux/module.h, v6.10 */
#define module_init(initfn)                                     \
    static inline initcall_t __maybe_unused __inittest(void)    \
    { return initfn; }                                          \
    int init_module(void) __copy(initfn)                        \
        __attribute__((alias(#initfn)));                         \
    ___ADDRESSABLE(init_module, __initdata);
```

Ligne par ligne:

1. `static inline initcall_t __maybe_unused __inittest(void) { return initfn; }`
   Astuce de type-checking. Cette fonction n'est jamais appelee. Elle existe
   pour que le compilateur verifie que `initfn` a le bon type (`int (*)(void)`).
   Si tu passes une fonction avec la mauvaise signature, GCC donne un warning
   ICI plutot qu'un crash au runtime.

2. `int init_module(void) __attribute__((alias(#initfn)));`
   La ligne importante. `#initfn` fait du C preprocessing "stringification":
   `#hello_init` devient `"hello_init"`. L'attribut GCC `alias` cree un
   symbole ELF `init_module` qui pointe vers le MEME code que `hello_init`.
   Le loader du kernel cherche le symbole `init_module` dans ton .ko pour
   savoir quoi appeler. C'est le contrat entre ton module et le kernel.

3. `___ADDRESSABLE(init_module, __initdata);`
   Empeche le compilateur de supprimer le symbole lors de l'optimisation.

Verifie apres compilation:

```bash
nm src/hello.ko | grep -E "init_module|hello_init"
```

Resultat attendu:

```
0000000000000000 T init_module
0000000000000000 t hello_init
```

Meme adresse. `T` = global (visible par le kernel), `t` = local (static).
L'alias `init_module` est le point d'entree que le loader utilise.

`module_exit` fonctionne pareil avec `cleanup_module`:

```c
/* include/linux/module.h, v6.10 */
#define module_exit(exitfn)                                     \
    static inline exitcall_t __maybe_unused __exittest(void)    \
    { return exitfn; }                                          \
    void cleanup_module(void) __copy(exitfn)                    \
        __attribute__((alias(#exitfn)));                         \
    ___ADDRESSABLE(cleanup_module, __exitdata);
```


## Etape 6: declarer la licence

```c
MODULE_LICENSE("GPL");
```

La chaine de macros dans `include/linux/module.h` et
`include/linux/moduleparam.h` (v6.10):

```c
/* include/linux/module.h */
#define MODULE_LICENSE(_license) MODULE_FILE MODULE_INFO(license, _license)

/* include/linux/module.h */
#define MODULE_INFO(tag, info) __MODULE_INFO(tag, tag, info)

/* include/linux/moduleparam.h */
#define __MODULE_INFO(tag, name, info)                            \
    static const char __UNIQUE_ID(name)[]                         \
        __used __section(".modinfo") __aligned(1)                 \
        = __MODULE_INFO_PREFIX __stringify(tag) "=" info
```

Resultat concret: une string `"license=GPL"` placee dans la section ELF
`.modinfo` du .ko. C'est tout. Pas de magie.

Pourquoi c'est necessaire: au chargement, le kernel lit `.modinfo` et
appelle cette fonction (`include/linux/license.h`, v6.10):

```c
/* include/linux/license.h, v6.10 */
static inline int license_is_gpl_compatible(const char *license)
{
    return (strcmp(license, "GPL") == 0
        || strcmp(license, "GPL v2") == 0
        || strcmp(license, "GPL and additional rights") == 0
        || strcmp(license, "Dual BSD/GPL") == 0
        || strcmp(license, "Dual MIT/GPL") == 0
        || strcmp(license, "Dual MPL/GPL") == 0);
}
```

Si ta licence n'est pas GPL-compatible:
- Le kernel marque ton module "tainted"
- Les symboles `EXPORT_SYMBOL_GPL()` deviennent INACCESSIBLES a ton module

Beaucoup de fonctions internes du kernel sont exportees GPL-only. Sans "GPL",
`insmod` echouera avec "Unknown symbol" des que ton module essaie d'appeler
une de ces fonctions.

Verifie les metadonnees:

```bash
modinfo src/hello.ko
# Ou en brut:
objdump -s -j .modinfo src/hello.ko
```


## Le module complet

A ce stade, tu comprends chaque ligne. Voici le module complet:

```c
#include <linux/module.h>    /* struct module, module_init, MODULE_LICENSE, etc. */
#include <linux/init.h>      /* __init, __exit */

static int __init hello_init(void)
{
    pr_info("rootkitlings/001: hello from kernel space\n");
    return 0;
}

static void __exit hello_exit(void)
{
    pr_info("rootkitlings/001: goodbye from kernel space\n");
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPL");
```

C'est `src/hello.c`, avec en plus `current->pid` et `MODULE_AUTHOR` /
`MODULE_DESCRIPTION` (expliques ci-dessous).


## Bonus: current (dans src/hello.c)

Le fichier `src/hello.c` utilise aussi `current->pid` et `current->comm`.
Ce n'est pas necessaire pour un module minimal, mais ca montre quelque chose
d'important.

`current` est un macro. Voici le vrai code, fichier COMPLET
`arch/x86/include/asm/current.h` (v6.10):

```c
/* arch/x86/include/asm/current.h, v6.10 (simplifie: ifdefs retires) */

struct pcpu_hot {
    union {
        struct {
            struct task_struct  *current_task;
            int                 preempt_count;
            int                 cpu_number;
            unsigned long       top_of_stack;
            void                *hardirq_stack_ptr;
            u16                 softirq_pending;
            bool                hardirq_stack_inuse;
        };
        u8 pad[64];
    };
};
static_assert(sizeof(struct pcpu_hot) == 64);

DECLARE_PER_CPU_ALIGNED(struct pcpu_hot, pcpu_hot);

static __always_inline struct task_struct *get_current(void)
{
    if (IS_ENABLED(CONFIG_USE_X86_SEG_SUPPORT))
        return this_cpu_read_const(const_pcpu_hot.current_task);

    return this_cpu_read_stable(pcpu_hot.current_task);
}

#define current get_current()
```

`DECLARE_PER_CPU` : chaque CPU physique a sa propre copie de `pcpu_hot`.
Le premier champ est `current_task`, un pointeur vers le `struct task_struct`
du processus en cours d'execution sur ce CPU.

`this_cpu_read_stable` : lit ce champ via le segment register GS (sur
x86_64). Le kernel configure le GS base au boot pour pointer vers la zone
per-CPU. C'est un acces memoire direct, une instruction assembleur.

Quand le scheduler change de processus sur un CPU, il met a jour
`pcpu_hot.current_task`. Donc `current` pointe toujours vers le bon process.

Quand `insmod` fait le syscall `init_module`, c'est le processus `insmod` qui
execute en kernel mode. Donc `current->comm` = "insmod", `current->pid` =
PID de insmod.

`struct task_struct` (dans `include/linux/sched.h`) est LA structure centrale
du kernel. Chaque processus/thread en a une. Elle contient tout:

```c
/* include/linux/sched.h, extrait */
struct task_struct {
    pid_t pid;
    char comm[TASK_COMM_LEN];    /* nom de l'executable, 16 chars max */
    const struct cred *cred;     /* credentials: uid, gid, capabilities */
    struct mm_struct *mm;        /* espace memoire userspace */
    /* ... des centaines d'autres champs ... */
};
```

On reviendra sur `cred` quand on fera l'escalation de privilege (exercice 035).


## Compiler et observer

**Le Makefile** (`Makefile` a la racine de l'exercice):

```makefile
obj-m += hello.o
obj-m += challenge.o

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	make -C $(KDIR) M=$(PWD)/src modules

clean:
	make -C $(KDIR) M=$(PWD)/src clean
```

Pourquoi `make -C $(KDIR)` au lieu de `gcc hello.c`? Le kernel a son propre
systeme de build (kbuild) qui ajoute des flags necessaires. Fais `make V=1`
pour voir les commandes reelles. Tu verras gcc appele avec:

- `-nostdinc` : ignore les headers libc. Pas de libc en kernel space.
- `-D__KERNEL__` : active le code kernel dans les headers (beaucoup ont
  des `#ifdef __KERNEL__`).
- `-DMODULE` : dit aux macros module_init/module_exit d'utiliser la branche
  "module" (alias vers init_module).
- `-DKBUILD_MODNAME='"hello"'` : nom du module comme string.

`obj-m` : compile comme module (.ko). `obj-y` compilerait en built-in
(integre au kernel).

**Sur l'hote (Arch):**

```bash
cd exercises/001-hello-lkm
make
```

**Examine ce que kbuild a produit:**

```bash
# le .ko existe
ls -la src/hello.ko

# sections ELF (repere .init.text, .exit.text, .modinfo)
readelf -S src/hello.ko

# symboles (repere init_module / hello_init a la meme adresse)
nm src/hello.ko | grep -E "init_module|hello_init|cleanup_module|hello_exit"

# metadonnees
modinfo src/hello.ko

# section .modinfo en brut (tu verras "license=GPL", "author=...", etc.)
objdump -s -j .modinfo src/hello.ko
```

**Dans la VM (vng ou qemu):**

```bash
# charge le module
insmod src/hello.ko

# lis les messages (tu verras "hello from kernel space" + PID de insmod)
dmesg | tail -5

# le module est dans la liste
lsmod | grep hello

# pareil, source brute (/proc/modules est ce que lsmod lit)
cat /proc/modules | grep hello

# representation sysfs
ls /sys/module/hello/

# decharge
rmmod hello

# messages de sortie
dmesg | tail -3
```

**insmod concretement** : c'est un programme du paquet `kmod`. Il lit le .ko
et appelle le syscall `finit_module()`:

```c
/* kernel/module/main.c, v6.10 */
SYSCALL_DEFINE3(init_module, void __user *, umod,
        unsigned long, len, const char __user *, uargs)
{
    int err;
    struct load_info info = { };

    err = may_init_module();
    if (err)
        return err;
    return load_module(&info, uargs, 0);
}
```

`load_module()` fait: verif signature, verif ELF, allocation memoire kernel,
resolution des symboles, ajout a la liste `modules`, appel de ta
`init_module()`.

**rmmod concretement** : appelle `delete_module()`:

```c
/* kernel/module/main.c, v6.10 */
SYSCALL_DEFINE2(delete_module, const char __user *, name_user,
        unsigned int, flags)
{
    struct module *mod;
    char name[MODULE_NAME_LEN];
    /* ... */
    mod = find_module(name);
    /* appelle mod->exit(), retire de la liste, libere memoire */
}
```

`struct module` en memoire (les champs importants pour la suite):

```c
/* include/linux/module.h, v6.10 */
struct module {
    enum module_state state;         /* LIVE, COMING, GOING */
    struct list_head list;           /* chaine de TOUS les modules charges */
    char name[MODULE_NAME_LEN];
    struct module_kobject mkobj;     /* sysfs: /sys/module/xxx */
    /* ... */
};
```

`list` : on s'en servira pour cacher le module de lsmod (exercice 026).
`mkobj` : pour le cacher de /sys/module (exercice 027).


## Challenge

Ecris `src/challenge.c` from scratch (fichier vide, tu tapes tout). Le module
doit:

1. Au chargement: `rootkitlings/001: <ton login> reporting for duty`
2. Au dechargement: `rootkitlings/001: <ton login> out`
3. MODULE_LICENSE("GPL"), MODULE_AUTHOR (ton login), MODULE_DESCRIPTION

Contrainte: ecris-le sans regarder hello.c. Si tu ne peux pas, relis les
etapes ci-dessus.

```bash
make
./test.sh   # dans la VM
```


## Hint

Si tu bloques: `hint.md` (3 niveaux).


## Pour aller plus loin

`deepdive.md` quand tu as fini.

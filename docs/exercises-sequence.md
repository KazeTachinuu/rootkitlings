# Sequence des 50 exercices rootkitlings

Chaque exercice est un dossier dans exercises/. La colonne "lignes" indique
combien de lignes de code l'etudiant ecrit. Le mode VM indique vng (rapide)
ou qemu (isolation totale).

## Phase 1: LKM basics (001-010)

| # | Nom | Ce qu'on apprend | Lignes | VM |
|---|-----|-------------------|--------|-----|
| 001 | hello-lkm | module_init, module_exit, printk, dmesg | 3 | vng |
| 002 | module-info | MODULE_LICENSE, MODULE_AUTHOR, MODULE_DESCRIPTION | 3 | vng |
| 003 | module-params | module_param, permissions, changer un param au load | 5 | vng |
| 004 | proc-create | proc_create, proc_ops, creer une entree /proc | 6 | vng |
| 005 | proc-read | seq_file, seq_printf, lire depuis userspace | 5 | vng |
| 006 | proc-write | copy_from_user, ecrire dans /proc depuis userspace | 8 | vng |
| 007 | kmalloc-kfree | kmalloc, kfree, GFP_KERNEL, gestion memoire kernel | 6 | vng |
| 008 | linked-lists | struct list_head, list_add, list_for_each_entry, INIT_LIST_HEAD | 8 | vng |
| 009 | workqueue | create_singlethread_workqueue, queue_delayed_work, action differee | 7 | vng |
| 010 | kthread | kthread_run, kthread_should_stop, kthread_stop, boucle kernel | 8 | vng |

## Phase 2: Explorer le kernel (011-015)

| # | Nom | Ce qu'on apprend | Lignes | VM |
|---|-----|-------------------|--------|-----|
| 011 | kallsyms-read | /proc/kallsyms, cat, grep, comprendre la table des symboles | 0 (exploration) | vng |
| 012 | kallsyms-kprobe | kprobe trick pour obtenir kallsyms_lookup_name post-5.7 | 6 | vng |
| 013 | task-struct | current, current->comm, current->pid, struct task_struct | 5 | vng |
| 014 | creds | current_cred(), uid, gid, struct cred, comprendre les permissions | 4 | vng |
| 015 | syscall-flow | comprendre le chemin d'un syscall du userspace au kernel | 0 (exploration) | vng |

## Phase 3: Hooking (016-025)

| # | Nom | Ce qu'on apprend | Lignes | VM |
|---|-----|-------------------|--------|-----|
| 016 | kprobe-basic | register_kprobe, pre_handler, intercepter un appel kernel | 6 | qemu |
| 017 | kretprobe | register_kretprobe, ret_handler, intercepter la valeur de retour | 7 | qemu |
| 018 | ftrace-explore | tracefs, /sys/kernel/tracing, available_filter_functions | 0 (exploration) | qemu |
| 019 | ftrace-ops | struct ftrace_ops, ftrace_set_filter_ip, register_ftrace_function | 8 | qemu |
| 020 | ftrace-hook-fn | ecrire la callback ftrace, sauver/restaurer les regs, appeler l'original | 10 | qemu |
| 021 | ftrace-helper | utiliser ftrace_helper.h (TheXcellerator), simplifier le hooking | 5 | qemu |
| 022 | hook-mkdir | hooker __x64_sys_mkdir, log chaque creation de dossier | 5 | qemu |
| 023 | hook-kill | hooker __x64_sys_kill, intercepter un signal specifique | 6 | qemu |
| 024 | hook-getdents | hooker __x64_sys_getdents64, comprendre le buffer de dirent | 8 | qemu |
| 025 | hook-cleanup | unhook propre, synchronize_rcu, eviter les panics | 4 | qemu |

## Phase 4: Hiding (026-033)

| # | Nom | Ce qu'on apprend | Lignes | VM |
|---|-----|-------------------|--------|-----|
| 026 | hide-module-list | list_del(&THIS_MODULE->list), disparaitre de lsmod | 1 | qemu |
| 027 | hide-module-sysfs | kobject_del(&THIS_MODULE->mkobj.kobj), disparaitre de /sys/module | 1 | qemu |
| 028 | unhide-module | sauver les pointeurs, re-linker pour rmmod propre | 6 | qemu |
| 029 | hide-files | filtrer getdents64 par prefixe, retirer des entries du buffer | 10 | qemu |
| 030 | hide-directory | cacher un dossier complet (le repaire du rootkit) | 4 | qemu |
| 031 | hide-process | filtrer getdents sur /proc, cacher un PID | 8 | qemu |
| 032 | hide-port | filtrer /proc/net/tcp, cacher une connexion reseau | 8 | qemu |
| 033 | hide-file-content | hooker read sur un fichier specifique, retirer une ligne | 10 | qemu |

## Phase 5: Privilege escalation (034-037)

| # | Nom | Ce qu'on apprend | Lignes | VM |
|---|-----|-------------------|--------|-----|
| 034 | read-creds | lire les creds du processus courant depuis le module | 4 | qemu |
| 035 | escalate-root | prepare_kernel_cred(NULL) + commit_creds, devenir root | 3 | qemu |
| 036 | escalate-pid | find_task_by_vpid, donner root a un PID specifique | 6 | qemu |
| 037 | magic-signal | hooker kill, signal magic (ex: 64) pour trigger l'escalation | 8 | qemu |

## Phase 6: Persistence (038-041)

| # | Nom | Ce qu'on apprend | Lignes | VM |
|---|-----|-------------------|--------|-----|
| 038 | modprobe-persist | /etc/modules-load.d/, charger un module au boot | 3 (config) | qemu |
| 039 | systemd-persist | ecrire un .service, systemd-based persistence | 5 (config) | qemu |
| 040 | initramfs | modifier initramfs pour inclure le module, mkinitramfs | 5 (script) | qemu |
| 041 | self-reload | call_usermodehelper pour se recharger apres rmmod | 6 | qemu |

## Phase 7: Communication (042-047)

| # | Nom | Ce qu'on apprend | Lignes | VM |
|---|-----|-------------------|--------|-----|
| 042 | kernel-socket | sock_create_kern, struct sockaddr_in, kernel_connect | 8 | qemu |
| 043 | kernel-send | kernel_sendmsg, struct msghdr, struct kvec | 6 | qemu |
| 044 | kernel-recv | kernel_recvmsg, lire la reponse du serveur | 6 | qemu |
| 045 | c2-protocol | definir un protocole simple (type + longueur + data) | 8 | qemu |
| 046 | exec-command | call_usermodehelper, UMH_WAIT_PROC, capturer stdout | 8 | qemu |
| 047 | c2-loop | kthread + boucle: connect, beacon, recv cmd, exec, send result | 10 | qemu |

## Phase 8: Crypto (048-050)

| # | Nom | Ce qu'on apprend | Lignes | VM |
|---|-----|-------------------|--------|-----|
| 048 | xor-cipher | XOR simple sur le buffer avant envoi | 5 | qemu |
| 049 | kernel-crypto | crypto_alloc_skcipher, AES-CBC, kernel crypto API | 10 | qemu |
| 050 | encrypted-c2 | integrer le chiffrement dans la boucle C2 | 8 | qemu |

## Liens entre exercices et features WLKOM

| Feature WLKOM | Points | Exercices preparatoires |
|---------------|--------|------------------------|
| Compile | 0.5 | 001-003 |
| Connection | 3.0 | 042-044, 047 |
| Persistence | 1.5 | 038-041 |
| Executing commands | 5.0 | 045-047 |
| Password | 1.0 | 048 (XOR ou derive) |
| Upload | 1.5 | 043-044, 050 |
| Download | 1.5 | 043-044, 050 |
| Hide module | 1.0 | 026-028 |
| Hide line | 2.0 | 033 |
| Hide dir/files | 2.0 | 029-030 |
| Crypto | 1.0 | 048-050 |

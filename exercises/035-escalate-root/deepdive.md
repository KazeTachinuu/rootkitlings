# Deepdive 035

Tu as fait la technique classique d'escalation kernel. Questions:

1. `prepare_kernel_cred(NULL)` utilise `init_cred`. Mais que se passe-t-il
   si le kernel a CONFIG_SECURITY_SELINUX ou CONFIG_SECURITY_APPARMOR?
   Est-ce que les LSM (Linux Security Modules) bloquent l'escalation?
   Lis `security/commoncap.c` et `security/selinux/hooks.c`.

2. Depuis le kernel 6.2, `prepare_kernel_cred(NULL)` est parfois modifie
   pour ne plus copier init_cred directement. Cherche les commits recents
   sur ce sujet. Comment les rootkits s'adaptent-ils?

3. Alternative: au lieu de prepare_kernel_cred, on peut modifier les
   credentials existantes:
   ```c
   struct cred *cred = prepare_creds();
   cred->uid = cred->euid = GLOBAL_ROOT_UID;
   commit_creds(cred);
   ```
   Quelle est la difference? (Indice: les capabilities ne sont pas
   modifiees avec cette methode.)

4. LKRG (Linux Kernel Runtime Guard) detecte les changements de
   credentials non autorises. Comment? Lis le code de LKRG pour
   comprendre son mecanisme de detection.

L'exercice 036 etend cette technique pour donner root a un PID specifique.

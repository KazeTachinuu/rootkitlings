# Deepdive: 003 module-params

1. Quand les permissions sont 0644, le parametre est modifiable en live via
   /sys/module/params/parameters/myint. Essaie de changer la permission
   a 0644 et apres chargement, fais:
   ```bash
   echo 99 > /sys/module/params/parameters/myint
   ```
   Le kernel modifie la variable en memoire sans recharger le module.

2. Un rootkit peut utiliser les parametres module pour recevoir de la
   configuration: port de C2, cle de chiffrement, prefixe de fichiers
   a cacher. Mais ces parametres sont visibles dans /sys/module/.
   A quel exercice on apprend a cacher le module de /sys ?

3. Cherche `module_param_array` dans moduleparam.h. Comment passer
   un tableau de valeurs a un module ?

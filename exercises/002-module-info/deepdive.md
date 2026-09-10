# Deepdive: 002 module-info

1. Les metadonnees du module sont stockees dans une section ELF speciale.
   Utilise `readelf -S src/info.ko` et cherche la section `.modinfo`.
   Puis `readelf -x .modinfo src/info.ko` pour voir le contenu brut.

2. Que se passe-t-il si tu mets MODULE_LICENSE("Proprietary") ?
   Essaie de charger le module et regarde dmesg. Tu verras un warning
   "module ... taints kernel". Cherche "tainted kernel" dans la doc.

3. Un rootkit voudrait-il mettre une licence GPL ou pas ? Reflechis:
   GPL donne acces a plus de symboles kernel, mais marquer le module
   GPL le rend visible d'une certaine facon. Quel compromis ?

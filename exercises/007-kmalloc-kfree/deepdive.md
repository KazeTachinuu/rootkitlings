# Deepdive: 007 kmalloc-kfree

1. Active kmemleak dans ta VM (si le kernel est compile avec
   CONFIG_DEBUG_KMEMLEAK). Charge ton module, decharge-le SANS
   appeler kfree, puis:
   ```bash
   echo scan > /sys/kernel/debug/kmemleak
   cat /sys/kernel/debug/kmemleak
   ```
   Tu devrais voir ta fuite. C'est un outil de debug puissant.

2. kmalloc utilise l'allocateur SLUB en interne. Les allocations sont
   regroupees par taille dans des "caches". Regarde:
   ```bash
   cat /proc/slabinfo
   ```
   Tu verras les caches kmalloc-256, kmalloc-512, etc. Un outil forensic
   peut scanner ces caches pour trouver des donnees du rootkit en memoire.

3. `vmalloc` est une alternative a kmalloc pour les grosses allocations
   (la memoire n'est pas contigue physiquement). Un rootkit qui charge
   du code supplementaire en memoire utiliserait vmalloc. Cherche
   `include/linux/vmalloc.h`.

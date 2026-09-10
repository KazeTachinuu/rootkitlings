# Deepdive 027

Tu as cache le module de lsmod ET de /sys/module/. Mais il y a d'autres
endroits ou un module laisse des traces:

1. /proc/kallsyms contient les symboles exportes par ton module
2. Les messages printk dans dmesg
3. La memoire kernel elle-meme (le struct module est toujours alloue)

Question: comment un outil de forensics comme Volatility peut-il retrouver
un module qui a fait list_del + kobject_del?

Indice: le kernel alloue les struct module dans le slab allocator. Un scan
lineaire de la memoire kernel a la recherche de signatures connues (magic
numbers, pointeurs vers des zones .text/.data valides) permet de les retrouver.

Cherche "Volatility linux.hidden_modules" pour voir la technique.

L'exercice 028 resout un probleme pratique: comment faire rmmod proprement
quand le module est cache.

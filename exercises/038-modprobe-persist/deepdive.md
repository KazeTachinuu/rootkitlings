# Deepdive 038: persistence via modules-load.d

## Question ouverte

Un admin sys fait tourner `systemctl status systemd-modules-load` et voit
que ton module est charge au boot. Comment le detecterait-il ?

Cherche quels logs systemd-modules-load produit dans `journalctl`.
Quel fichier un defenseur surveillerait-il pour detecter un nouveau module
ajoute a /etc/modules-load.d/ ?

## Piste

Regarde les outils de monitoring d'integrite de fichiers (AIDE, OSSEC, auditd).
Comment auditd peut-il surveiller les ecritures dans /etc/modules-load.d/ ?

```bash
auditctl -w /etc/modules-load.d/ -p wa -k module_persist
```

L'exercice 039 explore une autre methode de persistence: systemd services.

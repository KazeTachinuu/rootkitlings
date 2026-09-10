# Hints: 009 workqueue

## Niveau 1

INIT_DELAYED_WORK prend l'adresse de ta struct delayed_work et la
fonction callback. schedule_delayed_work prend la meme struct et un
delai en jiffies (utilise msecs_to_jiffies pour convertir).

## Niveau 2

Dans l'init:
```c
INIT_DELAYED_WORK(&my_work, work_callback);
schedule_delayed_work(&my_work, msecs_to_jiffies(1000));
```

Dans la callback, pour re-planifier:
```c
schedule_delayed_work(&my_work, msecs_to_jiffies(1000));
```

## Niveau 3

```c
// init:
INIT_DELAYED_WORK(&my_work, work_callback);
schedule_delayed_work(&my_work, msecs_to_jiffies(1000));

// callback (derniere ligne):
schedule_delayed_work(&my_work, msecs_to_jiffies(1000));

// exit:
cancel_delayed_work_sync(&my_work);
```

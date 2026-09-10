# Hints, exercice 036

## Niveau 1

`find_task_by_vpid` cherche un processus par son PID dans le namespace
courant. Il faut l'appeler entre `rcu_read_lock()` et `rcu_read_unlock()`.

## Niveau 2

```c
rcu_read_lock();
task = find_task_by_vpid(target_pid);
rcu_read_unlock();
if (!task)
    return -ESRCH;
new_cred = prepare_kernel_cred(NULL);
```

## Niveau 3

```c
rcu_read_lock();
task = find_task_by_vpid(target_pid);
if (!task) {
    rcu_read_unlock();
    return -ESRCH;
}
rcu_read_unlock();

new_cred = prepare_kernel_cred(NULL);
if (new_cred) {
    rcu_assign_pointer(task->real_cred, new_cred);
    rcu_assign_pointer(task->cred, new_cred);
}
```

Note: en vrai il faudrait `put_cred` sur l'ancien cred pour eviter un
memory leak. Pour cet exercice pedagogique, on simplifie.

#!/bin/bash
# Ce script est execute par le kernel via call_usermodehelper
# quand le module se decharge. Il attend puis recharge le module.
sleep 1
insmod /tmp/reload.ko

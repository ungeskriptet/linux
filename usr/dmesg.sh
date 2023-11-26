#!/bin/busybox sh
while :; do busybox dmesg | busybox less; done

#!/bin/sh
echo "53" >  /sys/class/gpio/export
sleep 1
echo "out" > /sys/class/gpio/gpio53/direction
sleep 1
while ((1))
do
echo "1" > /sys/class/gpio/gpio53/value

sleep 1

echo "0" > /sys/class/gpio/gpio53/value

sleep 3
done


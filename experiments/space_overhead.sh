#!/bin/bash

# Script to automate acquisition of space overhead data

bm="./sd-vbs-bin/disparity/data/vga/disparity ./sd-vbs-bin/disparity/data/vga/"
period=100

## Test with in-line snapshot storage, address resoltuion, layout acquisition
echo "Flush Mode"
for i in $(seq 30)
do
    
    sudo ./snapshot "$bm" -o /tmp/overhead -fr -p $period -h 1>/dev/null 2>/dev/null
    difflines=$(sudo diff /tmp/overhead/cachedump1.csv /tmp/overhead/cachedump2.csv | grep "^>" | wc -l)
    totlines=$(sudo cat /tmp/overhead/cachedump1.csv | wc -l)
    python -c "print(100 * float($difflines)/$totlines)"
done

## Test with address resoltuion, layout acquisition (transparent mode)
echo "Resolve+Layout Mode"
for i in $(seq 30)
do
    sudo ./snapshot "$bm" -o /tmp/overhead -fr -p $period -t -h 1>/dev/null 2>/dev/null
    difflines=$(sudo diff /tmp/overhead/cachedump1.csv /tmp/overhead/cachedump2.csv | grep "^>" | wc -l)
    python -c "print(100 * float($difflines)/$totlines)"
done
    
## Test with address resoltuion, (transparent mode)
echo "Resolve Mode"
for i in $(seq 30)
do
    sudo ./snapshot "$bm" -o /tmp/overhead -fr -p $period -t -l -h 1>/dev/null 2>/dev/null
    difflines=$(sudo diff /tmp/overhead/cachedump1.csv /tmp/overhead/cachedump2.csv | grep "^>" | wc -l)
    python -c "print(100 * float($difflines)/$totlines)"
done
    
echo "Layout Mode"
## Test with layout acquisition, (transparent mode)
for i in $(seq 30)
do
    sudo ./snapshot "$bm" -o /tmp/overhead -fr -p $period -t -n -h 1>/dev/null 2>/dev/null
    difflines=$(sudo diff /tmp/overhead/cachedump1.csv /tmp/overhead/cachedump2.csv | grep "^>" | wc -l)
    python -c "print(100 * float($difflines)/$totlines)"
done
    
echo "Full Transparent Mode"
## Test in full transparent mode
for i in $(seq 30)
do
    sudo ./snapshot "$bm" -o /tmp/overhead -fr -p $period -t -n -l -h 1>/dev/null 2>/dev/null
    difflines=$(sudo diff /tmp/overhead/cachedump1.csv /tmp/overhead/cachedump2.csv | grep "^>" | wc -l)
    python -c "print(100 * float($difflines)/$totlines)"
done


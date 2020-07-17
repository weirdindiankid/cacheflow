#!/usr/bin/python

#######################################################
#                                                     #
# Cache snapshot plotting functions                   #
#                                                     #
# Author: Renato Mancuso (BU)                         #
# Date: April 2020                                    #
#                                                     #
#######################################################

import math
import os
import os.path
import sys
import numpy as np
import commands
import operator
import shutil
import matplotlib.pyplot as plt

from proc_maps_parse import *

path="../sdvbs_solo/"
bms=["disparity", "mser", "sift", "tracking", "bomb"]
spects={}
pids={}
quotas={}
reuse={}

tot_lines = float(2 * 1024 * 1024 / 64)

def plot_quota(b, ax):
    x = range(1, len(quotas[b])+1)
    y = quotas[b]

    ax.set(title=(b + " - Active"), ylabel='L2 lines (%)')
    ax.set_ylim(0, 1)
    ax.plot(x, y)    
    ax.fill_between(x, y, 0, alpha=0.5)
    plt.setp(ax.get_xticklabels(), visible=False)

def plot_reuse(b, ax):
    x = range(1, len(reuse[b])+1)
    y = reuse[b]
    
    ax.set(title=(b + " - Reused"))
    ax.set_ylim(0, 1)
    ax.plot(0,0)
    ax.plot(x, y)
    ax.fill_between(x, y, 0, alpha=0)
    ax.fill_between(x, y, 0, alpha=0.5)
    plt.setp(ax.get_xticklabels(), visible=False)
    
for b in bms:
    spects[b] = MemSpectrum(0, path + b + "/pids.txt", 1, 500)
    pids[b] = spects[b].other_pids[-1]
    quotas[b] = []
    reuse[b] = []
    
for b in bms:
    for a in bms:
        b_spect = spects[b]
        a_spect = spects[a]

        b_pid = pids[b]
        a_pid = pids[a]

        # Get the number of snapshots for this bm
        count = len(b_spect.dumps)
        
        tot_quota = 0.0
        tot_reused = 0.0
        ua_prev_dump = None
        in_prev_dump = None
        
        for i, d in enumerate(b_spect.dumps):
            ua_quota = d.pid_accesses[b_pid].get_count() / tot_lines

            if(len(quotas[b]) < len(b_spect.dumps)):
                quotas[b].append(ua_quota)
                
            ua_reused = 0            
            in_quota = 0
            in_reused = 0

            if ua_prev_dump != None:
                ua_reused = d.pid_accesses[b_pid].get_reused_blocks(ua_prev_dump.pid_accesses[b_pid])

            if(len(reuse[b]) < len(b_spect.dumps)):
                reuse[b].append(ua_reused / tot_lines)
            
            if i < len(a_spect.dumps):
                in_d = a_spect.dumps[i]
                in_quota = in_d.pid_accesses[a_pid].get_count() / tot_lines
                in_reused = 0
                
                if in_prev_dump != None:
                    in_reused = in_d.pid_accesses[a_pid].get_reused_blocks(in_prev_dump.pid_accesses[a_pid])
                    
                in_prev_dump = in_d
            ua_prev_dump = d

            # Compute quota exceeding cache size
            tot_quota += max(0, (ua_quota + in_quota) - 1.0)
            tot_reused += (ua_reused / tot_lines) * in_quota
            
        max_quota = 1.0 * len(b_spect.dumps)
        
        print "BM %s intefered by %s: %f (reused = %f)" % (b, a, float(tot_quota) / max_quota, float(tot_reused) / max_quota)

i = 1
for b in bms:

    ax = plt.subplot(len(bms), 2, i)
    plot_quota(b, ax)
    i+=1

    ax = plt.subplot(len(bms), 2, i)
    plot_reuse(b, ax)
    i+=1

fig = plt.gcf()    
fig.set_size_inches(5.5, 6)
plt.tight_layout()
plt.show()

fig.savefig("characteristics.png", dpi=fig.dpi, bbox_inches='tight');
fig.savefig("characteristics.pdf", dpi=fig.dpi, bbox_inches='tight');

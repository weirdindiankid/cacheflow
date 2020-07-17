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
import matplotlib.ticker as ticker
from matplotlib.ticker import MaxNLocator
import matplotlib.gridspec as gridspec
from collections import OrderedDict
import matplotlib.colors as mcolors

from proc_maps_parse import *

def autoselect_regions(spect, pid, count):

    ## Take regions from second dump
    regions = spect.dumps[5].pid_regions[pid]

    totals = []
    for i, r in enumerate(regions):
        totals.append((i, spect.get_total_blocks_in_region(pid, i), \
                       spect.get_range_blocks_in_region(pid, i)))

    totals = sorted(totals, key=operator.itemgetter(1), reverse=False)
    totals = totals[-count:]

    ranges = [max(1, math.log(1 + x[2] / 0x1000)) for x in totals]
    totals = [x[0] for x in totals]

    # Compute appropriate horizontal ratios
    print ranges
    
    # return total and horizontal ratios
    return (totals, ranges)
        
def plot_spectrum(spect, pid, path, scale = 1.5):

    ann = []
    axs = []
    
    fig = plt.figure(figsize=(10, 5))

    # Select regions to plot, expressed as IDs
    sel_regions, h_r = autoselect_regions(spect, pid, 4)

    print "Regions: " + str(sel_regions)
    # Select ratios for regions
    #h_r = [1, 1, 2, 5]
    
    cols = ["r", "green", "black", "blue", "yellow"]
    
    #fig, axs = plt.subplots(2, sharex=True, sharey=False, figsize=(10, 5))
    # ann.append(plot_spectrum_region(spect, pid, 3, axs[1], 'r'))
    # ann.append(plot_spectrum_region(spect, pid, 0, axs[0], 'b'))
    
    spec = gridspec.GridSpec(ncols=1, nrows=len(sel_regions), height_ratios=h_r, hspace=0.15)

    cmap=plt.get_cmap("Dark2")
    
    for i, r in enumerate(sel_regions):
        axs.append(fig.add_subplot(spec[i]))
        ann.append(plot_spectrum_region_heatm(spect, pid, r, axs[-1], cols[i], h_r[i], scale))

    # Hide x labels and tick labels for all but bottom plot.
    for ax in axs:
        ax.label_outer()

    plt.show()
    fig.savefig(path + "/plot.png", dpi=fig.dpi, bbox_extra_artists=(ann), bbox_inches='tight');
    fig.savefig(path + "/plot.pdf", dpi=fig.dpi, bbox_extra_artists=(ann), bbox_inches='tight');
    
def plot_spectrum_region(spect, pid, region, axs, col = "r", reg_scale = 1, point_scale = 1.5):

    min_y = None
    max_y = None 
    reg = None
    
    #print "==== REG %i === " % (region)
    
    for i, d in enumerate(spect.dumps):
        acc = d.pid_accesses[pid]

        if region not in acc.regions_to_pages:
            continue
        
        pages = np.array(acc.regions_to_pages[region])
        reg = d.pid_regions[pid]
        area = acc.get_weights(pages, point_scale)
        reg_size = reg[region].end - reg[region].start

        pages = (pages - reg[region].start) / 0x1000
        
        #print "%i) PAGES: %i" % (i, (cur_reg[region].end - cur_reg[region].start) / 0x1000)
        
        x = [i]*len(acc.regions_to_pages[region])        
        axs.scatter(x, pages, s=area, color=col, marker="s", alpha=0.5)
        #axs.hist2d(x, pages, weights=area, normed=False)
        
        if len(pages) > 0:
            cur_min = min(pages)
            cur_max = max(pages)
        
            if min_y == None or min_y > cur_min:
                min_y = cur_min

            if max_y == None or max_y < cur_max:
                max_y = cur_max

    print hex(min_y)
    print hex(max_y)

    axs.set_xlim((-0.5, len(spect.dumps)-0.5))
    
    axs.set_xlabel("Snapshot #")

    print "%i) MAX %i - MIN %i" % (region, max_y, min_y)
    
    # Select multiple locato for axis depending on size of axis
    if (max_y - min_y > 3 and reg_scale < 2):
        axs.yaxis.set_major_locator(ticker.MultipleLocator((max_y - min_y) / 2))
        axs.set_ylim((min_y - 0.5, max_y + 0.5))
    elif (max_y - min_y > 3):
        axs.yaxis.set_major_locator(ticker.MultipleLocator((max_y - min_y) / 6))
        #axs.set_ylim((min_y - 0.5, max_y + 0.5))        
    elif (max_y != min_y):
        axs.yaxis.set_major_locator(ticker.MultipleLocator(1))
    else:
        axs.yaxis.set_major_locator(ticker.FixedLocator([min_y]))

    axs.xaxis.set_major_locator(ticker.MultipleLocator(max(1, len(spect.dumps)/20)))
    axs.get_yaxis().set_major_formatter(ticker.FuncFormatter(lambda x, pos: "+%i" % (x)))

    ## Add annotation to y axis to show name of the area and arrow range
    off = -(len(spect.dumps))/float(8.5)

    if (min_y == max_y):
        axs.annotate('', xy=(off, min_y-0.05), xytext=(off, max_y+0.05),
        arrowprops=dict(arrowstyle='|-|',facecolor='red'),
        annotation_clip=False)

    else:
        axs.annotate('', xy=(off, min_y), xytext=(off, max_y),
                arrowprops=dict(arrowstyle='|-|',facecolor='red'),
                annotation_clip=False)

    center = (max_y + min_y) / float(2)
    ann = axs.annotate(reg[region].short_name, xy=(1.8*off, center), xytext=(1.8*off, center),
                annotation_clip=False)

    return ann


def plot_spectrum_region_heatm(spect, pid, region, axs, col = "r", reg_scale = 1, point_scale = 1.5):

    min_y = None
    max_y = None 
    reg = None
    
    #print "==== REG %i === " % (region)

    x = []
    y = []
    
    for i, d in enumerate(spect.dumps):
        acc = d.pid_accesses[pid]

        if region not in acc.regions_to_pages:
            continue
        
        pages = np.array(acc.regions_to_pages[region])
        reg = d.pid_regions[pid]
        reg_size = reg[region].end - reg[region].start
        offsets = (pages - reg[region].start) / 0x1000

        for p in range(0, len(pages)):
            blocks = acc.pages[pages[p]]
            x.extend([i] * blocks)
            y.extend([offsets[p]] * blocks)
        
        if len(offsets) > 0:
            cur_min = min(offsets)
            cur_max = max(offsets)
        
            if min_y == None or min_y > cur_min:
                min_y = cur_min

            if max_y == None or max_y < cur_max:
                max_y = cur_max

    print hex(min_y)
    print hex(max_y)

    axs.hist2d(x, y, normed=True, bins=[len(spect.dumps), min(max_y - min_y + 1, 50)], \
               norm=mcolors.PowerNorm(0.3), range=[(-0.5, len(spect.dumps)-0.5), (min_y, max_y)])
    
    axs.set_xlim((-0.5, len(spect.dumps)-0.5))
    
    axs.set_xlabel("Snapshot #")

    print "%i) MAX %i - MIN %i" % (region, max_y, min_y)
    
    # Select multiple locato for axis depending on size of axis
    if (max_y - min_y > 3 and reg_scale < 2):
        axs.yaxis.set_major_locator(ticker.MultipleLocator((max_y - min_y) / 2))
        axs.set_ylim((min_y - 0.5, max_y + 0.5))
    elif (max_y - min_y > 3):
        axs.yaxis.set_major_locator(ticker.MultipleLocator((max_y - min_y) / 6))
        #axs.set_ylim((min_y - 0.5, max_y + 0.5))        
    elif (max_y != min_y):
        axs.yaxis.set_major_locator(ticker.MultipleLocator(1))
    else:
        axs.yaxis.set_major_locator(ticker.FixedLocator([min_y]))

    axs.xaxis.set_major_locator(ticker.MultipleLocator(max(1, len(spect.dumps)/20)))
    axs.get_yaxis().set_major_formatter(ticker.FuncFormatter(lambda x, pos: "+%i" % (x)))

    ## Add annotation to y axis to show name of the area and arrow range
    off = -(len(spect.dumps))/float(8.5)

    if (min_y == max_y):
        axs.annotate('', xy=(off, min_y-0.05), xytext=(off, max_y+0.05),
        arrowprops=dict(arrowstyle='|-|',facecolor='red'),
        annotation_clip=False)

    else:
        axs.annotate('', xy=(off, min_y), xytext=(off, max_y),
                arrowprops=dict(arrowstyle='|-|',facecolor='red'),
                annotation_clip=False)

    center = (max_y + min_y) / float(2)
    ann = axs.annotate(reg[region].short_name, xy=(1.8*off, center), xytext=(1.8*off, center),
                annotation_clip=False)

    return ann

def plot_multipid_seq(spects, path, captions = [], bw = 0, ms = []):
    
    ann = []
    axs = []
    
    fig = plt.figure(figsize=(10, 5))
    count = len(spects)
    
    # Select regions to plot, expressed as IDs    
    spec = gridspec.GridSpec(ncols=1, nrows=count, hspace=0.15)

    cmap=plt.get_cmap("Dark2")
    
    for i, s in enumerate(spects):
        if i <= 0:
            axs.append(fig.add_subplot(spec[i]))
        else:
            axs.append(fig.add_subplot(spec[i], sharey=axs[0]))

        if ms:
            __plot_multipid_single(s, s.other_pids, axs[-1], count, captions,
                                   bw, "period = %i ms" % (ms[i]))
        else:
            __plot_multipid_single(s, s.other_pids, axs[-1], count, captions, bw, "")

    # Hide x labels and tick labels for all but bottom plot.
    for ax in axs:
        ax.label_outer()

    pids = len(spects[-1].other_pids) + 2
    ann.append(axs[0].legend(bbox_to_anchor=(0., 1.02, 1., .102),
                         loc='lower left', ncol=pids, mode="expand",
                         borderaxespad=0.))

    axs[0].set_ylim((0, 2*1024*1024/64))
    plt.xlabel("Snapshot #")
    plt.ylabel("L2 Cache Occupancy")

    plt.show()
    fig.savefig(path + "/plot_wss.png", dpi=fig.dpi, bbox_extra_artists=(ann), bbox_inches='tight');
    fig.savefig(path + "/plot_wss.pdf", dpi=fig.dpi, bbox_extra_artists=(ann), bbox_inches='tight');
 
    
def __plot_multipid_single(spect, pids, axs, plot_count, captions = [], bw = 0, annot = ""):

    ids = []
    bars = {}

    pids.append("other")
    pids.append("unres")

    if not captions:
        captions = array(pids)

    idx = 0
    for p in pids:
        bars[p] = []

        if idx >= len(captions):
            captions.append(p)
            
        idx += 1

    print captions
        
    cmap=plt.get_cmap("viridis", len(pids))

    colors = cmap.colors

    if bw == 0:
        w = 0.5
    else:
        w = bw

    start_p = {}
    
    for i, d in enumerate(spect.dumps):
        start = 0
        
        ids.append(i)
        idx = 0
        for p in pids:

            if p == "other":
                count = d.not_in_pidfile
            elif p == "unres":
                count = d.unresolved 
            else:
                count = d.pid_accesses[p].get_count()
                
            bars[p].append(count)

            if i > 0 and bw != 0:
                axs.fill([i-1 + w/2, i-1 + w/2, i - w/2, i - w/2], \
                [start_p[p], start_p[p] + bars[p][-2], start + bars[p][-1], start], \
                color=colors[idx], alpha = 0.5, lw=0)

            start_p[p] = start
            start += count
            idx += 1
            
    stack = [0]*len(spect.dumps)

    idx = 0
    for p in pids:
        axs.bar(ids, bars[p], bottom=stack, width=w, color=colors[idx], label=captions[idx])
        idx += 1
        stack = np.add(stack, bars[p]).tolist()

    axs.get_yaxis().set_major_formatter(\
            ticker.FuncFormatter(lambda x, pos: "%0.0f%%" % ((100*x)/(2*1024*1024/64))))

    axs.get_yaxis().set_major_locator(ticker.MultipleLocator(3276.8 * plot_count))
    axs.get_xaxis().set_major_locator(ticker.MultipleLocator(max(1,len(spect.dumps)/20)))

    axs.set_xlim((0-float(w)/2, len(spect.dumps) -1 + float(w)/2))
    
    bbox_props = dict(boxstyle="round,pad=0.5", fc="w", ec="k", lw=2, alpha=0.8)

    if(annot != ""):
        axs.annotate(annot,
                    xy=(0, 0.5),
                    xycoords='axes fraction',
                    xytext=(0.85, 0.15),
                    bbox=bbox_props,
                    arrowprops=None)

def plot_sc_synth():
    ## 5 ms interval (default)
    # path = "../single_synth/"
    # spect = MemSpectrum(14245, path + "/pids.txt", 1, 36)
    # labels = ["parent", "synth"]
    # #plot_multipid_seq([spect], path, labels, 0.5)
    # plot_spectrum(spect, 14245, path)
    
    ## 10 ms interval
    path = "../synth_new_10/"
    spect10 = MemSpectrum(15295, path + "/pids.txt", 1, 18)
    
    ## 20 ms interval
    path = "../synth_new_20/"
    spect20 = MemSpectrum(15304, path + "/pids.txt", 1, 18)

    # Combined plot
    plot_multipid_seq([spect, spect10, spect20], path, labels, 0.5)

def plot_sc_synth_step():
    ## 1 ms interval
    # path = "../synth_step_1/"
    # spect1 = MemSpectrum(16738, path + "/pids.txt", 1, 141)
    # plot_spectrum(spect1, 16738, path)

    ## 5 ms interval (default)    
    path = "../single_synth_step/"
    spect = MemSpectrum(13974, path + "/pids.txt", 1, 28)
    labels = ["parent", "synth"]
    plot_spectrum(spect, 13974, path)

    ## 10 ms interval
    path = "../synth_step_10/"
    spect10 = MemSpectrum(16600, path + "/pids.txt", 1, 18)
    
    ## 20 ms interval
    path = "../synth_step_20/"
    spect20 = MemSpectrum(16611, path + "/pids.txt", 1, 18)

    ## 30 ms interval
    path = "../synth_step_30/"
    spect30 = MemSpectrum(17222, path + "/pids.txt", 1, 18)

    ## 40 ms interval
    path = "../synth_step_40/"
    spect40 = MemSpectrum(17194, path + "/pids.txt", 1, 18)

    ## 50 ms interval
    path = "../synth_step_50/"
    spect50 = MemSpectrum(17104, path + "/pids.txt", 0, 18)

    # Combined plot
    plot_multipid_seq([spect20, spect30, spect40], path, labels, 0.5)

def plot_sc_synth_15step():
    labels = ["parent", "synth"]

    ## 2 ms interval
    path = "../synth_15step_2/"
    spect2 = MemSpectrum(19840, path + "/pids.txt", 1, 100)

    path = "../synth_15step_5/"
    spect5 = MemSpectrum(18955, path + "/pids.txt", 1, 100)

    path = "../synth_15step_10/"
    spect10 = MemSpectrum(18971, path + "/pids.txt", 1, 100)
    plot_multipid_seq([spect2, spect5, spect10], path, labels, 0.5, [2, 5, 10])

    return
    ## 5 ms interval
    # path = "../synth_15step_5/"
    # spect5 = MemSpectrum(17818, path + "/pids.txt", 1, 100)

    # plot_spectrum(spect5, 17818, path)
    
    ## 10 ms interval
    path = "../synth_15step_10/"
    spect10 = MemSpectrum(17827, path + "/pids.txt", 1, 10)
    
    ## 20 ms interval
    path = "../synth_15step_20/"
    spect20 = MemSpectrum(17830, path + "/pids.txt", 1, 10)

    ## 30 ms interval
    path = "../synth_15step_30/"
    spect30 = MemSpectrum(17833, path + "/pids.txt", 1, 10)

    ## 40 ms interval
    path = "../synth_15step_40/"
    spect40 = MemSpectrum(17836, path + "/pids.txt", 1, 10)

    # Combined plot
    plot_multipid_seq([spect10, spect20, spect30], path, labels, 0.5)

    
def plot_sc_sd_vbs():
    path = "../sc_4sdvbs_vga/"
    spect = MemSpectrum(0, path + "/pids.txt", 1, 700)
    labels = ["parent", "track. (vga)", "mser (vga)", "sift (cif)", "disp. (cif)"]
    plot_multipid_seq([spect], path, labels, 1, False)

def plot_sc_sd_vbs_nrt():
    path = "../sc_4sdvbs_vga_nrt/"
    spect = MemSpectrum(0, path + "/pids.txt", 1, 700)
    labels = ["parent", "track. (vga)", "mser (vga)", "sift (cif)", "disp. (cif)"]
    plot_multipid_seq([spect], path, labels, 1, False)

def plot_mc_sd_vbs():
    path = "../mc_4sdvbs_vga/"
    spect = MemSpectrum(0, path + "/pids.txt", 1, 700)
    labels = ["parent", "track. (vga)", "mser (vga)", "sift (cif)", "disp. (cif)"]
    plot_multipid_seq([spect], path, labels, 1, False)

def plot_mc_4c_sd_vbs():
    path = "../mc_4c_4sdvbs_vga/"
    spect = MemSpectrum(0, path + "/pids.txt", 1, 700)
    labels = ["parent", "track. (vga)", "mser (vga)", "sift (cif)", "disp. (cif)"]
    plot_multipid_seq([spect], path, labels, 1, False)
        
def plot_solo(bm, labels):
    path = "../sdvbs_solo/" + bm + "/"
    spect = MemSpectrum(0, path + "/pids.txt", 1, 300)
    plot_multipid_seq([spect], path, labels, 1, False)
    
def plot_sc_sd_vbs_rtprio():
    path = "../single_conc_sd_vbs_rtprio/"
    spect = MemSpectrum(0, path + "/pids.txt", 1, 225)
    labels = ["parent", "disp. (vga)", "mser (vga)", "sift (cif)", "track. (cif)"]
    plot_multipid(spect, spect.other_pids, path, labels, 1)

def plot_mc_4_sd_vbs_rtprio():
    path = "../mult_4_conc_sd_vbs_rtprio/"
    spect = MemSpectrum(0, path + "/pids.txt", 1, 225)
    labels = ["parent", "disp. (vga)", "mser (vga)", "sift (cif)", "track. (cif)"]
    plot_multipid(spect, spect.other_pids, path, labels, 1)

def plot_mc_2_sd_vbs_rtprio():
    path = "../mult_2_conc_sd_vbs_rtprio/"
    spect = MemSpectrum(0, path + "/pids.txt", 1, 201)
    labels = ["parent", "disp. (vga)", "mser (vga)"]
    plot_multipid(spect, spect.other_pids, path, labels, 1)
    
def plot_sc_disp_cif():
    path = "../disparity_cif/"
    pid = 20843
    spect = MemSpectrum(pid, path + "/pids.txt", 1, 69)
    plot_spectrum(spect, pid, path, 0.3)

def plot_simple():
    path = "../syn_dual/"
    pid = 15380
    spect = MemSpectrum(pid, path + "/pids.txt", 1, 30)
    plot_spectrum(spect, pid, path, 0.3)

def plot_simple_tr():
    path = "../syn_dual_tr/"
    pid = 15158
    spect = MemSpectrum(pid, path + "/pids.txt", 1, 30)
    plot_spectrum(spect, pid, path, 0.3)

def plot_sc_disp_vga():
    path = "../disparity_vga/"
    pid = 20812
    spect = MemSpectrum(pid, path + "/pids.txt", 1, 200)
    plot_spectrum(spect, pid, path, 0.3)

def plot_sc_mser_vga():
    path = "../mser_vga/"
    pid = 20868
    spect = MemSpectrum(pid, path + "/pids.txt", 1, 200)
    plot_spectrum(spect, pid, path, 0.3)

def plot_sc_sift_vga():
    path = "../sift_vga/"
    pid = 20891
    spect = MemSpectrum(pid, path + "/pids.txt", 1, 200)
    plot_spectrum(spect, pid, path, 0.3)
    
def cp_plots_to_paper():
    dirs = ["../single_synth/", "../synth_new_20/", "../single_synth_step/", "../single_conc_sd_vbs/", "../single_conc_sd_vbs_rtprio/", "../mult_4_conc_sd_vbs_rtprio/", "../mult_2_conc_sd_vbs_rtprio/", "../disparity_cif/", "../disparity_vga/"]

    dest = "../../paper_ECRTS20/figs/"
    
    for d in dirs:
        prefix = d.split("/")[-2] + "_"
        
        name = "plot.pdf"
        if os.path.isfile(d + name):
            shutil.copyfile(d + name, dest + prefix + name)

        name = "plot_pids.pdf"
        if os.path.isfile(d + name):
            shutil.copyfile(d + name, dest + prefix + name)

        name = "plot_wss.pdf"
        if os.path.isfile(d + name):
            shutil.copyfile(d + name, dest + prefix + name)
            
if __name__ == '__main__':

    if len(sys.argv) > 2:

        if (False):

            # CMD: python plot.py 8734 ../single_executable_synth/
            
            pid = int(sys.argv[1])
            path = sys.argv[2]
        
            spect = MemSpectrum(pid, path + "/pids.txt", 1, 14)
            #spect.get_stats_per_region()        
            plot_spectrum(spect, pid, path)

        elif (False):

            # CMD: python plot.py 7105 ../sd_vbs_concurr/
            
            pid = int(sys.argv[1])
            path = sys.argv[2]
        
            spect = MemSpectrum(pid, path + "/pids.txt", 5, 300)

            #spect.get_stats_per_region()        
            plot_spectrum(spect, pid, path)

        elif (False):

            # CMD: python plot.py 7105 ../sd_vbs_concurr/
            
            pids = []
            for arg in sys.argv[1:-1]:
                pids.append(int(arg))

            path = sys.argv[-1]
            spect = MemSpectrum(pids[0], path + "/pids.txt", 5, 100)

            plot_multipid(spect, spect.other_pids, path)


        elif (False):

            # Everything passed via params
            pids = []
            for arg in sys.argv[1:-1]:
                pids.append(int(arg))

            path = sys.argv[-1]
            first = int(sys.argv[-3])
            last = int(sys.argv[-2])
            
            spect = MemSpectrum(pids[0], path + "/pids.txt", first, last)

            plot_multipid(spect, spect.other_pids, path, ["parent", "obs"])

        else:
            
            # Single core, synthetic experiments:
            #plot_sc_synth()
            #plot_sc_synth_step()
            #plot_sc_synth_15step()

            #plot_sc_disp_vga()            
            #plot_sc_disp_cif()
            #plot_sc_mser_vga()
            #plot_sc_sift_vga()
            
            #plot_sc_sd_vbs()
            #plot_sc_sd_vbs_nrt()
            #plot_mc_sd_vbs()
            #plot_mc_4c_sd_vbs()
            plot_simple()
            plot_simple_tr()

            #plot_sc_sd_vbs_rtprio()
            #plot_mc_4_sd_vbs_rtprio()
            #plot_mc_2_sd_vbs_rtprio()

            #plot_solo("disparity", ["par", "disparity (vga)"])
            #plot_solo("sift", ["par", "sift (vga)"])
            #plot_solo("mser", ["par", "mser (vga)"])
            #plot_solo("tracking", ["par", "tracking (vga)"])
            
            #cp_plots_to_paper()
        
    else:
        print(sys.argv[0] + " /proc/<pid>/maps")

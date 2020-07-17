#!/usr/bin/python

#######################################################
#                                                     #
# Library to parse the content of /proc/PID/maps file #
# and to match the content of cache snapshots         #
#                                                     #
# Author: Renato Mancuso (BU)                         #
# Date: April 2020                                    #
#                                                     #
#######################################################

import os
import os.path
import sys
import commands
import operator

PAGE_SIZE = 0x1000

class Accesses:
    def __init__(self, pid):
        self.pid = pid
        self.pages = {}
        self.total_blocks = 0
        self.ranges = None
        self.regions = None
        self.page_to_regions = {}
        self.regions_to_pages = {}
        self.not_in_regions = 0
        
    def add_access(self, page):
        page = int(page, 0)
        if (page not in self.pages):
            self.pages[page] = 1
        else:
            self.pages[page] += 1

        self.total_blocks += 1

    # Computes the number of blocks reloaded between snapshots
    def get_reused_blocks(self, other):

        reused = 0
        
        for p in self.pages:
            if p in other.pages:
                reused += min(self.pages[p], other.pages[p])

        return reused
        
    def get_ranked_list(self, count):
        sorted_acc = sorted(self.pages.items(), key=operator.itemgetter(1), reverse=True)
        return sorted_acc[:count]

    def get_count(self):
        return self.total_blocks

    def get_weights(self, pages, scale = 1):
        weights = []
        for p in pages:
            weights.append(self.pages[p] * scale)
        return weights
    
    def match_to_regions(self, regions):
        self.regions = regions

        for i, r in enumerate(regions):
            self.regions_to_pages[i] = []

        for page in self.pages:
            for i, r in enumerate(regions):
                
                if page >= r.start and page < r.end + 4096:
                    self.page_to_regions[page] = i
                    self.regions_to_pages[i].append(page)
                    break
                
            # Sanity check here
            if page not in self.page_to_regions:
                self.not_in_regions += 1
        
    def get_page_ranges(self):
        if self.ranges != None:
            return self.ranges

        self.ranges = []
        sorted_addr = sorted(self.pages.items(), key=operator.itemgetter(0), reverse=False)
        start = None
        end = None
        for p in sorted_addr:
            if start == None:
                start = p[0]
                end = p[0]
                continue
            
            if p[0] == (end + PAGE_SIZE):
                end = p[0]
            else:
                self.ranges.append((start, end))
                start = p[0]
                end = p[0]

        if start != None:
            self.ranges.append((start, end))
            
        return self.ranges
                
class CacheDump:
    def __init__(self, other_pids, dump_file):
        self.dump_file = dump_file
        self.pid_accesses = {}
        self.pid_regions = {}
        self.bad_entries = 0
        self.unresolved = 0
        self.okay_entries = 0
        self.tot_entries = 0
        self.not_in_pidfile = 0
        self.pids_from_file = other_pids

        # Make sure we have an entry for all the PIDs on file
        for pid in self.pids_from_file:
            if pid not in self.pid_accesses:
                self.pid_accesses[pid] = Accesses(pid)
        
        # Parse cache dump file
        file = open(self.dump_file)
        lines = [l.strip('\n') for l in file.readlines()]
        for l in lines:
            fields = l.split(",")
            if len(fields) == 2:
                (pid, page) = fields
                pid = int(pid)

                # Make sure we have an entry for each new PID we
                # encounter
                if pid not in self.pid_accesses:
                    self.pid_accesses[pid] = Accesses(pid)

                if pid in self.pids_from_file:
                    self.pid_accesses[pid].add_access(page)
                    self.okay_entries += 1
                elif pid >= 0:
                    self.pid_accesses[pid].add_access(page)
                    self.okay_entries += 1
                    self.not_in_pidfile += 1
                else:
                    self.unresolved += 1
            else:
                self.bad_entries += 1     
            self.tot_entries += 1
                
        file.close()

        # Automatically try to find proc/pid/maps file for each PID
        base_path = dump_file.split("/")[0:-1]
        base_path = ("/").join(base_path) + "/"
        dump_id = dump_file.split("/")[-1].split(".")
        dump_id = (dump_id[0])[9:]
        
        for pid in self.pid_accesses:
            proc_file = base_path + str(pid) + "-" + str(dump_id) + ".txt"
            if os.path.isfile(proc_file):
                self.pid_regions[pid] = self.__parse_maps(proc_file)
                self.pid_accesses[pid].match_to_regions(self.pid_regions[pid])
                
    # Internal function to parse proc/pid/maps files
    def __parse_maps(self, proc_file):
        maps = []

        proc_file = open(proc_file)            
        lines = [l.strip('\n') for l in proc_file.readlines()]
                
        for i in range(len(lines)):
            fields = lines[i].split (" ", 5)
            
            start_end = None
            perm = None
            off = None
            dev = None
            inode = None
            path = None        
            
            if len (fields) == 6:
                (start_end, perm, off, dev, inode, path) = fields
            else:
                (start_end, perm, off, dev, inode) = fields
                path = ""
                
            (start, end) = start_end.split("-")

            region = Region(start, end, perm, off, dev, inode, path, i)
            maps.append(region)

        return maps
                
    def print_stats(self):            
        print "\nTotal entries: \t%i\nBad: \t\t%i (%0.2f %%)\nNo PID: \t%i (%0.2f %%)\nGood: \t\t%i (%0.2f %%)\n" \
            % (self.tot_entries, \
            self.bad_entries, 100*float(self.bad_entries)/self.tot_entries, \
            self.unresolved, 100*float(self.unresolved)/self.tot_entries, \
            self.okay_entries, 100*float(self.okay_entries)/self.tot_entries, \
            )
        
        for p in self.pid_accesses:            
            print "\nMost Accessed pages for PID %i:" % (p)
            ranked = self.pid_accesses[p].get_ranked_list(10)
            for page in ranked:
                print "Page: %s Lines: %i" % (hex(page[0]), page[1])

            ranges = self.pid_accesses[p].get_page_ranges()
            for r in ranges:
                print "[%s - %s] (%i pages)" % (hex(r[0]), hex(r[1]), (r[1] - r[0])/PAGE_SIZE + 1)
            
class Region:
    def __init__(self, start, end, perm, off, dev, inode, path, index):
        self.start = int(start, 16)
        self.end = int(end, 16)
        self.perm = perm
        self.off = off
        self.dev = dev
        self.inode = inode
        self.path = path.strip()
        self.index = index

        # Determine the type
        self.is_heap = False
        self.is_stack = False
        self.is_vvar = False
        self.is_vdso = False
        self.is_vsyscall = False
        self.is_mapped = False
        self.is_anon = False
        self.short_name = ""
        
        if (self.path == "[heap]"):
            self.is_heap = True
            self.anon = True
            
        elif (self.path == "[stack]"):
            self.is_stack = True
            self.anon = True

        elif (self.path == "[vvar]"):
            self.is_vvar = True
            self.anon = True
        
        elif (self.path == "[vdso]"):
            self.is_vdso = True
            self.anon = True
    
        elif (self.path == "[vsyscall]"):
            self.is_vsyscall = True
            self.anon = True

        elif (self.path != ""):
            self.is_mapped = True
            last = self.path.split("/")[-1]
            if self.index == 0 and self.perm == "r-xp":
                self.short_name = "[text]"
            elif self.index in [1, 2]:
                if self.perm == "r--p":
                    self.short_name = "[rodata]" 
                elif self.perm == "rw-p":
                    self.short_name = "[data]"
            else:
                self.short_name = last.split("-")[0]
                    
        else:
            self.is_anon = True
            self.path = "[anon]"

        if self.short_name == "":
            self.short_name = self.path
            
    def __repr__(self):
        return "[%s] %s->%s (%s)" % (self.index, self.start, self.end, self.path)


class MemSpectrum:
    def __init__(self, pid, pid_file, start_idx, stop_idx = 0):
        self.pid = pid
        self.pid_file = pid_file
        self.start_idx = start_idx
        self.stop_idx = stop_idx

        # Find out path from pid file
        self.base_path = pid_file.split("/")[0:-1]
        self.base_path = ("/").join(self.base_path) + "/"

        # Parse other interesting pids for this experiment
        self.other_pids = []

        # Parse PID file
        file = open(self.pid_file)
        lines = [l.strip('\n') for l in file.readlines()]

        # Auto-adjust number of iterations if not passed explicitly by the user
        self.total_iter = int(lines[0])
        if self.stop_idx == 0:
            self.stop_idx = self.total_iter

        for l in lines[1:]:
            pid = int(l)
            self.other_pids.append(pid)
            
        file.close()
        
        # Parse all the cachedump files
        self.dumps = []
        for i in range(start_idx, stop_idx+1):
            dumpfile = self.base_path + "cachedump" + str(i) + ".csv"

            print "Parsing file: %s" % (dumpfile)
            
            if os.path.isfile(dumpfile):
                dump = CacheDump(self.other_pids, dumpfile)
                self.dumps.append(dump)
            else:
                print "Error: unable to find file: %s" % (dumpfile)
                break

    def get_total_blocks_in_region(self, pid, region_idx):
        total = 0
        for i, d in enumerate(self.dumps):
            acc = d.pid_accesses[self.pid]

            if region_idx in acc.regions_to_pages:
                total += len(acc.regions_to_pages[region_idx])

        return total

    def get_range_blocks_in_region(self, pid, region_idx):
        gl_max = None
        gl_min = None
        
        for i, d in enumerate(self.dumps):
            acc = d.pid_accesses[pid]

            if region_idx not in acc.regions_to_pages:
                continue
            
            acc_reg = acc.regions_to_pages[region_idx]
            if (len(acc_reg) == 0):
                continue
        
            cur_min = min(acc_reg)
            cur_max = max(acc_reg)
            
            if gl_max == None or gl_max < cur_max:
                gl_max = cur_max

            if gl_min == None or gl_min > cur_min:
                gl_min = cur_min

        if gl_max == None:
            return 0
        
        return (gl_max - gl_min)
    
    def get_stats_per_region(self, pid = None):
        if pid == None:
            pid = self.pid
        
        for i, d in enumerate(self.dumps):
            print "=== DUMP %i ===" % (i)
            acc = d.pid_accesses[pid]
            reg = d.pid_regions[pid]
            
            for j, r in enumerate(reg):
                print "Area [%i] (%s) - Pages: %i" % (j, reg[j].short_name, \
                                                      len(acc.regions_to_pages[j]))
                
if __name__ == '__main__':

    if len(sys.argv) > 1:
        spect = MemSpectrum(8813, sys.argv[1], 20, 25)

        spect.get_stats_per_region()
        
    else:
        print(sys.argv[0] + " /proc/<pid>/maps")



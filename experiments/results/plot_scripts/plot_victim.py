import matplotlib.pyplot as plt
import numpy as np
from scipy.stats import norm
import matplotlib.gridspec as gridspec
import scipy.stats

sequences=[]

def mean_confidence_interval(data, confidence=0.95):
    a = 1.0 * np.array(data)
    n = len(a)
    m, se = np.mean(a), scipy.stats.sem(a)
    h = se * scipy.stats.t.ppf((1 + confidence) / 2., n-1)
    return m, m-h, m+h

cached = []
fig = plt.figure(figsize=(6, 3))

plt.xticks(range(0,17))
#plt.title('Probability of X Lines Cached after Y Iterations')

plots = 8

cmap=plt.get_cmap("viridis", plots)
colors=cmap.colors

ways=[0] * 16
total=0

idx = 0
rnd = []
rounds = []

f = open("period.txt", "r")
for l in f.readlines():
    if l == "\n":
        if (idx == 16):
            if len(rnd) == 16:
                rounds.append(list(rnd))
                print rnd
            rnd = []      
            idx = 0
        else:
            idx += 1    
        continue

    total += 1
    cur = int(l.split(":")[0])
    ways[cur-1] += 1

    rnd.append(cur)
    idx += 1
        
for w in range(len(ways)):
    ways[w] = ways[w]/float(total)

    
plt.bar(range(1,17), ways, label="Observed Replacement")
plt.plot(range(0,18), [float(1.0/16)]*18, ls=":", color="red", label="Ideal Replacement")
plt.legend(ncol=2, loc="upper center", borderaxespad=0.,
           bbox_to_anchor=(0., 1.02, 1., .102),)
plt.xlim(0.3,16.7)
plt.xlabel("Way Selected for Replacement")
plt.ylabel("Probability")
plt.show()

fig.savefig("victim_prob.png", dpi=fig.dpi, bbox_inches='tight');
fig.savefig("victim_prob.pdf", dpi=fig.dpi, bbox_inches='tight');

# cached.append([])

# leg.append(ax.bar(x, cached[r-1], color=colors[r-1]))
# ax.set_yticks([0.1, 0.3, 0.5])
# ax.set_ylim(0, 0.5)

# labels.append(str(r) + " iter.")

# if r == plots/2:
#     ax.set_ylabel('Probablity')
    
# ax.set_xticks(range(1,17))
# #ax.set_xlim(1,16)
# ax.set_xlabel('Number of Cached Lines')

# fig.legend(leg,     # The line objects
#            ncol=4,
#            labels=labels,   # The labels for each line
#            loc="upper center",   # Position of legend
#            borderaxespad=0.1,    # Small spacing around legend box
# )

# plt.tight_layout()
# plt.show()

# fig.savefig("repl_prob.png", dpi=fig.dpi, bbox_inches='tight');
# fig.savefig("repl_prob.pdf", dpi=fig.dpi, bbox_inches='tight');

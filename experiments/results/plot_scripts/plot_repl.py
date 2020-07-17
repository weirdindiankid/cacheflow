import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
import numpy as np
from scipy.stats import norm
import matplotlib.gridspec as gridspec
import statistics


cached = []
fig = plt.figure(figsize=(6, 5))

plt.xticks(range(0,17))
plt.title('Probability of X Lines Cached after Y Iterations')

plots = 8

cmap=plt.get_cmap("viridis", plots)
colors=cmap.colors

gs1 = gridspec.GridSpec(plots, 1)
gs1.update(wspace=0.025, hspace=0.2) # set the spacing between axes. 

ax = plt.subplot(gs1[0])

leg = []
labels=[]

fitlab = "Fit"
handles = []
labels = []

fithandle = None

for r in range(1, plots+1):

    ax = plt.subplot(gs1[r-1], sharey=ax)
    
    f = open("pol"+str(r)+".txt", "r")
    cached.append([])
    raw = []
    
    for i in range(1, 17):
        cached[r-1].append(0)
    
    for l in f.readlines():
        cur = int(l)
        if cur > 15:
            continue
        cached[r-1][cur] += 1
        raw.append(cur+1)
        
    print cached[r-1]

    #ax.axvline(statistics.median(raw))
    norm_cached = [0] * len(cached[r-1])
    
    for l in range(len(cached[r-1])):
        norm_cached[l] = float(cached[r-1][l]) / (sum(cached[r-1]))
    
    x = range(1,17)
    ax.bar(x, norm_cached, color=colors[r-1], label=(str(r) + " iter."))
    ax.set_yticks([0.1, 0.3, 0.5])
    ax.set_ylim(0, 0.5)
    
    if r == plots/2:
        ax.set_ylabel('Probability')

    ax.set_xticks(range(1,17))

    mean,std=norm.fit(raw)

    x = np.linspace(1, 16, 1000)
    p = norm.pdf(x, mean, std)
    
    # if r == 16:
    #     leg.append(ax.plot(x, p, linewidth=1, color='r', ls="--", label="Test"))
    # else:

    handles.append(ax.get_legend_handles_labels()[0][0])
    labels.append(ax.get_legend_handles_labels()[1][0])    

    print len(ax.get_legend_handles_labels()[0])
    
    if r == 8:        
        ax.plot(x, p, linewidth=1, color='r', ls="--", label="Fit")
        
        handles.append(ax.get_legend_handles_labels()[0][0])
        labels.append(ax.get_legend_handles_labels()[1][0])    
        
    else:
        ax.plot(x, p, linewidth=1, color='r', ls="--", label="_nolegend_")

        
ax.set_xticks(range(1,17))
#ax.set_xlim(1,16)
ax.set_xlabel('Number of Cached Lines ($k$)')


# # manually define a new patch
# fitline = Line2D([0], [0], color="r", linewidth=1, linestyle='--')

# # handles is a list, so append manual patch
# handles.append(fitline) 
# labels.append("Fit")

# print labels


fig.legend(handles=handles,ncol=5,labels=labels,
           loc="upper center",   # Position of legend
           borderaxespad=0.1,    # Small spacing around legend box
)


plt.tight_layout()
plt.show()

fig.savefig("repl_prob.png", dpi=fig.dpi, bbox_inches='tight');
fig.savefig("repl_prob.pdf", dpi=fig.dpi, bbox_inches='tight');

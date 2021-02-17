#!/usr/bin/env python3

import argparse
import numpy as np
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import axes3d

parser = argparse.ArgumentParser(description='plot paths')
parser.add_argument('paths')
parser.add_argument('-g', '--graph')
args = parser.parse_args()

fig = plt.figure()
ax = fig.gca(projection='3d')
colors = [
    '#1f77b4', '#ff7f0e', '#2ca02c', '#d62728', '#9467bd', '#8c564b',
    '#e377c2', '#7f7f7f', '#bcbd22', '#17becf'
]

if args.graph:
    graph = np.genfromtxt(args.graph, delimiter=',', skip_header=1)
    ax.quiver(graph[:, 0],
              graph[:, 1],
              np.zeros(len(graph)),
              graph[:, 2] - graph[:, 0],
              graph[:, 3] - graph[:, 1],
              np.zeros(len(graph)),
              color='black')

data = np.genfromtxt(args.paths,
                     delimiter=',',
                     skip_header=1,
                     usecols=(1, 2, 3, 5))

for i in np.arange(data[:, 0].max() + 1):
    path = data[data[:, 0] == i]
    ax.quiver(path[:-1, 1],
              path[:-1, 2],
              path[:-1, 3],
              np.diff(path[:, 1]),
              np.diff(path[:, 2]),
              np.diff(path[:, 3]),
              color=colors[int(i) % len(colors)])
    ax.quiver(path[:, 1],
              path[:, 2],
              path[:, 3],
              np.zeros(len(path)),
              np.zeros(len(path)),
              1 - path[:, 3].clip(1, 2),
              path[:, 3],
              color='gray')

ax.zaxis.set_ticks([])
ax.set_zlim3d(0, 5)

plt.show()

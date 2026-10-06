# fit_shadow.py <capture.png> <window width> <height> <margin>: fit one and two blurred copies of the
# window rectangle (alpha, sigma, offset down) to the shadow around it, on a white backdrop
import sys, numpy as np
from PIL import Image
from scipy.optimize import least_squares
from scipy.special import erf

def load_alpha(path):
    im = np.asarray(Image.open(path).convert('L'), dtype=float)
    return 1 - im / 255.0

def Phi(x): return 0.5 * (1 + erf(x / np.sqrt(2)))

def model(p, W, H, m, shape):
    # two layers: alpha_i * blurred rect (sigma_i), offset down by dy_i; rect = window (x: m..m+W, y: m..m+H)
    h, w = shape
    y, x = np.mgrid[0:h, 0:w] + 0.5
    total = np.zeros(shape)
    for i in range(len(p) // 3):
        a, s, dy = p[3*i:3*i+3]
        s = abs(s) + 1e-3
        fx = Phi((x - m) / s) - Phi((x - m - W) / s)
        fy = Phi((y - m - dy) / s) - Phi((y - m - H - dy) / s)
        total = 1 - (1 - total) * (1 - a * fx * fy)
    return total

name, W, H, m = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
A = load_alpha(name)
h, w = A.shape
# fit only outside the window rect (where the shadow is visible), away from the corner cutouts
mask = np.ones_like(A, bool)
mask[m:m+H, m:m+W] = False
def resid(p): return (model(p, W, H, m, A.shape) - A)[mask]
for layers, p0 in ((1, [0.25, 15, 8]), (2, [0.12, 4, 1, 0.14, 20, 14])):
    r = least_squares(resid, p0)
    err = resid(r.x)
    print(layers, 'layers:', ' '.join('%.4f' % v for v in r.x), 'rms %.5f max %.5f (x255: rms %.2f max %.2f)' % (np.sqrt((err**2).mean()), abs(err).max(), 255*np.sqrt((err**2).mean()), 255*abs(err).max()))

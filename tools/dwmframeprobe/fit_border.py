# fit_border.py <dir>: the opacity and colour of the default border, from the edge pixels of the
# round/roundsmall captures on white, grey and black backdrops and the fitted shadow under them
import numpy as np
from PIL import Image
from scipy.special import erf
def Phi(x): return 0.5 * (1 + erf(x / np.sqrt(2)))
def shadow_alpha(x, y, layers, m=64, W=160, H=110):
    t = 1.0
    for a, s, dy in layers:
        fx = Phi((x + 0.5 - m) / s) - Phi((x + 0.5 - m - W) / s)
        fy = Phi((y + 0.5 - m - dy) / s) - Phi((y + 0.5 - m - H - dy) / s)
        t *= 1 - a * fx * fy
    return 1 - t
import sys
S = sys.argv[1]   # a directory of the probe's PNGs, decoded from its output
for name, layers, r in (('round', [(0.1445, 7.08, 1.98), (0.1487, 19.55, 34.5)], 8), ('roundsmall', [(0.0977, 4.05, 9.17)], 4)):
    rows = []
    for bg, suffix in ((255, '@white'), (128, ''), (0, '@black')):
        im = np.asarray(Image.open(f'{S}/{name}{suffix}.png').convert('L'), dtype=float)
        m = 64; H, W = im.shape
        pts = [(x, m) for x in range(m + r + 2, W - m - r - 2)] + [(x, H - m - 1) for x in range(m + r + 2, W - m - r - 2)] + \
              [(m, y) for y in range(m + r + 2, H - m - r - 2)] + [(W - m - 1, y) for y in range(m + r + 2, H - m - r - 2)]
        for x, y in pts:
            rows.append((bg * (1 - shadow_alpha(x, y, layers)), im[y, x]))
    A = np.array(rows)
    # obs = (1 - a) * B' + P
    X = np.stack([A[:, 0], np.ones(len(A))], axis=1)
    (k, P), res, *_ = np.linalg.lstsq(X, A[:, 1], rcond=None)
    pred = X @ np.array([k, P])
    print(name, 'a = %.4f, premultiplied colour P = %.2f (colour %.1f)' % (1 - k, P, P / (1 - k)), 'rms %.2f max %.2f' % (np.sqrt(((pred - A[:, 1])**2).mean()), abs(pred - A[:, 1]).max()))

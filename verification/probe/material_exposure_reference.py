"""Independent finite-domain composition of separately evaluated B and H.

Component evaluators must isolate source roles directly, never recover B by
subtracting whole images or by scaling a directional-light fraction.
"""
from dataclasses import dataclass
import math

CAP=65504.

@dataclass(frozen=True)
class ExposureResult:
    base: tuple
    highlights: tuple
    q: tuple
    displayed: tuple
    encoded: tuple
    exact_domain: bool


def evaluate(base,highlights,exposure):
    if not math.isfinite(exposure) or not .125<=exposure<=2.:
        raise ValueError('exposure outside qualified [1/8,2] range')
    base,highlights=tuple(base),tuple(highlights)
    if len(base)!=3 or len(highlights)!=3:raise ValueError('RGB required')
    exact=all(math.isfinite(b) and math.isfinite(h) and b>=0 and h>=0 and b+h<=CAP for b,h in zip(base,highlights))
    ceiling=CAP/min(exposure,1.)
    def finite(value):return 0. if math.isnan(value) or value<=0 else min(value,ceiling)
    q=tuple(finite(b/exposure+h) for b,h in zip(base,highlights))
    return ExposureResult(base,highlights,q,tuple(exposure*v for v in q),
                          tuple(max(v,1e-22)**(1/2.2) if v>0 else 0. for v in q),exact)

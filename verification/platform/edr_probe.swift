// Read-only display capability inspection. Run: swift verification/platform/edr_probe.swift
// Current headroom changes with display/application state; this does not enable HDR.
import AppKit

for screen in NSScreen.screens {
    print("display=\(screen.localizedName) frame=\(screen.frame) scale=\(screen.backingScaleFactor) EDR_current=\(screen.maximumExtendedDynamicRangeColorComponentValue) EDR_potential=\(screen.maximumPotentialExtendedDynamicRangeColorComponentValue) EDR_reference=\(screen.maximumReferenceExtendedDynamicRangeColorComponentValue)")
}

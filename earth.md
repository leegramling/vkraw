# Earth Globe Implementation (VSG Path)

This document describes the current `vkvsg` globe implementation.

## Scope

- Target: `vkvsg` only (VSG renderer path).
- Units: feet.
- Earth model: WGS84 ellipsoid scale.
- Rendering path: `vsg::Builder` sphere + texture.

## Coordinate System And Units

The globe uses WGS84 radii converted to feet:

- Equatorial radius `a = 6378137.0 m = 20925646.325 ft`
- Polar radius `b = 6356752.314245 m = 20855486.596 ft`

The scene origin is Earth center (`ECEF` center).

## Rendering Path

The globe node is created with `vsg::Builder::createSphere()`:

- Geometry scaled to an ellipsoid via axis vectors.
- Texture assigned through `vsg::StateInfo::image`.
- Wireframe toggled via `vsg::StateInfo::wireframe`.

This avoids the previous custom graphics pipeline path.

## Camera And Navigation

Navigation now uses only `vsg::Trackball` with `EllipsoidModel`:

- LMB drag: orbit.
- MMB drag: pan.
- RMB drag or wheel: zoom.

The previous manual globe rotation transform was removed to avoid fighting camera controls.

## Textures

Texture source options:

- `--earth-texture <path>` loads an image file.
- If no file is provided (or loading fails), a procedural fallback texture is used.

## Wireframe

- `W` toggles wireframe at runtime.
- The scene node is rebuilt with updated `StateInfo::wireframe`.

## Runtime Notes

- Runtime requires a display server (X11/Wayland) and valid Vulkan driver.
- Headless sessions can fail at window creation or presentation.

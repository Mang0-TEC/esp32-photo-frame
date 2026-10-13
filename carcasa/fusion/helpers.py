import adsk.core, adsk.fusion

VI = adsk.core.ValueInput.createByString
OPS = {
    'new': adsk.fusion.FeatureOperations.NewBodyFeatureOperation,
    'join': adsk.fusion.FeatureOperations.JoinFeatureOperation,
    'cut': adsk.fusion.FeatureOperations.CutFeatureOperation,
}
H = adsk.fusion.DimensionOrientations.HorizontalDimensionOrientation
V = adsk.fusion.DimensionOrientations.VerticalDimensionOrientation


def _design():
    return adsk.fusion.Design.cast(adsk.core.Application.get().activeProduct)


def ev(expr):
    return _design().unitsManager.evaluateExpression(expr, 'mm')  # cm


def sub(parent, name):
    for o in parent.occurrences:
        if o.component.name == name:
            return o.component
    occ = parent.occurrences.addNewComponent(adsk.core.Matrix3D.create())
    occ.component.name = name
    return occ.component


def _plane(comp, base, off, tag):
    pi = comp.constructionPlanes.createInput()
    pi.setByOffset(base, VI(off))
    pl = comp.constructionPlanes.add(pi)
    pl.name = 'pl_' + tag
    pl.isLightBulbOn = False
    return pl


def plane_z(comp, z0, tag):
    return _plane(comp, comp.xYConstructionPlane, z0, tag)


def _dim(sk, a, b, orient, expr):
    """Cota de distancia; si la expresión vale 0 usa una restricción de alineación."""
    if abs(ev(expr)) < 1e-9:
        gc = sk.geometricConstraints
        if orient == H:
            gc.addVerticalPoints(a, b)
        else:
            gc.addHorizontalPoints(a, b)
        return None
    pa, pb = a.geometry, b.geometry
    tp = adsk.core.Point3D.create((pa.x + pb.x) / 2 + 0.3, (pa.y + pb.y) / 2 + 0.3, 0)
    d = sk.sketchDimensions.addDistanceDimension(a, b, orient, tp)
    d.parameter.expression = expr
    return d


def _extrude(comp, name, prof, h, op, target, taper=None):
    ex = comp.features.extrudeFeatures
    inp = ex.createInput(prof, OPS[op])
    if taper:
        inp.setOneSideExtent(adsk.fusion.DistanceExtentDefinition.create(VI(h)),
                             adsk.fusion.ExtentDirections.PositiveExtentDirection, VI(taper))
    else:
        inp.setDistanceExtent(False, VI(h))
    if target is not None and op != 'new':
        inp.participantBodies = [target]
    f = ex.add(inp)
    f.name = 'ex_' + name
    if op == 'new':
        b = f.bodies.item(0)
        b.name = name
        return b
    return target


def box(comp, name, x0, y0, z0, dx, dy, dz, op='new', target=None):
    """Caja paramétrica: rectángulo en plano XY desplazado z0, extruida dz (+Z)."""
    sk = comp.sketches.add(plane_z(comp, z0, name))
    sk.name = 'sk_' + name
    x0v, y0v, dxv, dyv = ev(x0), ev(y0), ev(dx), ev(dy)
    sk.sketchCurves.sketchLines.addTwoPointRectangle(
        adsk.core.Point3D.create(x0v, y0v, 0), adsk.core.Point3D.create(x0v + dxv, y0v + dyv, 0))
    pts = {}
    for p in sk.sketchPoints:
        if p == sk.originPoint:
            continue
        g = p.geometry
        pts[(round(g.x - x0v, 4) != 0, round(g.y - y0v, 4) != 0)] = p
    p00, p10, p01 = pts[(False, False)], pts[(True, False)], pts[(False, True)]
    o = sk.originPoint
    _dim(sk, o, p00, H, x0)
    _dim(sk, o, p00, V, y0)
    _dim(sk, p00, p10, H, dx)
    _dim(sk, p00, p01, V, dy)
    return _extrude(comp, name, sk.profiles.item(0), dz, op, target)


def cyl(comp, name, cx, cy, z0, d, h, op='new', target=None, taper=None):
    """Cilindro paramétrico de eje Z (h negativo = hacia -Z)."""
    sk = comp.sketches.add(plane_z(comp, z0, name))
    sk.name = 'sk_' + name
    c = sk.sketchCurves.sketchCircles.addByCenterRadius(
        adsk.core.Point3D.create(ev(cx), ev(cy), 0), ev(d) / 2)
    o = sk.originPoint
    _dim(sk, o, c.centerSketchPoint, H, cx)
    _dim(sk, o, c.centerSketchPoint, V, cy)
    dd = sk.sketchDimensions.addDiameterDimension(c, adsk.core.Point3D.create(ev(cx) + ev(d), ev(cy), 0))
    dd.parameter.expression = d
    return _extrude(comp, name, sk.profiles.item(0), h, op, target, taper)


def cyl_y(comp, name, cx, cz, y0, d, h, op='new', target=None, taper=None):
    """Cilindro paramétrico de eje Y: h positivo = hacia +Y (atrás)."""
    sk = comp.sketches.add(_plane(comp, comp.xZConstructionPlane, y0, name))
    sk.name = 'sk_' + name
    sp = sk.modelToSketchSpace(adsk.core.Point3D.create(ev(cx), ev(y0), ev(cz)))
    c = sk.sketchCurves.sketchCircles.addByCenterRadius(adsk.core.Point3D.create(sp.x, sp.y, 0), ev(d) / 2)
    o = sk.originPoint
    _dim(sk, o, c.centerSketchPoint, H, cx)
    _dim(sk, o, c.centerSketchPoint, V, cz)
    dd = sk.sketchDimensions.addDiameterDimension(c, adsk.core.Point3D.create(sp.x + ev(d), sp.y, 0))
    dd.parameter.expression = d
    n = sk.xDirection.crossProduct(sk.yDirection)
    hexpr = h if n.y > 0 else '-(' + h + ')'
    return _extrude(comp, name, sk.profiles.item(0), hexpr, op, target, taper)


def box_x(comp, name, x0, y0, z0, dy, dz, dx, op='new', target=None):
    """Caja paramétrica de eje X: rectángulo en plano YZ desplazado x0, extruida dx (+X)."""
    sk = comp.sketches.add(_plane(comp, comp.yZConstructionPlane, x0, name))
    sk.name = 'sk_' + name
    x0v, y0v, z0v = ev(x0), ev(y0), ev(z0)
    a = sk.modelToSketchSpace(adsk.core.Point3D.create(x0v, y0v, z0v))
    b = sk.modelToSketchSpace(adsk.core.Point3D.create(x0v, y0v + ev(dy), z0v + ev(dz)))
    sk.sketchCurves.sketchLines.addTwoPointRectangle(
        adsk.core.Point3D.create(a.x, a.y, 0), adsk.core.Point3D.create(b.x, b.y, 0))
    pts = {}
    for p in sk.sketchPoints:
        if p == sk.originPoint:
            continue
        m = sk.sketchToModelSpace(p.geometry)
        pts[(round(m.y - y0v, 4) != 0, round(m.z - z0v, 4) != 0)] = p
    p00, py, pz = pts[(False, False)], pts[(True, False)], pts[(False, True)]
    oy = H if abs(py.geometry.x - p00.geometry.x) > 1e-6 else V
    oz = V if oy == H else H
    o = sk.originPoint
    _dim(sk, o, p00, oy, y0)
    _dim(sk, o, p00, oz, z0)
    _dim(sk, p00, py, oy, dy)
    _dim(sk, p00, pz, oz, dz)
    n = sk.xDirection.crossProduct(sk.yDirection)
    hexpr = dx if n.x > 0 else '-(' + dx + ')'
    return _extrude(comp, name, sk.profiles.item(0), hexpr, op, target)


def report(comp):
    for b in comp.bRepBodies:
        mn, mx = b.boundingBox.minPoint, b.boundingBox.maxPoint
        print('  %-22s x %7.2f..%7.2f  y %6.2f..%6.2f  z %6.2f..%6.2f  vol %9.1f' % (
            b.name, mn.x*10, mx.x*10, mn.y*10, mx.y*10, mn.z*10, mx.z*10, b.volume*1000))

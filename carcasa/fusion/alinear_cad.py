import re
import adsk.core, adsk.fusion

# Coloca los modelos CAD de _Modelos_CAD segun los parametros actuales.
# Cada matriz: columnas = a donde va el eje x, y, z del modelo; t = traslacion (cm).

def alinear(design):
    ev = lambda e: design.unitsManager.evaluateExpression(e, 'mm')
    mm = 0.1
    cad = [o for o in design.rootComponent.allOccurrences if o.component.name == '_Modelos_CAD'][0]
    # Fusion nombra los X-ref 'modelo v1' o 'modelo.f3d  v1' segun como se abrio el documento.
    occs = {re.sub(r'(\.f3d)?\s+v\d+$', '', o.component.name): o for o in cad.childOccurrences}

    def M(cx, cy, cz, t):
        m = adsk.core.Matrix3D.create()
        m.setWithCoordinateSystem(adsk.core.Point3D.create(*t), adsk.core.Vector3D.create(*cx),
                                  adsk.core.Vector3D.create(*cy), adsk.core.Vector3D.create(*cz))
        return m

    T = {}
    # Display: x->X, y->Z (pines arriba), z->-Y (vidrio al frente); cara frontal del PCB sobre los apoyos.
    yf = ev('pared + holgura_vidrio + disp_panel_t')
    T['LCD_TFT_3.5inch_ST7796S'] = M((1, 0, 0), (0, 0, 1), (0, -1, 0),
                                        (ev('disp_x0 + (disp_pcb_w - 59.15 mm) / 2'), yf + 1.2 * mm, ev('disp_z0')))
    # ESP32 (dentro del contenedor Pos_ESP32): x->-X, y->Z (USB abajo), z->+Y (componentes a la tapa).
    T['Pos_ESP32'] = M((-1, 0, 0), (0, 0, 1), (0, 1, 0),
                       (ev('esp_x0 + esp_w / 2'), ev('tapa_y_int - esp_gap - esp_comp_h') - 1.6 * mm, ev('esp_z0')))
    # SD: x->-Z (ranura arriba), y->-X, z->+Y; extremo de la ranura del PCB = borde superior del dummy.
    T['SD Card Module - 1'] = M((0, 0, -1), (-1, 0, 0), (0, 1, 0),
                                   (ev('sd_x0 + sd_pcb_w / 2'), ev('tapa_y_int - sd_standoff') - 1.57 * mm,
                                    ev('sd_z0 + sd_pcb_l') - 23.76 * mm))
    # TSL2561: x->-X (barrenos a la izquierda), y->-Y, z->+Z (chip arriba).
    zp = ev('pared + int_h + pared - sensor_pared - sensor_chip_aire - tsl_chip_h - tsl_t - sensor_separacion')
    T['tsl2561'] = M((-1, 0, 0), (0, -1, 0), (0, 0, 1), (ev('tsl_x0') + 18 * mm, ev('tsl_y0') + 13.05 * mm, zp))
    # LED: y->-Y (punta al frente), x->X, z->-Z; punta contra la cara interior del bisel.
    T['Led5mm_RGB'] = M((1, 0, 0), (0, -1, 0), (0, 0, -1), (ev('led_x'), ev('pared') + 8.5 * mm, ev('led_z')))

    for k, m in T.items():
        occs[k].transform2 = m
    if design.snapshots.hasPendingSnapshot:
        design.snapshots.add()
    return {k: occs[k].transform2.translation for k in T}

# Alcantarilla — placa metálica (láser)

Modelo limpio para corte láser / CNC, derivado de fotogrametría (7 radios).

| Archivo                   | Uso                                      |
| ------------------------- | ---------------------------------------- |
| `alcantarilla-metal.svg`  | Vista / import en Illustrator, etc.      |
| `alcantarilla-metal.dxf`  | Corte láser (unidades en **mm**)         |
| `alcantarilla-metal.glb`  | Vista 3D (placa 3 mm)                    |
| `alcantarilla_metal.py`   | Regenera SVG + DXF + GLB                 |

## Geometría

- Diámetro exterior: **400 mm**
- Espesor de placa (GLB): **3 mm**
- Patrón de 7 radios con ranuras anulares y orificios

## Regenerar

```bash
cd hardware/alcantarilla
python3 alcantarilla_metal.py
```

Dependencias: `numpy`, `Pillow`.

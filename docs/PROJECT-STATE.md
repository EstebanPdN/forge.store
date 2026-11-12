# Estado actual

Actualizado: 2026-11-11. La referencia mantenida es web V2; V1 permanece intacta.
La aplicación nativa ya está implementada, con validación separada de la web simulada.

- Repositorio solicitado: `EstebanPdN/forge.store`, público, sin releases.
- Vídeos: FVD2/Theora + IMA ADPCM; siete archivos, 16.044.949 bytes, un único paquete visible.
- Publicación: código, recursos revisados y binarios en `builds/3ds`; sin volcados ni cachés personales.
- Documentación vigente: [índice](README.md), [compilación](BUILDING.md), [verificación](VERIFICATION.md).
- Las auditorías y capturas anteriores se conservan localmente; no representan la aprobación del build actual.
- No se declara paridad visual total ni validación física completa.
- Galería corregida: reproducción legible, duración junto al título, navegación separada,
  transiciones y captura inferior; transparencias al 90 % e icono de ajustes de engranaje.
- Comprobación automática de la tienda antes de la intro, con plazo máximo y verificación
  del paquete. La instalación y el reinicio en consola física siguen pendientes de validar.
- Caché HTTP compartida y precarga de páginas/imágenes públicas de GitHub, con solicitudes
  condicionales y reintento de fallos transitorios. Contratos en [actualizaciones y caché](UPDATES-AND-CACHE.md).

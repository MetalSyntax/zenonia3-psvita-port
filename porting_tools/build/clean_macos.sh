#!/bin/bash
echo "Buscando y eliminando archivos ._ de macOS..."
find . -name "._*" -type f -delete
echo "Limpieza completada."

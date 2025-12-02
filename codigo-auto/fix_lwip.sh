#!/bin/bash
# Script para solucionar el error de compilación de LwIP

echo "🔧 Solucionando configuración LwIP..."

# Eliminar archivos de configuración antiguos
echo "Limpiando configuración antigua..."
rm -f sdkconfig
rm -f sdkconfig.old
rm -rf build/

echo "✅ Archivos de configuración eliminados"
echo ""
echo "Ahora ejecuta desde VS Code ESP-IDF:"
echo "  1. Presiona F1"
echo "  2. Busca 'ESP-IDF: Full Clean'"
echo "  3. Luego 'ESP-IDF: Build your project'"
echo ""
echo "O desde terminal:"
echo "  idf.py fullclean"
echo "  idf.py build"

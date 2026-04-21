"""
Pre-build script: копирует рабочие menu файлы из menu/ в lib/idryer-menu

Логика:
1. В menu/ есть рабочие файлы → копируем в lib/idryer-menu/
2. В menu/ нет файлов, но в lib есть кэш → используем что есть (warning)
3. Нигде нет → ошибка сборки
"""

import os
import shutil
from pathlib import Path

Import("env")

# Пути
PROJECT_DIR = Path(env["PROJECT_DIR"])
SOURCE_DIR = PROJECT_DIR / "menu"
DEST_DIR = PROJECT_DIR / "lib" / "idryer-menu" / "src"

# Файлы для копирования (полный набор v3_nvs для LINK).
# Источник истины — menu/menu_v2.yaml; все .cpp/.h ниже генерируются скриптом
# menu/generator/gen_menu_v3_nvs.py.
MENU_FILES = [
    # Метаданные + идентификаторы + типы
    "menu_meta.h",
    "menu_ids.h",
    "menu_types.h",
    # Кэш значений (in-memory снимок для ESP LINK)
    "menu_cache.h",
    "menu_cache.cpp",
    # State + persist (NVS)
    "menu_state.h",
    "menu_state.cpp",
    "menu_nvs.h",
    "menu_nvs_io.h",
    "menu_nvs_io.cpp",
    # Данные меню + биндинги + weak-заглушки колбэков
    "menu_data.cpp",
    "menu_bindings.h",
    "menu_bindings.cpp",
    "menu_callbacks_weak.cpp",
]

# Устаревшие файлы от v2 (EEPROM backend) — удаляем из DEST_DIR, если остались
# после миграции на v3_nvs.
OBSOLETE_FILES = [
    "menu_eeprom.h",
    "menu_eeprom_io.h",
    "menu_eeprom_io.cpp",
]

def copy_menu_files():
    """Копирует menu файлы перед сборкой"""

    source_exists = SOURCE_DIR.exists() and SOURCE_DIR.is_dir()
    dest_exists = DEST_DIR.exists() and any(DEST_DIR.glob("*.cpp"))

    if source_exists:
        # Копируем из рабочего каталога menu/
        DEST_DIR.mkdir(parents=True, exist_ok=True)

        copied = 0
        for filename in MENU_FILES:
            src = SOURCE_DIR / filename
            dst = DEST_DIR / filename

            if src.exists():
                # Копируем только если файл изменился
                if not dst.exists() or src.stat().st_mtime > dst.stat().st_mtime:
                    shutil.copy2(src, dst)
                    print(f"  [MENU] Copied: {filename}")
                    copied += 1
            else:
                print(f"  [WARNING] File not found in source: {filename}")

        if copied == 0:
            print("  [MENU] Files up to date")

        # Удаляем устаревшие файлы v2 (EEPROM backend) если они остались
        for filename in OBSOLETE_FILES:
            dst = DEST_DIR / filename
            if dst.exists():
                dst.unlink()
                print(f"  [MENU] Removed obsolete: {filename}")

        # Создаём library.json если нет
        lib_json = DEST_DIR.parent / "library.json"
        if not lib_json.exists():
            lib_json.write_text('''{
    "name": "idryer-menu",
    "version": "1.0.0",
    "description": "Menu data structure (auto-copied from RP2040 project)",
    "frameworks": "*",
    "platforms": "*"
}
''')
            print("  [MENU] Created library.json")

    elif dest_exists:
        # Используем кэшированные файлы
        print("  [MENU] Using cached files (menu source not available)")

    else:
        # Нигде нет — ошибка
        print("\n" + "="*60)
        print("ERROR: Menu files not found!")
        print("="*60)
        print(f"Source:           {SOURCE_DIR}")
        print(f"Destination:      {DEST_DIR}")
        print("")
        print("Solutions:")
        print("1. Add generated menu files to:")
        print(f"   {SOURCE_DIR}")
        print("")
        print("2. Or copy menu files manually to cache:")
        print(f"   {DEST_DIR}")
        print("="*60 + "\n")
        env.Exit(1)

# Запускаем при загрузке скрипта (pre-build)
print("[PRE-BUILD] Checking menu files...")
copy_menu_files()

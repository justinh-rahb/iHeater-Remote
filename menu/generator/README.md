# Генератор меню

## Cоздать виртуальное окружение 

python3 -m venv .venv
source .venv/bin/activate
pip install pyyaml

## Установка
```bash
pip install pyyaml
```
## Запуск
python3 menu/generator/gen_menu.py menu/menu.yaml --out menu

python3 menu/generator/gen_menu_v2.py menu/menu_v2.yaml --out menu

python3 menu/generator/gen_menu_v2.py menu/menu_v2.yaml --out menu --num-units 1
